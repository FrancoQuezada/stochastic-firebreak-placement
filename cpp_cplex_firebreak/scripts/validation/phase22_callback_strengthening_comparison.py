#!/usr/bin/env python3
"""Run Phase 22 callback Branch-and-Benders strengthening comparison."""

from __future__ import annotations

import argparse
import csv
import json
import random
import re
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ALPHAS = (0.01, 0.02)
TRAIN_COUNTS = (2, 5)
TEST_COUNT = 10
NUM_CASES = 3
SEED_BASE = 22000
TIME_LIMIT = 120
MIP_GAP = 0.001
THREADS = 1
TOLERANCE = 1.0e-6
ROOT_USER_CUT_MAX_ROUNDS = 1


@dataclass(frozen=True)
class VariantSpec:
    name: str
    flags: tuple[str, ...] = ()


VARIANTS = (
    VariantSpec("branch_benders_baseline"),
    VariantSpec("branch_benders_llbi", ("--use-lifted-lower-bounds",)),
    VariantSpec(
        "branch_benders_root_cuts",
        ("--use-root-user-cuts", "--root-user-cut-max-rounds", str(ROOT_USER_CUT_MAX_ROUNDS)),
    ),
    VariantSpec(
        "branch_benders_llbi_root_cuts",
        (
            "--use-lifted-lower-bounds",
            "--use-root-user-cuts",
            "--root-user-cut-max-rounds",
            str(ROOT_USER_CUT_MAX_ROUNDS),
        ),
    ),
)

MAIN_COLUMNS = (
    "alpha",
    "train_count",
    "case_id",
    "variant",
    "train_ids",
    "test_ids",
    "solver_status",
    "objective",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "selected_firebreaks",
    "train_expected_burned_area",
    "train_worst_10pct_burned_area",
    "test_expected_burned_area",
    "test_worst_10pct_burned_area",
    "branch_benders_candidate_incumbents_checked",
    "branch_benders_lazy_cuts_added",
    "branch_benders_max_cut_violation",
    "branch_benders_use_lifted_lower_bounds",
    "branch_benders_lifted_lower_bound_count",
    "branch_benders_lifted_lower_bound_precompute_time_sec",
    "branch_benders_use_root_user_cuts",
    "branch_benders_root_user_cuts_added",
    "branch_benders_root_user_cut_rounds",
    "branch_benders_root_user_cut_max_violation",
    "branch_benders_root_user_cut_total_time_sec",
    "notes",
)

PAIRWISE_COLUMNS = (
    "alpha",
    "train_count",
    "case_id",
    "baseline_objective",
    "llbi_objective",
    "root_cuts_objective",
    "llbi_root_cuts_objective",
    "max_objective_difference",
    "all_objectives_match",
    "baseline_selected_firebreaks",
    "llbi_selected_firebreaks",
    "root_cuts_selected_firebreaks",
    "llbi_root_cuts_selected_firebreaks",
    "all_selected_firebreaks_match",
    "all_train_metrics_match",
    "all_test_metrics_match",
    "fastest_variant",
    "fewest_lazy_cuts_variant",
    "fewest_candidate_checks_variant",
    "notes",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run Phase 22 DPV callback strengthening comparison."
    )
    parser.add_argument("--exe", default="build_gpp/firebreak_cpp")
    parser.add_argument("--landscape", default="Sub20")
    parser.add_argument("--forest-path", default="../sample_test/data/CanadianFBP/Sub20")
    parser.add_argument("--results-path", default="../sample_test/Sub20")
    parser.add_argument("--output-dir", default="results/phase22_callback_strengthening")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-existing", action="store_true")
    parser.add_argument(
        "--max-solves",
        type=int,
        default=0,
        help="Optional cap for debugging; 0 runs the full grid.",
    )
    return parser.parse_args()


def alpha_tag(alpha: float) -> str:
    return f"alpha{int(round(alpha * 1000)):03d}"


