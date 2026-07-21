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
    "execution_status",
    "attempt",
    "resume_action",
    "failure_stage",
    "failure_type",
    "failure_message",
    "worker_exit_code",
    "optimization_weight_map_hash",
    "out_of_sample_weight_map_hash",
    "paired_reburn_weight_map_hash",
    "paired_reburn_instance_requested",
    "paired_reburn_instance_resolved",
    "paired_reburn_resolution_method",
    "paired_reburn_resolution_status",
    "paired_selected_firebreak_count",
    "paired_selected_firebreaks_mapped",
    "paired_selected_firebreaks_missing",
    "paired_selected_mapping_status",
    "selected_firebreak_original_ids",
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
    "solver_command",
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
    parser.add_argument("--rerun-existing", action="store_true",
                        help="Discard ALL existing rows for this worker and resolve everything "
                             "from scratch (including previously-successful rows).")
    parser.add_argument("--retry-failed", action="store_true",
                        help="Rerun rows whose existing result is a clean recorded failure, "
                             "preserving the logical run_id/train_ids/weight map and incrementing "
                             "the attempt counter. Rows with a malformed/incomplete result are "
                             "always rerun regardless of this flag. Completed valid rows are "
                             "never rerun by this flag.")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.rerun_existing and args.retry_failed:
        raise SystemExit("--rerun-existing and --retry-failed are contradictory; choose one.")
    return args


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


def resolve_paired_reburn_instance(
    instance_config: dict[str, dict[str, str]], instance_id: str
) -> tuple[str | None, str, str]:
    """Resolve instance_id's paired reburn instance (Phase 8B section 11).

    Returns (resolved_reburn_id_or_None, resolution_method, resolution_status). Pairing
    is derived from the instance config (folder-suffix convention), then cross-checked
    against declared_cells so a corrupted/mismatched pairing (e.g. the documented
    100x100 folder mismatch) is rejected rather than silently used.
    """
    requested_id = paired_reburn_instance_id(instance_id)
    method = "instance_config_suffix_and_cell_count_match"
    if requested_id is None:
        return None, method, "not_applicable"
    if requested_id not in instance_config:
        return None, method, "unavailable"
    reduced_cells = str(instance_config.get(instance_id, {}).get("declared_cells", "")).strip()
    reburn_cells = str(instance_config[requested_id].get("declared_cells", "")).strip()
    if reduced_cells and reburn_cells and reduced_cells != reburn_cells:
        return None, method, "cell_count_mismatch"
    return requested_id, method, "resolved"


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


def read_result_json(row: dict[str, str]) -> dict | None:
    path_str = row.get("output_json", "")
    if not path_str:
        return None
    path = Path(path_str)
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def row_complete_and_valid(row: dict[str, str]) -> bool:
    """Real completion validation (Phase 8B section 21). File existence alone is never
    sufficient: the result JSON must exist, parse, name the expected run_id and weight
    map hash, carry a final status, and (when paired evaluation was required) report a
    successful paired reburn evaluation."""
    if row.get("worker_return_code") != "0":
        return False
    payload = read_result_json(row)
    if payload is None:
        return False
    if payload.get("run_id", "") != row.get("run_id", ""):
        return False
    expected_hash = (row.get("weight_map_hash") or "").strip()
    if expected_hash and str(payload.get("weight_map_hash", "")) != expected_hash:
        return False
    status = payload.get("solver_status") or payload.get("status") or ""
    if not str(status).strip():
        return False
    if "objective_validation_passed" not in payload:
        return False
    solution_path = Path(row.get("solution_dir", "")) / f"{row.get('task_id', '')}.csv"
    if not solution_path.exists():
        return False
    if bool_value(row.get("paired_evaluation_enabled")) and row.get("paired_reburn_status") != "ok":
        return False
    return True


def is_recorded_failure(row: dict[str, str]) -> bool:
    """A CLEAN failure: the worker ran, exited nonzero, and left a coherent failure
    record (as opposed to a malformed/interrupted row, which is always rerun)."""
    return (
        row.get("worker_return_code", "") not in ("", "0")
        and row.get("worker_status", "").lower() == "failed"
    )


def write_worker_csv(path: Path, rows: list[dict[str, str]], manifest_fields: list[str]) -> None:
    """Write the worker CSV atomically: a partially written file left by an interrupted
    worker is never mistaken for a complete one (Phase 8B section 20)."""
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
    tmp_path = path.with_suffix(path.suffix + f".tmp{os.getpid()}")
    with tmp_path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
        out.flush()
        os.fsync(out.fileno())
    os.replace(tmp_path, path)


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


