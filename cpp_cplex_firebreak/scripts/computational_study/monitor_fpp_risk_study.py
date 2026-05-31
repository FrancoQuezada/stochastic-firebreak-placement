#!/usr/bin/env python3
"""Write checkpoint monitoring summaries for the FPP risk study."""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

import fpp_risk_study_config as cfg


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create stage monitoring files for the FPP risk study.")
    parser.add_argument("--output-dir", type=Path, default=cfg.DEFAULT_FULL_OUTPUT_DIR)
    parser.add_argument("--manifest", type=Path, default=None)
    parser.add_argument("--stage-name", required=True)
    parser.add_argument("--objective-tolerance", type=float, default=1.0e-4)
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def float_or_none(value: str) -> float | None:
    if value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def group_key(row: dict[str, str]) -> tuple[str, str, str, str, str, str]:
    return (
        row.get("landscape", ""),
        row.get("alpha", ""),
        row.get("lambda", ""),
        row.get("train_count", ""),
        row.get("case_id", ""),
        row.get("split_key", ""),
    )


def write_status_summary(path: Path, run_rows: list[dict[str, str]]) -> None:
    counts = Counter(row.get("status", "") for row in run_rows)
    time_limit_counts = Counter(
        row.get("status", "") for row in run_rows
        if row.get("time_limit_like") == "true")
    rows = [
        {
            "status": status,
            "count": count,
            "time_limit_like_count": time_limit_counts.get(status, 0),
        }
        for status, count in sorted(counts.items())
    ]
    write_csv(path, ["status", "count", "time_limit_like_count"], rows)


def write_missing_runs(path: Path, manifest_rows: list[dict[str, str]]) -> list[dict[str, str]]:
    missing = [
        row for row in manifest_rows
        if row.get("json_path") and not Path(row["json_path"]).exists()
    ]
    fields = [
        "run_id", "method_key", "method_label", "landscape", "alpha", "lambda",
        "risk_measure", "train_count", "test_count", "case_id", "split_key", "json_path",
    ]
    write_csv(path, fields, missing)
    return missing


def validate_splits(manifest_rows: list[dict[str, str]]) -> dict[str, object]:
    split_keys_by_group: dict[tuple[str, str, str, str], set[str]] = defaultdict(set)
    train_ids_by_group: dict[tuple[str, str, str, str], set[str]] = defaultdict(set)
    test_ids_by_group: dict[tuple[str, str, str, str], set[str]] = defaultdict(set)
    for row in manifest_rows:
        key = (
            row.get("landscape", ""),
            row.get("alpha", ""),
            row.get("train_count", ""),
            row.get("case_id", ""),
        )
        split_keys_by_group[key].add(row.get("split_key", ""))
        train_ids_by_group[key].add(row.get("train_ids", ""))
        test_ids_by_group[key].add(row.get("test_ids", ""))
    split_issues = [
        "|".join(key) for key, values in sorted(split_keys_by_group.items())
        if len(values) != 1
    ]
    train_issues = [
        "|".join(key) for key, values in sorted(train_ids_by_group.items())
        if len(values) != 1
    ]
    test_issues = [
        "|".join(key) for key, values in sorted(test_ids_by_group.items())
        if len(values) != 1
    ]
    return {
        "shared_split_consistent": not split_issues and not train_issues and not test_issues,
        "split_issue_count": len(split_issues) + len(train_issues) + len(test_issues),
        "split_issue_examples": (split_issues + train_issues + test_issues)[:10],
    }