def list_scenario_ids(results_path: Path) -> list[int]:
    messages_dir = results_path / "Messages"
    if not messages_dir.exists():
        raise RuntimeError(f"Messages directory not found: {messages_dir}")
    ids: list[int] = []
    pattern = re.compile(r"MessagesFile0*([0-9]+)\.csv$")
    for path in messages_dir.glob("MessagesFile*.csv"):
        match = pattern.match(path.name)
        if match:
            ids.append(int(match.group(1)))
    ids = sorted(set(ids))
    if not ids:
        raise RuntimeError(f"No scenario message files found in {messages_dir}")
    return ids


def generated_split(available_ids: list[int], seed: int, train_count: int) -> tuple[list[int], list[int]]:
    ids = list(available_ids)
    rng = random.Random(seed)
    rng.shuffle(ids)
    train_ids = sorted(ids[:train_count])
    test_ids = sorted(ids[train_count : train_count + TEST_COUNT])
    if len(train_ids) != train_count or len(test_ids) != TEST_COUNT:
        raise RuntimeError("Generated split has wrong size.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Generated split is not disjoint.")
    return train_ids, test_ids


def join_ids(ids: list[int]) -> str:
    return ",".join(str(value) for value in ids)


def csv_ids(ids: list[int]) -> str:
    return ";".join(str(value) for value in ids)


def selected_firebreaks_text(value: Any) -> str:
    if isinstance(value, list):
        return ";".join(str(int(v)) for v in value)
    return ""


def float_or_zero(value: Any) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def int_or_zero(value: Any) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


def bool_text(value: Any) -> str:
    return "true" if bool(value) else "false"


def run_id_for(variant: VariantSpec, alpha: float, train_count: int, case_id: int) -> str:
    return f"phase22_{variant.name}_{alpha_tag(alpha)}_train{train_count}_case{case_id}"


def build_command(
    args: argparse.Namespace,
    variant: VariantSpec,
    alpha: float,
    train_count: int,
    case_id: int,
    train_ids: list[int],
    test_ids: list[int],
    json_path: Path,
    raw_csv_path: Path,
) -> list[str]:
    return [
        args.exe,
        "run-dpv-branch-benders-oos",
        "--landscape",
        args.landscape,
        "--forest-path",
        args.forest_path,
        "--results-path",
        args.results_path,
        "--train-ids",
        join_ids(train_ids),
        "--test-ids",
        join_ids(test_ids),
        "--alpha",
        str(alpha),
        "--time-limit",
        str(TIME_LIMIT),
        "--mip-gap",
        str(MIP_GAP),
        "--threads",
        str(THREADS),
        "--tolerance",
        str(TOLERANCE),
        "--run-id",
        run_id_for(variant, alpha, train_count, case_id),
        "--output-json",
        str(json_path),
        "--output-csv",
        str(raw_csv_path),
        *variant.flags,
    ]


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def row_from_json(
    data: dict[str, Any],
    variant: VariantSpec,
    alpha: float,
    train_count: int,
    case_id: int,
) -> dict[str, Any]:
    benders = data.get("benders", {})
    branch = data.get("branch_benders", {})
    use_llbi = benders.get("use_lifted_lower_bounds", "--use-lifted-lower-bounds" in variant.flags)
    use_root = branch.get("use_root_user_cuts", "--use-root-user-cuts" in variant.flags)

    notes: list[str] = []
    status = str(data.get("solver_status", ""))
    if status != "Optimal":
        notes.append("non_optimal_status")
    if float_or_zero(branch.get("max_cut_violation", data.get("max_cut_violation"))) > TOLERANCE:
        notes.append("final_violation_above_tolerance")
    if use_root and not branch.get("root_user_cut_only_at_root_confirmed", True):
        notes.append("root_only_not_confirmed")
    if not notes:
        notes.append("ok")

    return {
        "alpha": alpha,
        "train_count": train_count,
        "case_id": case_id,
        "variant": variant.name,
        "train_ids": csv_ids([int(v) for v in data.get("train_ids", [])]),
        "test_ids": csv_ids([int(v) for v in data.get("test_ids", [])]),
        "solver_status": status,
        "objective": float_or_zero(data.get("objective_in_sample")),
        "best_bound": float_or_zero(data.get("best_bound")),
        "mip_gap": float_or_zero(data.get("mip_gap")),
        "runtime_seconds": float_or_zero(data.get("runtime_seconds")),
        "selected_firebreaks": selected_firebreaks_text(data.get("selected_firebreaks")),
        "train_expected_burned_area": float_or_zero(data.get("train_expected_burned_area")),
        "train_worst_10pct_burned_area": float_or_zero(data.get("train_worst_10pct_burned_area")),
        "test_expected_burned_area": float_or_zero(data.get("test_expected_burned_area")),
        "test_worst_10pct_burned_area": float_or_zero(data.get("test_worst_10pct_burned_area")),
        "branch_benders_candidate_incumbents_checked": int_or_zero(
            branch.get("candidate_incumbents_checked")
        ),
        "branch_benders_lazy_cuts_added": int_or_zero(branch.get("lazy_cuts_added")),
        "branch_benders_max_cut_violation": float_or_zero(
            branch.get("max_cut_violation", data.get("max_cut_violation"))
        ),
        "branch_benders_use_lifted_lower_bounds": bool_text(use_llbi),
        "branch_benders_lifted_lower_bound_count": int_or_zero(
            benders.get("lifted_lower_bound_count")
        ),
        "branch_benders_lifted_lower_bound_precompute_time_sec": float_or_zero(
            benders.get("lifted_lower_bound_precompute_time_sec")
        ),
        "branch_benders_use_root_user_cuts": bool_text(use_root),
        "branch_benders_root_user_cuts_added": int_or_zero(branch.get("root_user_cuts_added")),
        "branch_benders_root_user_cut_rounds": int_or_zero(
            branch.get("root_user_cut_rounds_executed")
        ),
        "branch_benders_root_user_cut_max_violation": float_or_zero(
            branch.get("root_user_cut_max_violation")
        ),
        "branch_benders_root_user_cut_total_time_sec": float_or_zero(
            branch.get("root_user_cut_total_time_sec")
        ),
        "notes": ";".join(notes),
    }