DPV_HEURISTIC_COMMANDS = {"run-static-dpv-oos", "run-static-dpv-mip-oos", "run-greedy-oos"}
DPV_OPTIMIZATION_COMMANDS = {"run-dpv-saa-oos", "run-dpv-benders-oos", "run-dpv-branch-benders-oos"}


def build_dpv_family_command(
    binary: Path,
    row: dict[str, str],
    train_ids: list[int],
    test_ids: list[int],
    temp_csv: Path,
    solution_json: Path,
    solution_csv: Path,
) -> list[str]:
    """DPV / Static-DPV / Greedy-DPV commands share a minimal arg set: none of them
    accept --risk-measure/--cvar-beta/--cvar-lambda (DPV-CVaR is out of scope), and the
    pure-heuristic commands additionally have no --time-limit/--mip-gap/--threads."""
    solver_command = row["solver_command"]
    args = [
        str(binary),
        solver_command,
        "--landscape", row["landscape"],
        "--forest-path", row["forest_path"],
        "--results-path", row["results_path"],
        "--train-ids", ids_arg(train_ids),
        "--test-ids", ids_arg(test_ids),
        "--alpha", row["alpha"],
        "--run-id", row["run_id"],
        "--output-json", row["output_json"],
        "--output-csv", str(temp_csv),
        "--solution-json", str(solution_json),
        "--solution-csv", str(solution_csv),
        "--dpv-ignition-policy", "fpp-safe",
    ]
    wmap = weight_map_file(row)
    if wmap:
        args.extend(["--weight-map-file", wmap])
    if solver_command in DPV_OPTIMIZATION_COMMANDS:
        args.extend([
            "--time-limit", row["time_limit"],
            "--mip-gap", row["mip_gap"],
            "--threads", row["threads"],
        ])
    if solver_command == "run-greedy-oos":
        args.extend(["--metric", row.get("greedy_metric") or "DPV3"])
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

    if row["solver_command"] in DPV_HEURISTIC_COMMANDS or row["solver_command"] in DPV_OPTIMIZATION_COMMANDS:
        return build_dpv_family_command(
            binary, row, train_ids, test_ids, temp_csv, solution_json, solution_csv)

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


def read_selected_firebreak_ids(solution_csv: Path) -> list[int]:
    """The solver writes selected original Cell2Fire firebreak IDs as a single
    comma-separated line (io::save_firebreak_solution_csv); never compact indices."""
    if not solution_csv.exists():
        raise RuntimeError(f"Solution CSV missing: {solution_csv}")
    text = solution_csv.read_text(encoding="utf-8").strip()
    if not text:
        return []
    ids = [int(token) for token in text.split(",")]
    if len(set(ids)) != len(ids):
        raise RuntimeError(f"Solution CSV {solution_csv} contains duplicate selected firebreak IDs.")
    return ids