def validate_exact_pairs(run_rows: list[dict[str, str]], tolerance: float) -> dict[str, object]:
    by_group: dict[tuple[str, str, str, str, str, str], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in run_rows:
        by_group[group_key(row)][row.get("method_key", "")] = row
    checked = 0
    max_diff = 0.0
    issues: list[dict[str, object]] = []
    for key, rows_by_method in sorted(by_group.items()):
        left = rows_by_method.get("fpp-saa")
        right = rows_by_method.get("fpp-bb")
        if not left or not right:
            continue
        if left.get("status") != "Optimal" or right.get("status") != "Optimal":
            continue
        left_objective = float_or_none(left.get("objective", ""))
        right_objective = float_or_none(right.get("objective", ""))
        if left_objective is None or right_objective is None:
            issues.append({"group": "|".join(key), "reason": "missing objective"})
            continue
        diff = abs(left_objective - right_objective)
        checked += 1
        max_diff = max(max_diff, diff)
        if diff > tolerance:
            issues.append({
                "group": "|".join(key),
                "objective_diff": diff,
                "fpp_saa_objective": left_objective,
                "fpp_bb_objective": right_objective,
            })
    return {
        "exact_optimal_pairs_checked": checked,
        "exact_optimal_pair_max_objective_diff": max_diff,
        "exact_optimal_pair_issue_count": len(issues),
        "exact_optimal_pair_issues": issues[:25],
    }


def main() -> int:
    args = parse_args()
    layout = cfg.ensure_output_layout(args.output_dir)
    manifest_path = args.manifest or (layout["commands"] / "full_command_manifest.csv")
    manifest_rows = read_csv(manifest_path)
    run_rows = read_csv(layout["summaries"] / "fpp_risk_run_summary.csv")
    method_rows = read_csv(layout["summaries"] / "fpp_risk_method_summary.csv")
    failure_rows = read_csv(layout["summaries"] / "fpp_risk_failures.csv")
    stage = args.stage_name

    monitoring_dir = layout["monitoring"]
    write_status_summary(monitoring_dir / f"stage_{stage}_status_summary.csv", run_rows)
    if method_rows:
        write_csv(
            monitoring_dir / f"stage_{stage}_method_summary.csv",
            list(method_rows[0].keys()),
            method_rows)
    else:
        write_csv(monitoring_dir / f"stage_{stage}_method_summary.csv", ["method_label", "runs"], [])
    if failure_rows:
        write_csv(
            monitoring_dir / f"stage_{stage}_failures.csv",
            list(failure_rows[0].keys()),
            failure_rows)
    else:
        write_csv(monitoring_dir / f"stage_{stage}_failures.csv", ["run_id", "status"], [])
    time_limit_rows = [row for row in run_rows if row.get("time_limit_like") == "true"]
    if run_rows:
        write_csv(
            monitoring_dir / f"stage_{stage}_time_limits.csv",
            list(run_rows[0].keys()),
            time_limit_rows)
    else:
        write_csv(monitoring_dir / f"stage_{stage}_time_limits.csv", ["run_id", "status"], [])
    missing_rows = write_missing_runs(
        monitoring_dir / f"stage_{stage}_missing_runs.csv",
        manifest_rows)

    status_counts = Counter(row.get("status", "") for row in run_rows)
    method_counts = Counter(row.get("method_key", "") for row in run_rows)
    risk_mapping_valid = all(
        (row.get("lambda"), row.get("risk_measure")) in {
            ("0", "expected"),
            ("0.5", "mean-cvar"),
            ("1", "cvar"),
        }
        for row in run_rows)
    tailblend_only_cvar = all(
        row.get("lambda") == "1" and row.get("risk_measure") == "cvar"
        for row in run_rows
        if row.get("method_key") == "restricted-tailblend-maintenance")
    validation = {
        "stage_name": stage,
        "output_dir": str(args.output_dir),
        "manifest_rows": len(manifest_rows),
        "json_outputs": len(list(layout["json"].glob("*.json"))),
        "run_summary_rows": len(run_rows),
        "missing_runs": len(missing_rows),
        "failure_rows": len(failure_rows),
        "time_limit_like_rows": len(time_limit_rows),
        "status_counts": dict(status_counts),
        "method_counts": dict(method_counts),
        "all_manifest_threads_one": {row.get("threads", "") for row in manifest_rows} <= {"1"},
        "all_manifest_time_limit_1800": {row.get("time_limit", "") for row in manifest_rows} <= {"1800"},
        "risk_mapping_valid": risk_mapping_valid,
        "tailblend_only_pure_cvar": tailblend_only_cvar,
        "no_dpv_rows": all(
            "dpv" not in row.get("method_key", "").lower()
            and "dpv" not in row.get("method_label", "").lower()
            for row in manifest_rows),
        **validate_splits(manifest_rows),
        **validate_exact_pairs(run_rows, args.objective_tolerance),
    }
    validation_path = monitoring_dir / f"stage_{stage}_validation.json"
    validation_path.write_text(json.dumps(validation, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"Monitoring stage: {stage}")
    print(f"Manifest rows: {len(manifest_rows)}")
    print(f"JSON outputs: {validation['json_outputs']}")
    print(f"Run summary rows: {len(run_rows)}")
    print(f"Missing runs: {len(missing_rows)}")
    print(f"Failures: {len(failure_rows)}")
    print(f"Time-limit-like rows: {len(time_limit_rows)}")
    print(f"Validation: {validation_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
