#!/usr/bin/env python3
"""Run the Phase 19 DPV exact-method comparison on small Sub20 splits."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ALPHAS = (0.01, 0.02)
TRAIN_COUNTS = (2, 5)
TEST_COUNT = 10
NUM_CASES = 3
SEED_BASE = 18000
TIME_LIMIT = 120
MIP_GAP = 0.001
THREADS = 1
TOLERANCE = 1.0e-6


@dataclass(frozen=True)
class MethodSpec:
    name: str
    command: str


METHODS = (
    MethodSpec("DPV-SAA", "run-dpv-saa-oos"),
    MethodSpec("DPV-Benders", "run-dpv-benders-oos"),
    MethodSpec("DPV-Branch-Benders", "run-dpv-branch-benders-oos"),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run Phase 19 DPV-SAA/Benders/Branch-Benders validation."
    )
    parser.add_argument("--exe", default="build_gpp/firebreak_cpp")
    parser.add_argument("--landscape", default="Sub20")
    parser.add_argument("--forest-path", default="../sample_test/data/CanadianFBP/Sub20")
    parser.add_argument("--results-path", default="../sample_test/Sub20")
    parser.add_argument("--output-dir", default="results/phase19_validation")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-existing", action="store_true")
    return parser.parse_args()


def alpha_tag(alpha: float) -> str:
    return f"alpha{int(round(alpha * 1000)):03d}"


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


def build_command(
    args: argparse.Namespace,
    method: MethodSpec,
    alpha: float,
    train_count: int,
    case_id: int,
    json_path: Path,
    raw_csv_path: Path,
) -> list[str]:
    seed = SEED_BASE + case_id
    run_id = (
        f"phase19_{method.name.lower().replace('-', '_')}_"
        f"{alpha_tag(alpha)}_train{train_count}_case{case_id}"
    )
    command = [
        args.exe,
        method.command,
        "--landscape",
        args.landscape,
        "--forest-path",
        args.forest_path,
        "--results-path",
        args.results_path,
        "--seed",
        str(seed),
        "--train-count",
        str(train_count),
        "--test-count",
        str(TEST_COUNT),
        "--alpha",
        str(alpha),
        "--time-limit",
        str(TIME_LIMIT),
        "--mip-gap",
        str(MIP_GAP),
        "--threads",
        str(THREADS),
        "--run-id",
        run_id,
        "--output-json",
        str(json_path),
        "--output-csv",
        str(raw_csv_path),
    ]
    if method.command == "run-dpv-benders-oos":
        command.extend(["--max-iterations", "100", "--tolerance", str(TOLERANCE)])
    elif method.command == "run-dpv-branch-benders-oos":
        command.extend(["--tolerance", str(TOLERANCE)])
    return command


def load_result(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def method_row(
    data: dict[str, Any],
    method: MethodSpec,
    alpha: float,
    train_count: int,
    case_id: int,
) -> dict[str, Any]:
    benders = data.get("benders", {})
    branch = data.get("branch_benders", {})
    if method.name == "DPV-Benders":
        iterations = benders.get("iterations", data.get("solver_iterations", 0))
        cuts_added = benders.get("cuts_added", data.get("cuts_added", 0))
        final_violation = benders.get("final_max_cut_violation", data.get("max_cut_violation", 0.0))
    elif method.name == "DPV-Branch-Benders":
        iterations = 0
        cuts_added = 0
        final_violation = branch.get("max_cut_violation", data.get("max_cut_violation", 0.0))
    else:
        iterations = 0
        cuts_added = 0
        final_violation = data.get("max_cut_violation", 0.0)

    return {
        "alpha": alpha,
        "train_count": train_count,
        "case_id": case_id,
        "method": method.name,
        "objective": float_or_zero(data.get("objective_in_sample")),
        "best_bound": float_or_zero(data.get("best_bound")),
        "mip_gap": float_or_zero(data.get("mip_gap")),
        "runtime_seconds": float_or_zero(data.get("runtime_seconds")),
        "selected_firebreaks": selected_firebreaks_text(data.get("selected_firebreaks")),
        "train_expected_burned_area": float_or_zero(data.get("train_expected_burned_area")),
        "train_worst_10pct_burned_area": float_or_zero(data.get("train_worst_10pct_burned_area")),
        "test_expected_burned_area": float_or_zero(data.get("test_expected_burned_area")),
        "test_worst_10pct_burned_area": float_or_zero(data.get("test_worst_10pct_burned_area")),
        "solver_status": data.get("solver_status", ""),
        "benders_iterations": int_or_zero(iterations),
        "benders_cuts_added": int_or_zero(cuts_added),
        "benders_largest_intermediate_violation": float_or_zero(
            benders.get("largest_intermediate_cut_violation")
        ),
        "branch_benders_candidate_incumbents_checked": int_or_zero(
            branch.get("candidate_incumbents_checked")
        ),
        "branch_benders_lazy_cuts_added": int_or_zero(branch.get("lazy_cuts_added")),
        "branch_benders_callback_time_sec": float_or_zero(branch.get("callback_time_sec")),
        "branch_benders_subproblem_time_sec": float_or_zero(branch.get("subproblem_time_sec")),
        "final_max_cut_violation": float_or_zero(final_violation),
        "comparison_status": "pending",
    }


def abs_diff(row_a: dict[str, Any], row_b: dict[str, Any], field: str) -> float:
    return abs(float(row_a[field]) - float(row_b[field]))


def pairwise_row(
    alpha: float,
    train_count: int,
    case_id: int,
    rows_by_method: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    monolithic = rows_by_method["DPV-SAA"]
    explicit = rows_by_method["DPV-Benders"]
    callback = rows_by_method["DPV-Branch-Benders"]

    objective_diffs = {
        "monolithic_vs_explicit_benders_objective_diff": abs_diff(monolithic, explicit, "objective"),
        "monolithic_vs_callback_objective_diff": abs_diff(monolithic, callback, "objective"),
        "explicit_benders_vs_callback_objective_diff": abs_diff(explicit, callback, "objective"),
    }
    train_burned_fields = (
        "train_expected_burned_area",
        "train_worst_10pct_burned_area",
    )
    test_burned_fields = (
        "test_expected_burned_area",
        "test_worst_10pct_burned_area",
    )
    objective_match = all(value <= TOLERANCE for value in objective_diffs.values())
    train_metric_match = all(
        abs_diff(monolithic, explicit, field) <= TOLERANCE
        and abs_diff(monolithic, callback, field) <= TOLERANCE
        for field in train_burned_fields
    )
    test_metric_match = all(
        abs_diff(monolithic, explicit, field) <= TOLERANCE
        and abs_diff(monolithic, callback, field) <= TOLERANCE
        for field in test_burned_fields
    )
    metric_match = train_metric_match and test_metric_match
    selected_values = {
        monolithic["selected_firebreaks"],
        explicit["selected_firebreaks"],
        callback["selected_firebreaks"],
    }
    selected_match = len(selected_values) == 1
    statuses = {
        monolithic["solver_status"],
        explicit["solver_status"],
        callback["solver_status"],
    }
    clean_status = statuses == {"Optimal"}

    notes = []
    if not selected_match and objective_match and metric_match:
        notes.append("possible_alternative_optimum")
    if not selected_match and objective_match and train_metric_match and not test_metric_match:
        notes.append("alternative_optimum_with_test_metric_difference")
    if not clean_status:
        notes.append("non_optimal_status")
    if not objective_match:
        notes.append("objective_mismatch")
    if not metric_match:
        notes.append("burned_metric_mismatch")
    if not notes:
        notes.append("matched")

    comparison_status = "MATCH" if objective_match and metric_match and clean_status else "CHECK"
    if (
        objective_match
        and train_metric_match
        and not selected_match
        and not test_metric_match
        and clean_status
    ):
        comparison_status = "OBJECTIVE_MATCH_ALT_SELECTION_TEST_METRIC_DIFF"
    elif not selected_match and comparison_status == "MATCH":
        comparison_status = "MATCH_ALT_SELECTION"

    return {
        "alpha": alpha,
        "train_count": train_count,
        "case_id": case_id,
        **objective_diffs,
        "selected_firebreak_match_flag": int(selected_match),
        "train_metric_match_flag": int(train_metric_match),
        "test_metric_match_flag": int(test_metric_match),
        "metric_match_flag": int(metric_match),
        "comparison_status": comparison_status,
        "notes": ";".join(notes),
    }


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir)
    json_dir = output_dir / "json"
    log_dir = output_dir / "logs"
    json_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    method_rows: list[dict[str, Any]] = []
    pairwise_rows: list[dict[str, Any]] = []
    raw_csv_path = output_dir / "raw_command_results.csv"

    for alpha in ALPHAS:
        for train_count in TRAIN_COUNTS:
            for case_id in range(NUM_CASES):
                rows_by_method: dict[str, dict[str, Any]] = {}
                for method in METHODS:
                    run_id = (
                        f"phase19_{method.name.lower().replace('-', '_')}_"
                        f"{alpha_tag(alpha)}_train{train_count}_case{case_id}"
                    )
                    json_path = json_dir / f"{run_id}.json"
                    log_path = log_dir / f"{run_id}.log"
                    command = build_command(args, method, alpha, train_count, case_id, json_path, raw_csv_path)
                    print("Running:", " ".join(command), flush=True)
                    if args.dry_run:
                        continue
                    if args.skip_existing and json_path.exists():
                        data = load_result(json_path)
                    else:
                        completed = subprocess.run(
                            command,
                            text=True,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            check=False,
                        )
                        log_path.write_text(completed.stdout, encoding="utf-8")
                        if completed.returncode != 0:
                            sys.stderr.write(completed.stdout)
                            return completed.returncode
                        data = load_result(json_path)
                    row = method_row(data, method, alpha, train_count, case_id)
                    rows_by_method[method.name] = row
                    method_rows.append(row)

                if not args.dry_run:
                    pair = pairwise_row(alpha, train_count, case_id, rows_by_method)
                    pairwise_rows.append(pair)
                    for method_name, row in rows_by_method.items():
                        if pair["comparison_status"] == "MATCH":
                            row["comparison_status"] = "MATCH"
                        elif pair["comparison_status"] == "MATCH_ALT_SELECTION":
                            row["comparison_status"] = "MATCH_ALT_SELECTION"
                        elif pair["comparison_status"] == "OBJECTIVE_MATCH_ALT_SELECTION_TEST_METRIC_DIFF":
                            row["comparison_status"] = "OBJECTIVE_MATCH_ALT_SELECTION_TEST_METRIC_DIFF"
                        else:
                            row["comparison_status"] = f"CHECK:{pair['notes']}"

    if args.dry_run:
        return 0

    write_csv(output_dir / "dpv_benders_comparison.csv", method_rows)
    write_csv(output_dir / "dpv_benders_pairwise_comparison.csv", pairwise_rows)

    accepted_alt = [
        row for row in pairwise_rows
        if row["comparison_status"] == "OBJECTIVE_MATCH_ALT_SELECTION_TEST_METRIC_DIFF"
    ]
    mismatches = [
        row for row in pairwise_rows
        if not (
            row["comparison_status"].startswith("MATCH")
            or row["comparison_status"] == "OBJECTIVE_MATCH_ALT_SELECTION_TEST_METRIC_DIFF"
        )
    ]
    print(
        f"Wrote {len(method_rows)} method rows and {len(pairwise_rows)} pairwise rows to {output_dir}."
    )
    if mismatches:
        print(f"Validation completed with {len(mismatches)} comparison rows requiring review.")
    elif accepted_alt:
        print(
            "All Phase 19 objectives and train burned-area metrics matched within tolerance; "
            f"{len(accepted_alt)} row had an alternative selected solution with different test metrics."
        )
    else:
        print("All Phase 19 objective and burned-area comparisons matched within tolerance.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