def build_paired_reburn_evaluation_command(
    binary: Path,
    reburn_row: dict[str, str],
    train_ids: list[int],
    selected_firebreak_ids: list[int],
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
        # The selected original Cell2Fire firebreak IDs from the reduced-instance
        # solution, transferred by original ID (never compact indices) to the reburn
        # instance's evaluation.
        "--firebreaks", ids_arg(selected_firebreak_ids),
        "--output", str(output_json),
        # A selected firebreak missing from the reburn instance is a hard failure, never
        # a silently dropped cell (Phase 8B section 12).
        "--require-full-firebreak-coverage",
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
        "paired_reburn_weight_map_hash": str(payload.get("weight_map_hash", "")),
        "paired_selected_firebreak_count": str(payload.get("paired_selected_firebreak_count", "")),
        "paired_selected_firebreaks_mapped": str(payload.get("paired_selected_firebreaks_mapped", "")),
        "paired_selected_firebreaks_missing": str(payload.get("paired_selected_firebreaks_missing", "")),
        "paired_selected_mapping_status": str(payload.get("paired_selected_mapping_status", "")),
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


def classify_failure(log_path: Path) -> tuple[str, str]:
    """Best-effort failure_stage/failure_type classification from the solver log tail
    (Phase 8B section 25). The C++ binary reports weight-map/mapping/solver failures
    within a single subprocess call; this keeps failures from collapsing into a single
    generic nonzero-exit-code bucket."""
    try:
        text = log_path.read_text(encoding="utf-8", errors="replace").lower()
    except OSError:
        text = ""
    if "weight map hash mismatch" in text or "weight-map hash mismatch" in text:
        return "weight_map_loading", "weight_map_hash_mismatch"
    if "weight map" in text and ("missing" in text or "does not exist" in text):
        return "weight_map_loading", "weight_map_missing_or_invalid"
    if "requires full firebreak coverage" in text:
        return "paired_firebreak_mapping", "missing_selected_firebreak"
    if "outside the compact evaluation universe" in text or "instance universe" in text:
        return "instance_mapping", "node_mapping_error"
    return "solver_execution", "nonzero_exit"


def failure_message_from_log(log_path: Path, limit: int = 500) -> str:
    try:
        text = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""
    lines = [line for line in text.splitlines() if line.strip()]
    return " | ".join(lines[-5:])[-limit:]


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

    instance_config = load_instance_config(Path("config/fpp_new_instances_scaling_instances.csv"))

    def record_failure(row, failure_stage, failure_type, failure_message, return_code,
                       train_ids, test_ids, command, log_path, started, finished, attempt,
                       resume_action):
        failed_row = merged_result(
            row, {}, train_ids=train_ids, test_ids=test_ids, command=command,
            log_path=log_path, return_code=return_code, started=started, finished=finished)
        failed_row["failure_stage"] = failure_stage
        failed_row["failure_type"] = failure_type
        failed_row["failure_message"] = failure_message[:500]
        failed_row["worker_exit_code"] = str(return_code)
        failed_row["attempt"] = str(attempt)
        failed_row["resume_action"] = resume_action
        by_task[row["task_id"]] = failed_row
        nonlocal final_rows
        final_rows = [r for r in final_rows if r.get("task_id") != row["task_id"]] + [failed_row]
        write_worker_csv(output_csv, final_rows, manifest_fields)

    for row in manifest_rows:
        task_id = row["task_id"]
        existing_row = by_task.get(task_id)
        attempt = 1
        resume_action = "run_missing"

        if existing_row is not None:
            if row_complete_and_valid(existing_row):
                print(f"SKIP complete {task_id}: {row['method']}")
                continue
            if is_recorded_failure(existing_row) and not args.retry_failed:
                print(f"SKIP failed (rerun with --retry-failed) {task_id}: {row['method']}")
                continue
            attempt = int(existing_row.get("attempt") or "0") + 1
            resume_action = "retry_failed" if is_recorded_failure(existing_row) else "rerun_invalid"

        log_path = log_dir / f"{task_id}.log"

        try:
            train_path, test_path = split_pair(row)
            if not train_path.exists() or not test_path.exists():
                raise RuntimeError(f"Missing split pair for {task_id}: {train_path}, {test_path}")
            train_ids = read_ids(train_path)
            test_ids = read_ids(test_path)
            validate_split(row, train_ids, test_ids)
        except Exception as exc:  # noqa: BLE001 - recorded as a row failure, not raised
            log_path.write_text(f"MANIFEST VALIDATION FAILURE: {exc}\n", encoding="utf-8")
            record_failure(
                row, "manifest_validation", type(exc).__name__, str(exc), 1,
                [], [], [], log_path, time.time(), time.time(), attempt, resume_action)
            print(f"FAILED {task_id} (manifest_validation): {exc}", file=sys.stderr)
            continue

        temp_csv = temp_dir / f"{task_id}.csv"
        if temp_csv.exists():
            temp_csv.unlink()
        try:
            command = build_command(binary, row, train_ids, test_ids, temp_csv)
        except Exception as exc:  # noqa: BLE001 - recorded as a row failure, not raised
            # Covers the worker no-regeneration contract: a missing/invalid weight map
            # referenced by the manifest fails this row cleanly before any solver
            # subprocess is launched, and the worker continues with the remaining rows.
            log_path.write_text(f"COMMAND CONSTRUCTION FAILURE: {exc}\n", encoding="utf-8")
            record_failure(
                row, "weight_map_loading", type(exc).__name__, str(exc), 1,
                train_ids, test_ids, [], log_path, time.time(), time.time(), attempt,
                resume_action)
            print(f"FAILED {task_id} (weight_map_loading): {exc}", file=sys.stderr)
            continue
        print(f"START {task_id} (attempt {attempt}): {row['method']}")
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
            failure_stage, failure_type = classify_failure(log_path)
            record_failure(
                row, failure_stage, failure_type, failure_message_from_log(log_path),
                completed.returncode, train_ids, test_ids, command, log_path, started,
                finished, attempt, resume_action)
            print(f"FAILED {task_id}; see {log_path}", file=sys.stderr)
            continue

        patch_result_json_metadata(row)
        try:
            solver_row = read_single_solver_row(temp_csv)
            # Verify the solver loaded exactly the canonical map named in the manifest.
            expected_hash = (row.get("weight_map_hash") or "").strip()
            actual_hash = (solver_row.get("weight_map_hash") or "").strip()
            if expected_hash and actual_hash and expected_hash != actual_hash:
                raise RuntimeError(
                    f"Weight-map hash mismatch for {task_id}: manifest {expected_hash} != "
                    f"result {actual_hash}.")
        except Exception as exc:  # noqa: BLE001 - recorded as a row failure, not raised
            record_failure(
                row, "result_validation", type(exc).__name__, str(exc), 1,
                train_ids, test_ids, command, log_path, started, finished, attempt,
                resume_action)
            print(f"FAILED {task_id} (result_validation): {exc}", file=sys.stderr)
            continue

        completed_row = merged_result(
            row, solver_row, train_ids=train_ids, test_ids=test_ids, command=command,
            log_path=log_path, return_code=0, started=started, finished=finished)
        completed_row["attempt"] = str(attempt)
        completed_row["resume_action"] = resume_action
        completed_row["optimization_weight_map_hash"] = actual_hash
        # The same --weight-map-file is attached for both the train and test
        # OptimizationInstance within one solver invocation (see MethodDispatcher),
        # so the out-of-sample stage necessarily loaded the identical canonical hash.
        completed_row["out_of_sample_weight_map_hash"] = actual_hash
        raw_status = str(solver_row.get("solver_status", "")).strip()
        completed_row["execution_status"] = (
            "heuristic_completed" if raw_status.lower() in {"notapplicable", "not_applicable", ""}
            else raw_status)

        selected_firebreak_ids = read_selected_firebreak_ids(
            Path(row["solution_dir"]) / f"{row['task_id']}.csv")
        completed_row["selected_firebreak_original_ids"] = ids_arg(selected_firebreak_ids)

        reburn_id, resolution_method, resolution_status = resolve_paired_reburn_instance(
            instance_config, row["instance_id"])
        completed_row["paired_reburn_instance_requested"] = paired_reburn_instance_id(row["instance_id"]) or ""
        completed_row["paired_reburn_instance_resolved"] = reburn_id or ""
        completed_row["paired_reburn_resolution_method"] = resolution_method
        completed_row["paired_reburn_resolution_status"] = resolution_status

        if reburn_id is not None:
            reburn_row = instance_config[reburn_id]
            reburn_eval_json = output_dir / "json" / f"{task_id}_paired_reburn_eval.json"
            reburn_command = build_paired_reburn_evaluation_command(
                binary, reburn_row, train_ids, selected_firebreak_ids,
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
                completed_row["failure_stage"] = "paired_evaluation"
                completed_row["failure_type"] = "nonzero_exit"
                completed_row["failure_message"] = failure_message_from_log(log_path)
            else:
                completed_row["paired_reburn_status"] = "ok"
                completed_row["paired_reburn_return_code"] = "0"
                completed_row.update(parse_paired_reburn_evaluation_json(reburn_eval_json))
                completed_row["paired_reburn_train_eval_runtime_sec"] = str(reburn_finished - reburn_started)
        elif bool_value(row.get("paired_evaluation_enabled")):
            # The manifest expected paired evaluation but resolution was rejected
            # (unavailable / ambiguous / cell-count mismatch): fail clearly, never
            # silently proceed as if pairing were not required.
            completed_row["paired_reburn_status"] = "unavailable"
            completed_row["failure_stage"] = "paired_instance_resolution"
            completed_row["failure_type"] = resolution_status
            completed_row["failure_message"] = (
                f"Could not resolve paired reburn instance for {row['instance_id']}: "
                f"{resolution_status}.")
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
    completed_count = sum(1 for row in final_rows if row_complete_and_valid(row))
    expected_count = len(manifest_rows)
    if completed_count != expected_count:
        print(f"{args.worker_id} has {completed_count} complete rows; expected {expected_count}.", file=sys.stderr)
        return 1
    print(f"{args.worker_id} complete: {expected_count} rows written to {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
