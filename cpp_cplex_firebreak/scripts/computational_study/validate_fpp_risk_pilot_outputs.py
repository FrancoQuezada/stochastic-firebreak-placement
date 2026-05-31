#!/usr/bin/env python3
"""Validate the Phase 1U FPP risk-study pilot outputs."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path


CRITICAL_RUN_COLUMNS = [
    "run_id",
    "method_key",
    "method_family",
    "landscape",
    "alpha",
    "lambda",
    "risk_measure",
    "case_id",
    "split_key",
    "status",
    "objective",
    "runtime_seconds",
    "restricted_heuristic_mode",
    "global_optimality_certified",
    "json_path",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Validate Phase 1U pilot aggregation outputs.")
    parser.add_argument("--output-dir", type=Path, default=Path("results/computational_study_fpp_risk_pilot"))
    parser.add_argument("--expected-command-count", type=int, default=52)
    parser.add_argument("--objective-tolerance", type=float, default=1.0e-4)
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def float_or_none(value: str) -> float | None:
    if value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def group_key(row: dict[str, str]) -> tuple[str, str, str, str, str]:
    return (
        row["landscape"],
        row["alpha"],
        row["lambda"],
        row["case_id"],
        row["split_key"],
    )


def main() -> int:
    args = parse_args()
    root = args.output_dir
    manifest_path = root / "commands" / "full_command_manifest.csv"
    run_summary_path = root / "summaries" / "fpp_risk_run_summary.csv"
    pairwise_path = root / "summaries" / "fpp_risk_pairwise_summary.csv"
    profile_path = root / "summaries" / "fpp_risk_performance_profile_data.csv"
    failures_path = root / "summaries" / "fpp_risk_failures.csv"

    manifest = read_csv(manifest_path)
    run_rows = read_csv(run_summary_path)
    pairwise_rows = read_csv(pairwise_path)
    profile_rows = read_csv(profile_path)
    failure_rows = read_csv(failures_path)
    json_count = len(list((root / "json").glob("*.json")))

    checks: dict[str, bool] = {}
    checks["manifest_count_expected"] = len(manifest) == args.expected_command_count
    checks["run_summary_count_expected"] = len(run_rows) == args.expected_command_count
    checks["json_count_expected"] = json_count == args.expected_command_count
    checks["unique_run_ids"] = len({row["run_id"] for row in manifest}) == len(manifest)
    checks["all_manifest_threads_one"] = {row["threads"] for row in manifest} == {"1"}
    checks["split_files_exist"] = all(Path(row["split_file"]).exists() for row in manifest)

    split_keys_by_case: dict[str, list[str]] = {}
    for case_id in sorted({row["case_id"] for row in manifest}):
        split_keys_by_case[case_id] = sorted({row["split_key"] for row in manifest if row["case_id"] == case_id})
    checks["shared_split_by_case"] = all(len(keys) == 1 for keys in split_keys_by_case.values())

    risk_mapping = Counter((row["lambda"], row["risk_measure"]) for row in run_rows)
    checks["risk_mapping_valid"] = all(
        (row["lambda"], row["risk_measure"]) in {
            ("0", "expected"),
            ("0.5", "mean-cvar"),
            ("1", "cvar"),
        }
        for row in run_rows)
    tailblend_rows = [row for row in run_rows if row["method_key"] == "restricted-tailblend-maintenance"]
    checks["tailblend_only_pure_cvar"] = all(row["lambda"] == "1" and row["risk_measure"] == "cvar" for row in tailblend_rows)
    checks["no_dpv_rows"] = all("dpv" not in row["method_key"].lower() and "DPV" not in row["method_label"] for row in run_rows)
    checks["critical_run_columns_present"] = all(field in run_rows[0] for field in CRITICAL_RUN_COLUMNS) if run_rows else False
    checks["pairwise_rows_exist"] = len(pairwise_rows) > 0
    checks["performance_profile_rows_exist"] = len(profile_rows) == len(run_rows)
    checks["performance_profile_group_fields_present"] = all(
        field in profile_rows[0]
        for field in ["landscape", "alpha", "lambda", "case_id", "split_key", "runtime_ratio_to_best"]
    ) if profile_rows else False

    restricted_rows = [row for row in run_rows if row["method_family"] == "restricted-heuristic"]
    checks["restricted_rows_present"] = len(restricted_rows) == 28
    checks["restricted_rows_not_globally_optimal"] = all(row["global_optimality_certified"] != "true" for row in restricted_rows)
    checks["restricted_rows_marked_heuristic"] = all(row["status"] == "RestrictedHeuristic" for row in restricted_rows)

    by_group: dict[tuple[str, str, str, str, str], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in run_rows:
        by_group[group_key(row)][row["method_key"]] = row
    exact_pairs_checked = 0
    exact_pair_max_objective_diff = 0.0
    exact_pair_issues: list[dict[str, str | float]] = []
    for key, rows_by_method in by_group.items():
        left = rows_by_method.get("fpp-saa")
        right = rows_by_method.get("fpp-bb")
        if not left or not right:
            continue
        if left["status"] == "Optimal" and right["status"] == "Optimal":
            exact_pairs_checked += 1
            left_objective = float_or_none(left["objective"])
            right_objective = float_or_none(right["objective"])
            if left_objective is None or right_objective is None:
                exact_pair_issues.append({"group": "|".join(key), "reason": "missing objective"})
                continue
            diff = abs(left_objective - right_objective)
            exact_pair_max_objective_diff = max(exact_pair_max_objective_diff, diff)
            if diff > args.objective_tolerance:
                exact_pair_issues.append({
                    "group": "|".join(key),
                    "objective_diff": diff,
                    "fpp_saa_objective": left_objective,
                    "fpp_bb_objective": right_objective,
                })
    checks["exact_optimal_pairs_consistent"] = not exact_pair_issues

    status_counts = Counter(row["status"] for row in run_rows)
    status_by_method_risk = Counter((row["method_key"], row["risk_measure"], row["status"]) for row in run_rows)
    feasible_or_time_limited = [
        {
            "run_id": row["run_id"],
            "method_key": row["method_key"],
            "risk_measure": row["risk_measure"],
            "alpha": row["alpha"],
            "case_id": row["case_id"],
            "status": row["status"],
            "runtime_seconds": row["runtime_seconds"],
            "mip_gap": row["mip_gap"],
            "objective": row["objective"],
            "best_bound": row["best_bound"],
        }
        for row in run_rows
        if row["status"] not in {"Optimal", "RestrictedHeuristic"}
    ]
    pairwise_valid_counts = Counter(row["comparison_valid"] for row in pairwise_rows)

    report = {
        "output_dir": str(root),
        "expected_command_count": args.expected_command_count,
        "manifest_rows": len(manifest),
        "json_count": json_count,
        "run_summary_rows": len(run_rows),
        "pairwise_rows": len(pairwise_rows),
        "performance_profile_rows": len(profile_rows),
        "failure_rows": len(failure_rows),
        "checks": checks,
        "status_counts": dict(status_counts),
        "status_by_method_risk": {"|".join(key): value for key, value in sorted(status_by_method_risk.items())},
        "risk_mapping_counts": {"|".join(key): value for key, value in sorted(risk_mapping.items())},
        "split_keys_by_case": split_keys_by_case,
        "tailblend_row_count": len(tailblend_rows),
        "tailblend_lambdas": sorted({row["lambda"] for row in tailblend_rows}),
        "restricted_row_count": len(restricted_rows),
        "exact_pairs_checked": exact_pairs_checked,
        "exact_pair_max_objective_diff": exact_pair_max_objective_diff,
        "exact_pair_issues": exact_pair_issues,
        "feasible_or_time_limited_rows": feasible_or_time_limited,
        "pairwise_valid_counts": dict(pairwise_valid_counts),
    }
    out_path = root / "summaries" / "fpp_risk_pilot_validation.json"
    out_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    failed_checks = [name for name, ok in checks.items() if not ok]
    print(f"Validation report: {out_path}")
    print(f"Manifest rows: {len(manifest)}")
    print(f"JSON outputs: {json_count}")
    print(f"Status counts: {dict(status_counts)}")
    print(f"Feasible/time-limit-like rows: {len(feasible_or_time_limited)}")
    print(f"Exact optimal pairs checked: {exact_pairs_checked}")
    print(f"Exact max objective diff: {exact_pair_max_objective_diff:.8g}")
    print(f"Pairwise valid counts: {dict(pairwise_valid_counts)}")
    if failed_checks:
        print("Failed checks: " + ", ".join(failed_checks))
        return 1
    print("All validation checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
