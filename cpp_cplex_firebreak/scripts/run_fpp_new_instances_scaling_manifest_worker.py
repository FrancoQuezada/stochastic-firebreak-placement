#!/usr/bin/env python3
"""Run one worker manifest for the FPP new_instances scaling experiment."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

from fpp_new_instances_scaling_compact_schema import graph_ratio_columns, parse_graph_ratio_summary

WORKER_FIELDS = [
    "worker_status",
    "worker_return_code",
    "worker_started_at_epoch",
    "worker_finished_at_epoch",
    "worker_runtime_seconds",
    "worker_command",
    "worker_log",
    "solver_method",
    "configured_mip_gap",
    "solver_mip_gap",
    "paired_reburn_status",
    "paired_reburn_return_code",
    "paired_reburn_eval_command",
    "paired_reburn_train_expected_burned_area",
    "paired_reburn_train_worst_10pct_burned_area",
    "paired_reburn_train_empirical_var_90pct_burned_area",
    "paired_reburn_train_empirical_cvar_90pct_burned_area",
    "paired_reburn_train_evaluation_runtime_seconds",
    "paired_reburn_train_eval_runtime_sec",
    "paired_reburn_train_scenario_count",
]

OMIT_FIELDS = {
    "projected_llbi_family",
    "projected_llbi_variant",
    "use_root_user_cuts",
    "use_lifted_lower_bounds",
    "use_projected_llbi",
    "use_projected_coverage_llbi_poly",
    "use_projected_path_llbi_poly",
    "use_projected_coverage_llbi_exp",
    "use_projected_path_llbi_exp",
    "use_combinatorial_benders",
    "combinatorial_benders_lift",
    "combinatorial_benders_cut_sampling_ratio",
    "combinatorial_benders_separate_fractional",
    "combinatorial_benders_initial_cuts",
    "projected_llbi_root_rounds",
    "projected_llbi_max_cuts_per_round",
    "projected_llbi_violation_tolerance",
    "projected_llbi_cut_density_limit",
    "projected_poly_max_cuts",
    "split_dir",
    "output_dir",
    "output_csv",
    "output_json",
    "solution_dir",
    "run_id",
    "solver_command",
    "worker_status",
    "worker_return_code",
    "worker_started_at_epoch",
    "worker_finished_at_epoch",
    "worker_command",
    "worker_log",
    "configured_mip_gap",
    "solver_mip_gap",
    "global_dominance_enabled",
    "global_dominance_candidates_removed",
    "global_dominance_equivalence_classes",
    "global_dominance_precompute_time_sec",
    "conditional_zero_benefit_enabled",
    "conditional_zero_benefit_fixings_attempted",
    "conditional_zero_benefit_fixings_applied",
    "conditional_zero_benefit_time_sec",
    "branch_benders_use_root_user_cuts",
    "branch_benders_root_user_cuts_added",
    "branch_benders_root_user_cut_rounds",
    "branch_benders_root_user_cut_max_violation",
    "restricted_candidate_enabled",
    "restricted_candidate_exact_mode",
    "restricted_candidate_initial_active_count",
    "restricted_candidate_final_active_count",
    "restricted_candidate_final_active_fraction",
    "restricted_candidate_eventually_activated_all",
    "restricted_candidate_rounds",
    "restricted_candidate_cut_pool_size",
    "restricted_candidate_heuristic_mode_enabled",
    "restricted_candidate_stopped_before_full_activation",
    "restricted_candidate_global_optimality_certified",
    "formulation",
    "dominator_cuts_enabled",
    "separator_cuts_enabled",
    "greedy_warm_start_enabled",
    "local_search_enabled",
    "compact_node_count",
    "eligible_node_count",
    "total_observed_scenario_nodes",
    "total_scenario_arcs",
    "separator_cuts_added",
    "separator_min_cut_calls",
    "separator_callback_invocations",
    "separator_duplicate_cuts_skipped",
    "separator_large_cuts_skipped",
    "separator_time_sec",
    "dominator_cuts_added",
    "dominator_aggregate_cuts_added",
    "dominator_individual_cuts_added",
    "dominator_dag_scenarios",
    "dominator_fallback_scenarios",
    "dominator_preprocessing_time_sec",
    "heuristic_time_sec",
    "heuristic_objective",
    "heuristic_exact_evaluations",
    "heuristic_selected_count",
    "evaluator_objective",
    "evaluator_abs_diff",
    "evaluator_rel_diff",
    "validation_status",
    "train_cvar_burned_area",
    "test_cvar_burned_area",
    "selected_firebreaks",
    "warm_start_used",
    "mip_start_accepted",
    "warm_start_source",
    "warm_start_valid_nodes",
    "warm_start_ignored_nodes",
    "warm_start_notes",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker-id", required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--binary", type=Path, default=None)
    parser.add_argument("--rerun-existing", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def require_project_root() -> None:
    if not Path("src/main.cpp").exists() or not Path("include").is_dir():
        raise SystemExit("Run this worker from cpp_cplex_firebreak/.")


def bool_value(value: str | None) -> bool:
    return str(value or "").strip().lower() in {"1", "true", "yes", "on"}


def ids_arg(ids: list[int]) -> str:
    return ",".join(str(value) for value in ids)


def read_ids(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    return [int(token) for token in re.split(r"[\s,;]+", text.strip()) if token]


def split_pair(row: dict[str, str]) -> tuple[Path, Path]:
    if row.get("train_split_path") and row.get("test_split_path"):
        return Path(row["train_split_path"]), Path(row["test_split_path"])
    split_dir = Path(row["split_dir"])
    prefix = (
        f"{row.get('instance_id', row['landscape'])}_seed{row['seed']}_train{row['train_count']}_"
        f"test{row['test_count']}_case{row['case_index']}"
    )
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def validate_split(row: dict[str, str], train_ids: list[int], test_ids: list[int]) -> None:
    train_count = int(row["train_count"])
    test_count = int(row["test_count"])
    training_pool_min = int(row.get("training_pool_min", "1"))
    training_pool_max = int(row.get("training_pool_max", "9000"))
    test_pool_min = int(row.get("test_pool_min", "9001"))
    test_pool_max = int(row.get("test_pool_max", "10000"))
    expected_test_ids = list(range(test_pool_min, test_pool_max + 1))
    if len(expected_test_ids) != test_count:
        raise RuntimeError(
            f"Configured fixed OOS range has {len(expected_test_ids)} IDs; expected {test_count}.")
    if len(train_ids) != train_count:
        raise RuntimeError(f"Train split has {len(train_ids)} IDs; expected {train_count}.")
    if len(test_ids) != test_count:
        raise RuntimeError(f"Test split has {len(test_ids)} IDs; expected {test_count}.")
    if len(set(train_ids)) != len(train_ids):
        raise RuntimeError("Train split contains duplicates.")
    if len(set(test_ids)) != len(test_ids):
        raise RuntimeError("Test split contains duplicates.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Train and test split IDs overlap.")
    outside = [value for value in train_ids if value < training_pool_min or value > training_pool_max]
    if outside:
        raise RuntimeError(f"Train split contains IDs outside the training pool: {outside[:10]}")
    if test_ids != expected_test_ids:
        raise RuntimeError("Test split is not exactly the configured fixed OOS set.")


def find_binary(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    env_bin = os.environ.get("FIREBREAK_BIN")
    if env_bin:
        candidates.append(Path(env_bin))
    candidates.extend([Path("build_gpp/firebreak_cpp"), Path("build/firebreak_cpp")])
    for path in candidates:
        if path.exists() and os.access(path, os.X_OK):
            return path
    raise RuntimeError("Could not find firebreak_cpp binary. Build first or set FIREBREAK_BIN.")


def load_instance_config(path: Path) -> dict[str, dict[str, str]]:
    if not path.exists():
        raise RuntimeError(f"Missing instance config: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    config: dict[str, dict[str, str]] = {}
    for row in rows:
        instance_id = row.get("instance_id", "")
        if not instance_id:
            raise RuntimeError(f"Instance config row missing instance_id: {row}")
        if instance_id in config:
            raise RuntimeError(f"Duplicate instance_id in config: {instance_id}")
        config[instance_id] = row
    return config


def paired_reburn_instance_id(instance_id: str) -> str | None:
    if instance_id.endswith("_reburn"):
        return None
    return f"{instance_id}_reburn"


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def load_manifest(path: Path, worker_id: str) -> list[dict[str, str]]:
    if not path.exists():
        raise RuntimeError(f"Missing worker manifest: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = [row for row in csv.DictReader(inp) if row.get("worker_id") == worker_id]
    if not rows:
        raise RuntimeError(f"{path} has no rows for {worker_id}.")
    return rows


def load_existing(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def pair_to_reburn_matrix(config: dict[str, dict[str, str]]) -> dict[str, str]:
    pairs: dict[str, str] = {}
    for instance_id in config:
        reburn_id = paired_reburn_instance_id(instance_id)
        if reburn_id and reburn_id in config:
            pairs[instance_id] = reburn_id
    return pairs


def row_complete(row: dict[str, str]) -> bool:
    if row.get("worker_return_code") == "0":
        return bool(row.get("solver_status") or row.get("status"))
    if row.get("worker_status", "").lower() == "ok":
        return bool(row.get("solver_status") or row.get("status"))
    return bool(row.get("solver_status") or row.get("status"))


def write_worker_csv(path: Path, rows: list[dict[str, str]], manifest_fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields: list[str] = []
    for field in manifest_fields + WORKER_FIELDS:
        if field in OMIT_FIELDS:
            continue
        if field not in fields:
            fields.append(field)
    for row in rows:
        for field in row:
            if field in OMIT_FIELDS:
                continue
            if field not in fields:
                fields.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def weight_map_file(row: dict[str, str]) -> str:
    """The canonical weight-map CSV path for this row, or "" for legacy homogeneous rows.

    The worker NEVER generates a map: it requires a pre-generated file to exist and fails
    clearly otherwise."""
    path = (row.get("weight_map_path") or "").strip()
    if not path:
        return ""
    if not Path(path).exists():
        raise RuntimeError(
            f"Weight map {path} referenced by task {row.get('task_id', '')} does not exist. "
            f"Pre-generate the registry (ensure-weight-map); the worker never regenerates maps.")
    return path


def base_solver_args(
    binary: Path,
    row: dict[str, str],
    train_ids: list[int],
    test_ids: list[int],
    temp_csv: Path,
    solution_json: Path,
    solution_csv: Path,
) -> list[str]:
    args = [
        str(binary),
        row["solver_command"],
        "--landscape", row["landscape"],
        "--forest-path", row["forest_path"],
        "--results-path", row["results_path"],
        "--train-ids", ids_arg(train_ids),
        "--test-ids", ids_arg(test_ids),
        "--alpha", row["alpha"],
        "--time-limit", row["time_limit"],
        "--mip-gap", row["mip_gap"],
        "--threads", row["threads"],
        "--risk-measure", row["risk_measure"],
        "--cvar-beta", row["cvar_beta"],
        "--cvar-lambda", row["cvar_lambda"],
        "--run-id", row["run_id"],
        "--output-json", row["output_json"],
        "--output-csv", str(temp_csv),
        "--solution-json", str(solution_json),
        "--solution-csv", str(solution_csv),
    ]
    wmap = weight_map_file(row)
    if wmap:
        args.extend(["--weight-map-file", wmap])
    return args


def build_command(
    binary: Path,
    row: dict[str, str],
    train_ids: list[int],
    test_ids: list[int],
    temp_csv: Path,
) -> list[str]:
    solution_dir = Path(row["solution_dir"])
    solution_json = solution_dir / f"{row['task_id']}.json"
    solution_csv = solution_dir / f"{row['task_id']}.csv"
    args = base_solver_args(binary, row, train_ids, test_ids, temp_csv, solution_json, solution_csv)

    if row["solver_command"] == "run-fpp-saa-oos":
        args.extend(["--fpp-formulation", "base"])
        return args

    if bool_value(row.get("use_lifted_lower_bounds")):
        args.append("--use-lifted-lower-bounds")
    if bool_value(row.get("use_root_user_cuts")):
        args.extend([
            "--use-root-user-cuts",
            "--root-user-cut-max-rounds", "1",
            "--root-user-cut-tolerance", "1e-6",
        ])
    if bool_value(row.get("use_projected_coverage_llbi_poly")):
        args.append("--use-projected-coverage-llbi-poly")
    if bool_value(row.get("use_projected_path_llbi_poly")):
        args.append("--use-projected-path-llbi-poly")
    if bool_value(row.get("use_projected_coverage_llbi_exp")):
        args.append("--use-projected-coverage-llbi-exp")
    if bool_value(row.get("use_projected_path_llbi_exp")):
        args.append("--use-projected-path-llbi-exp")
    if bool_value(row.get("use_projected_llbi")):
        args.extend([
            "--projected-llbi-root-rounds", row["projected_llbi_root_rounds"],
            "--projected-llbi-max-cuts-per-round", row["projected_llbi_max_cuts_per_round"],
            "--projected-llbi-violation-tolerance", row["projected_llbi_violation_tolerance"],
            "--projected-llbi-cut-density-limit", row["projected_llbi_cut_density_limit"],
            "--projected-poly-max-cuts", row["projected_poly_max_cuts"],
        ])
    if bool_value(row.get("use_combinatorial_benders")):
        args.append("--use-combinatorial-benders")
        args.extend([
            "--combinatorial-benders-lift", row["combinatorial_benders_lift"],
            "--combinatorial-benders-cut-sampling-ratio", row["combinatorial_benders_cut_sampling_ratio"],
            "--combinatorial-benders-scenario-order", row.get("combinatorial_benders_scenario_order", "eta-asc"),
            "--combinatorial-benders-separate-fractional", row["combinatorial_benders_separate_fractional"],
            "--combinatorial-benders-initial-cuts", row["combinatorial_benders_initial_cuts"],
        ])
    return args


def build_paired_reburn_evaluation_command(
    binary: Path,
    reburn_row: dict[str, str],
    train_ids: list[int],
    firebreaks_csv: Path,
    output_json: Path,
    weight_map: str = "",
) -> list[str]:
    command = [
        str(binary),
        "evaluate",
        "--landscape", reburn_row["landscape"],
        "--forest-path", reburn_row["forest_path"],
        "--results-path", reburn_row["results_path"],
        "--scenario-ids", ids_arg(train_ids),
        "--firebreaks", str(firebreaks_csv),
        "--output", str(output_json),
    ]
    # The same canonical map (keyed by original Cell2Fire ID over the full physical
    # universe) is used for the reburn evaluation as for the reduced solve.
    if weight_map:
        command.extend(["--weight-map-file", weight_map])
    return command


def parse_paired_reburn_evaluation_json(path: Path) -> dict[str, str]:
    if not path.exists():
        raise RuntimeError(f"Paired reburn evaluation JSON missing: {path}")
    payload = json.loads(path.read_text(encoding="utf-8"))
    return {
        "paired_reburn_train_expected_burned_area": str(payload.get("expected_burned_area", "")),
        "paired_reburn_train_worst_10pct_burned_area": str(payload.get("worst_10pct_burned_area", "")),
        "paired_reburn_train_empirical_var_90pct_burned_area": str(payload.get("empirical_var_90pct_burned_area", "")),
        "paired_reburn_train_empirical_cvar_90pct_burned_area": str(payload.get("empirical_cvar_90pct_burned_area", "")),
        "paired_reburn_train_evaluation_runtime_seconds": str(payload.get("total_runtime_seconds", "")),
        "paired_reburn_train_scenario_count": str(len(payload.get("scenarios", []))),
    }


def read_single_solver_row(path: Path) -> dict[str, str]:
    if not path.exists():
        raise RuntimeError(f"Solver did not write expected row CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != 1:
        raise RuntimeError(f"{path} contains {len(rows)} rows; expected exactly 1.")
    return rows[0]


def patch_result_json_metadata(row: dict[str, str]) -> None:
    path = Path(row["output_json"])
    if not path.exists():
        return
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return
    for key in [
        "task_id",
        "worker_id",
        "case_id",
        "case_index",
        "seed_base",
        "seed",
        "split_seed",
        "instance_id",
        "folder_name",
        "instance_type",
        "declared_cells",
        "inferred_cells",
        "metadata_status",
        "metadata_consistent",
        "graph_variant",
        "objective_family",
        "method",
        "method_family",
        "fpp_mode",
        "risk_measure",
        "cvar_beta",
        "cvar_lambda",
        "projected_llbi_family",
        "projected_llbi_variant",
        "use_root_user_cuts",
        "use_lifted_lower_bounds",
        "use_projected_llbi",
        "use_combinatorial_benders",
    ]:
        if key in row:
            payload[key] = row[key]
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def merged_result(
    manifest_row: dict[str, str],
    solver_row: dict[str, str],
    *,
    train_ids: list[int],
    test_ids: list[int],
    command: list[str],
    log_path: Path,
    return_code: int,
    started: float,
    finished: float,
) -> dict[str, str]:
    out = dict(solver_row)
    solver_mip_gap = solver_row.get("mip_gap", "")
    out["solver_method"] = solver_row.get("method", "")
    out.update(manifest_row)
    out["configured_mip_gap"] = manifest_row.get("mip_gap", "")
    out["solver_mip_gap"] = solver_mip_gap
    if solver_mip_gap:
        out["mip_gap"] = solver_mip_gap
    out["train_ids"] = ids_arg(train_ids)
    out["test_ids"] = ids_arg(test_ids)
    out["train_scenario_count"] = str(len(train_ids))
    out["test_scenario_count"] = str(len(test_ids))
    out["worker_status"] = "ok" if return_code == 0 else "failed"
    out["worker_return_code"] = str(return_code)
    out["worker_started_at_epoch"] = f"{started:.6f}"
    out["worker_finished_at_epoch"] = f"{finished:.6f}"
    out["worker_runtime_seconds"] = f"{finished - started:.6f}"
    out["worker_command"] = " ".join(shlex.quote(part) for part in command)
    out["worker_log"] = str(log_path)
    out.update(graph_ratio_columns("train", parse_graph_ratio_summary(out.get("train_graph_classification_ratios", ""))))
    out.update(graph_ratio_columns("test", parse_graph_ratio_summary(out.get("test_graph_classification_ratios", ""))))
    out.update(graph_ratio_columns("instance", parse_graph_ratio_summary(out.get("instance_graph_classification_ratios", ""))))
    return out


def main() -> int:
    args = parse_args()
    require_project_root()
    binary = find_binary(args.binary)
    manifest_rows = load_manifest(args.manifest, args.worker_id)
    manifest_fields = list(manifest_rows[0].keys())
    output_csv = Path(manifest_rows[0]["output_csv"])
    output_dir = Path(manifest_rows[0]["output_dir"])
    log_dir = output_dir / "logs"
    temp_dir = log_dir / "row_csv"
    log_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)
    Path(manifest_rows[0]["solution_dir"]).mkdir(parents=True, exist_ok=True)
    (output_dir / "json").mkdir(parents=True, exist_ok=True)

    existing = [] if args.rerun_existing else load_existing(output_csv)
    by_task = {row.get("task_id", ""): row for row in existing if row.get("task_id")}
    final_rows = [row for row in existing if row.get("task_id")]

    for row in manifest_rows:
        task_id = row["task_id"]
        if task_id in by_task and row_complete(by_task[task_id]):
            print(f"SKIP complete {task_id}: {row['method']}")
            continue

        train_path, test_path = split_pair(row)
        if not train_path.exists() or not test_path.exists():
            raise RuntimeError(f"Missing split pair for {task_id}: {train_path}, {test_path}")
        train_ids = read_ids(train_path)
        test_ids = read_ids(test_path)
        validate_split(row, train_ids, test_ids)

        temp_csv = temp_dir / f"{task_id}.csv"
        if temp_csv.exists():
            temp_csv.unlink()
        log_path = log_dir / f"{task_id}.log"
        command = build_command(binary, row, train_ids, test_ids, temp_csv)
        print(f"START {task_id}: {row['method']}")
        print("  " + " ".join(shlex.quote(part) for part in command))
        if args.dry_run:
            continue

        started = time.time()
        with log_path.open("w", encoding="utf-8") as log:
            log.write("COMMAND: " + " ".join(shlex.quote(part) for part in command) + "\n")
            log.flush()
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        finished = time.time()
        if completed.returncode != 0:
            failed_row = merged_result(
                row, {}, train_ids=train_ids, test_ids=test_ids, command=command,
                log_path=log_path, return_code=completed.returncode, started=started, finished=finished)
            by_task[task_id] = failed_row
            final_rows = [r for r in final_rows if r.get("task_id") != task_id] + [failed_row]
            write_worker_csv(output_csv, final_rows, manifest_fields)
            print(f"FAILED {task_id}; see {log_path}", file=sys.stderr)
            return completed.returncode

        patch_result_json_metadata(row)
        solver_row = read_single_solver_row(temp_csv)

        # Verify the solver loaded exactly the canonical map named in the manifest.
        expected_hash = (row.get("weight_map_hash") or "").strip()
        actual_hash = (solver_row.get("weight_map_hash") or "").strip()
        if expected_hash and actual_hash and expected_hash != actual_hash:
            raise RuntimeError(
                f"Weight-map hash mismatch for {task_id}: manifest {expected_hash} != "
                f"result {actual_hash}.")

        completed_row = merged_result(
            row, solver_row, train_ids=train_ids, test_ids=test_ids, command=command,
            log_path=log_path, return_code=0, started=started, finished=finished)

        instance_config = load_instance_config(Path("config/fpp_new_instances_scaling_instances.csv"))
        reburn_pairs = pair_to_reburn_matrix(instance_config)
        paired_reburn_id = reburn_pairs.get(row["instance_id"])
        if paired_reburn_id is not None:
            reburn_row = instance_config[paired_reburn_id]
            reburn_eval_json = output_dir / "json" / f"{task_id}_paired_reburn_eval.json"
            reburn_command = build_paired_reburn_evaluation_command(
                binary, reburn_row, train_ids, Path(row["solution_dir"]) / f"{row['task_id']}.csv",
                reburn_eval_json, weight_map=weight_map_file(row))
            completed_row["paired_reburn_eval_command"] = " ".join(shlex.quote(part) for part in reburn_command)
            reburn_started = time.time()
            with log_path.open("a", encoding="utf-8") as log:
                log.write("\nPAIRED REBURN COMMAND: " + " ".join(shlex.quote(part) for part in reburn_command) + "\n")
                log.flush()
                paired_completed = subprocess.run(reburn_command, stdout=log, stderr=subprocess.STDOUT)
            reburn_finished = time.time()
            if paired_completed.returncode != 0:
                completed_row["paired_reburn_status"] = "failed"
                completed_row["paired_reburn_return_code"] = str(paired_completed.returncode)
            else:
                completed_row["paired_reburn_status"] = "ok"
                completed_row["paired_reburn_return_code"] = "0"
                completed_row.update(parse_paired_reburn_evaluation_json(reburn_eval_json))
                completed_row["paired_reburn_train_eval_runtime_sec"] = str(reburn_finished - reburn_started)
        else:
            completed_row["paired_reburn_status"] = "n/a"

        by_task[task_id] = completed_row
        final_rows = [r for r in final_rows if r.get("task_id") != task_id] + [completed_row]
        final_rows.sort(key=lambda r: r.get("task_id", ""))
        write_worker_csv(output_csv, final_rows, manifest_fields)
        print(f"END {task_id}: {finished - started:.3f}s")

    if args.dry_run:
        print("Dry run complete; no solver commands were executed.")
        return 0
    completed_count = sum(1 for row in final_rows if row_complete(row))
    expected_count = len(manifest_rows)
    if completed_count != expected_count:
        print(f"{args.worker_id} has {completed_count} complete rows; expected {expected_count}.", file=sys.stderr)
        return 1
    print(f"{args.worker_id} complete: {expected_count} rows written to {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