def failure_row(
    variant: VariantSpec,
    alpha: float,
    train_count: int,
    case_id: int,
    train_ids: list[int],
    test_ids: list[int],
    note: str,
) -> dict[str, Any]:
    row = {column: "" for column in MAIN_COLUMNS}
    row.update(
        {
            "alpha": alpha,
            "train_count": train_count,
            "case_id": case_id,
            "variant": variant.name,
            "train_ids": csv_ids(train_ids),
            "test_ids": csv_ids(test_ids),
            "solver_status": "FAILED",
            "branch_benders_use_lifted_lower_bounds": bool_text(
                "--use-lifted-lower-bounds" in variant.flags
            ),
            "branch_benders_use_root_user_cuts": bool_text("--use-root-user-cuts" in variant.flags),
            "notes": note,
        }
    )
    return row


def write_csv(path: Path, rows: list[dict[str, Any]], columns: tuple[str, ...]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def close_enough(values: list[float]) -> bool:
    if not values:
        return False
    return max(values) - min(values) <= TOLERANCE


def min_variant(rows: list[dict[str, Any]], field: str) -> str:
    clean = [row for row in rows if row.get("solver_status") == "Optimal"]
    if not clean:
        return ""
    return str(min(clean, key=lambda row: float_or_zero(row.get(field))).get("variant", ""))


def pairwise_row(alpha: float, train_count: int, case_id: int, rows: list[dict[str, Any]]) -> dict[str, Any]:
    by_variant = {str(row["variant"]): row for row in rows}
    ordered = [by_variant[variant.name] for variant in VARIANTS if variant.name in by_variant]
    objectives = [float_or_zero(row.get("objective")) for row in ordered]
    selected = [str(row.get("selected_firebreaks", "")) for row in ordered]
    train_expected = [float_or_zero(row.get("train_expected_burned_area")) for row in ordered]
    train_worst = [float_or_zero(row.get("train_worst_10pct_burned_area")) for row in ordered]
    test_expected = [float_or_zero(row.get("test_expected_burned_area")) for row in ordered]
    test_worst = [float_or_zero(row.get("test_worst_10pct_burned_area")) for row in ordered]
    statuses = [str(row.get("solver_status", "")) for row in ordered]
    violations = [float_or_zero(row.get("branch_benders_max_cut_violation")) for row in ordered]

    notes: list[str] = []
    if len(ordered) != len(VARIANTS):
        notes.append("missing_variant")
    if any(status != "Optimal" for status in statuses):
        notes.append("non_optimal_or_failed")
    if not close_enough(objectives):
        notes.append("objective_mismatch")
    if len(set(selected)) > 1:
        notes.append("selected_firebreak_difference")
    if not close_enough(train_expected) or not close_enough(train_worst):
        notes.append("train_metric_difference")
    if not close_enough(test_expected) or not close_enough(test_worst):
        notes.append("test_metric_difference")
    if any(violation > TOLERANCE for violation in violations):
        notes.append("final_violation_above_tolerance")
    if not notes:
        notes.append("matched")

    return {
        "alpha": alpha,
        "train_count": train_count,
        "case_id": case_id,
        "baseline_objective": float_or_zero(by_variant.get("branch_benders_baseline", {}).get("objective")),
        "llbi_objective": float_or_zero(by_variant.get("branch_benders_llbi", {}).get("objective")),
        "root_cuts_objective": float_or_zero(
            by_variant.get("branch_benders_root_cuts", {}).get("objective")
        ),
        "llbi_root_cuts_objective": float_or_zero(
            by_variant.get("branch_benders_llbi_root_cuts", {}).get("objective")
        ),
        "max_objective_difference": (max(objectives) - min(objectives)) if objectives else 0.0,
        "all_objectives_match": bool_text(close_enough(objectives)),
        "baseline_selected_firebreaks": by_variant.get("branch_benders_baseline", {}).get(
            "selected_firebreaks", ""
        ),
        "llbi_selected_firebreaks": by_variant.get("branch_benders_llbi", {}).get(
            "selected_firebreaks", ""
        ),
        "root_cuts_selected_firebreaks": by_variant.get("branch_benders_root_cuts", {}).get(
            "selected_firebreaks", ""
        ),
        "llbi_root_cuts_selected_firebreaks": by_variant.get(
            "branch_benders_llbi_root_cuts", {}
        ).get("selected_firebreaks", ""),
        "all_selected_firebreaks_match": bool_text(len(set(selected)) == 1),
        "all_train_metrics_match": bool_text(
            close_enough(train_expected) and close_enough(train_worst)
        ),
        "all_test_metrics_match": bool_text(close_enough(test_expected) and close_enough(test_worst)),
        "fastest_variant": min_variant(ordered, "runtime_seconds"),
        "fewest_lazy_cuts_variant": min_variant(ordered, "branch_benders_lazy_cuts_added"),
        "fewest_candidate_checks_variant": min_variant(
            ordered, "branch_benders_candidate_incumbents_checked"
        ),
        "notes": ";".join(notes),
    }


def print_summary(rows: list[dict[str, Any]], pairwise_rows: list[dict[str, Any]]) -> None:
    print("Phase 22 callback strengthening comparison summary")
    print(f"  instances: {len(pairwise_rows)}")
    print(f"  solves: {len(rows)}")
    print(
        "  objective mismatches: "
        f"{sum(row['all_objectives_match'] != 'true' for row in pairwise_rows)}"
    )
    print(
        "  selected firebreak differences: "
        f"{sum(row['all_selected_firebreaks_match'] != 'true' for row in pairwise_rows)}"
    )
    for variant in VARIANTS:
        variant_rows = [
            row
            for row in rows
            if row.get("variant") == variant.name and row.get("solver_status") == "Optimal"
        ]
        if not variant_rows:
            print(f"  {variant.name}: no optimal rows")
            continue
        runtimes = [float_or_zero(row["runtime_seconds"]) for row in variant_rows]
        lazy_cuts = [float_or_zero(row["branch_benders_lazy_cuts_added"]) for row in variant_rows]
        checks = [
            float_or_zero(row["branch_benders_candidate_incumbents_checked"])
            for row in variant_rows
        ]
        root_cuts = [float_or_zero(row["branch_benders_root_user_cuts_added"]) for row in variant_rows]
        fastest = sum(row["fastest_variant"] == variant.name for row in pairwise_rows)
        print(
            f"  {variant.name}: mean_runtime={statistics.mean(runtimes):.6f}, "
            f"median_runtime={statistics.median(runtimes):.6f}, "
            f"mean_lazy_cuts={statistics.mean(lazy_cuts):.3f}, "
            f"mean_candidate_checks={statistics.mean(checks):.3f}, "
            f"mean_root_user_cuts={statistics.mean(root_cuts):.3f}, "
            f"fastest_instances={fastest}"
        )


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir)
    json_dir = output_dir / "json"
    log_dir = output_dir / "logs"
    raw_csv_dir = output_dir / "raw_csv"
    split_dir = output_dir / "splits"
    for directory in (json_dir, log_dir, raw_csv_dir, split_dir):
        directory.mkdir(parents=True, exist_ok=True)

    available_ids = list_scenario_ids(Path(args.results_path))
    rows: list[dict[str, Any]] = []
    rows_by_instance: dict[tuple[float, int, int], list[dict[str, Any]]] = {}
    commands_run = 0
    failures = 0

    for train_count in TRAIN_COUNTS:
        for case_id in range(NUM_CASES):
            seed = SEED_BASE + case_id
            train_ids, test_ids = generated_split(available_ids, seed, train_count)
            (split_dir / f"seed{seed}_train{train_count}_test{TEST_COUNT}_case{case_id}_train.csv").write_text(
                "\n".join(str(value) for value in train_ids) + "\n",
                encoding="utf-8",
            )
            (split_dir / f"seed{seed}_train{train_count}_test{TEST_COUNT}_case{case_id}_test.csv").write_text(
                "\n".join(str(value) for value in test_ids) + "\n",
                encoding="utf-8",
            )
            for alpha in ALPHAS:
                for variant in VARIANTS:
                    if args.max_solves and commands_run >= args.max_solves:
                        continue
                    run_id = run_id_for(variant, alpha, train_count, case_id)
                    json_path = json_dir / f"{run_id}.json"
                    raw_csv_path = raw_csv_dir / f"{run_id}.csv"
                    log_path = log_dir / f"{run_id}.log"
                    command = build_command(
                        args,
                        variant,
                        alpha,
                        train_count,
                        case_id,
                        train_ids,
                        test_ids,
                        json_path,
                        raw_csv_path,
                    )
                    if args.dry_run:
                        print(" ".join(command))
                        continue
                    should_run = not (
                        args.skip_existing and json_path.exists() and json_path.stat().st_size > 0
                    )
                    if should_run:
                        print(f"Running {run_id}", flush=True)
                        completed = subprocess.run(
                            command,
                            text=True,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            check=False,
                        )
                        log_path.write_text(completed.stdout, encoding="utf-8")
                        commands_run += 1
                        if completed.returncode != 0:
                            failures += 1
                            row = failure_row(
                                variant,
                                alpha,
                                train_count,
                                case_id,
                                train_ids,
                                test_ids,
                                f"command_failed_returncode_{completed.returncode}",
                            )
                            rows.append(row)
                            rows_by_instance.setdefault((alpha, train_count, case_id), []).append(row)
                            continue
                    else:
                        print(f"Skipping existing {run_id}", flush=True)

                    try:
                        data = load_json(json_path)
                        row = row_from_json(data, variant, alpha, train_count, case_id)
                    except Exception as exc:  # noqa: BLE001 - script should report all run failures.
                        failures += 1
                        row = failure_row(
                            variant,
                            alpha,
                            train_count,
                            case_id,
                            train_ids,
                            test_ids,
                            f"result_parse_failed_{type(exc).__name__}",
                        )
                    rows.append(row)
                    rows_by_instance.setdefault((alpha, train_count, case_id), []).append(row)

    pairwise_rows = [
        pairwise_row(alpha, train_count, case_id, instance_rows)
        for (alpha, train_count, case_id), instance_rows in sorted(rows_by_instance.items())
    ]
    write_csv(output_dir / "callback_strengthening_comparison.csv", rows, MAIN_COLUMNS)
    write_csv(output_dir / "callback_strengthening_pairwise.csv", pairwise_rows, PAIRWISE_COLUMNS)

    if not args.dry_run:
        print_summary(rows, pairwise_rows)
        print(f"  commands_run_this_invocation: {commands_run}")
        print(f"  failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
