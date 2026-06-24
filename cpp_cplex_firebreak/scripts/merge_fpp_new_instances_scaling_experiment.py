#!/usr/bin/env python3
"""Merge worker CSVs for the FPP new_instances scaling experiment."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=Path("results/batch/fpp_new_instances_scaling"))
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="Merge successful rows from completed worker CSVs even if some expected workers are missing or incomplete.",
    )
    parser.add_argument(
        "--output-csv",
        type=Path,
        default=None,
        help="Optional combined CSV path. Defaults to batch_results_all.csv, or batch_results_partial.csv with --allow-partial.",
    )
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields: list[str] = []
    for row in rows:
        for field in row:
            if field not in fields:
                fields.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def as_float(value: str) -> float:
    try:
        return float(value)
    except ValueError:
        return 0.0


def as_int(value: str) -> int:
    try:
        return int(float(value))
    except ValueError:
        return 0


def sort_key(row: dict[str, str]) -> tuple:
    return (
        row.get("instance_id", ""),
        as_int(row.get("train_count", row.get("train_scenario_count", "0"))),
        as_float(row.get("alpha", "0")),
        row.get("case_id", ""),
        row.get("objective_family", ""),
        row.get("method_family", ""),
        row.get("method", ""),
        row.get("task_id", ""),
    )


def logical_key(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row.get("instance_id", ""),
        row.get("landscape", ""),
        row.get("alpha", ""),
        row.get("train_count", row.get("train_scenario_count", "")),
        row.get("test_count", row.get("test_scenario_count", "")),
        row.get("case_id", ""),
        row.get("objective_family", ""),
        row.get("method", ""),
        row.get("fpp_mode", ""),
    )


def main() -> int:
    args = parse_args()
    manifest_dir = args.results_dir / "manifests"
    full_manifest = manifest_dir / "full_task_manifest.csv"
    if not full_manifest.exists():
        raise SystemExit(f"Missing manifest: {full_manifest}")
    manifest_rows = read_csv(full_manifest)
    expected_total = len(manifest_rows)
    expected_by_worker = Counter(row["worker_id"] for row in manifest_rows)
    expected_methods = len({row.get("method", "") for row in manifest_rows})

    merged: list[dict[str, str]] = []
    report: list[str] = []
    missing_workers: list[str] = []
    incomplete_workers: list[str] = []
    failed_rows = 0
    for worker_id in sorted(expected_by_worker):
        worker_csv = args.results_dir / "workers" / worker_id / f"batch_results_{worker_id}.csv"
        if not worker_csv.exists():
            if args.allow_partial:
                missing_workers.append(worker_id)
                report.append(f"{worker_id}: missing")
                continue
            raise SystemExit(f"Missing worker CSV: {worker_csv}")
        rows = read_csv(worker_csv)
        expected = expected_by_worker[worker_id]
        if expected != expected_methods:
            raise SystemExit(
                f"{worker_id} has {expected} expected rows; expected one full method panel of {expected_methods}.")
        complete_rows = [row for row in rows if row.get("worker_return_code") == "0"]
        complete = len(complete_rows)
        failed_rows += len(rows) - complete
        if len(rows) != expected or complete != expected:
            if args.allow_partial:
                incomplete_workers.append(worker_id)
                report.append(f"{worker_id}: {complete}/{expected} successful rows")
                merged.extend(complete_rows)
                continue
            if len(rows) != expected:
                raise SystemExit(f"{worker_csv} has {len(rows)} rows; expected {expected}.")
            raise SystemExit(f"{worker_csv} has {complete}/{expected} successful rows.")
        report.append(f"{worker_id}: {len(rows)} rows")
        merged.extend(complete_rows)

    if len(merged) != expected_total and not args.allow_partial:
        raise SystemExit(f"Merged {len(merged)} rows; expected {expected_total}.")
    counts = Counter(logical_key(row) for row in merged)
    duplicates = [key for key, count in counts.items() if count > 1]
    if duplicates:
        raise SystemExit(f"Duplicate logical rows found: {len(duplicates)}")

    merged.sort(key=sort_key)
    if args.output_csv is not None:
        combined_csv = args.output_csv
    elif args.allow_partial:
        combined_csv = args.results_dir / "batch_results_partial.csv"
    else:
        combined_csv = args.results_dir / "batch_results_all.csv"
    write_csv(combined_csv, merged)
    report_path = args.results_dir / ("merge_report_partial.txt" if args.allow_partial else "merge_report.txt")
    report_path.write_text(
        "\n".join([
            f"merged_rows={len(merged)}",
            f"expected_rows={expected_total}",
            f"workers_expected={len(expected_by_worker)}",
            f"workers_merged={len(expected_by_worker) - len(missing_workers)}",
            f"missing_workers={len(missing_workers)}",
            f"incomplete_workers={len(incomplete_workers)}",
            f"failed_or_unmerged_rows={failed_rows}",
            f"methods_per_worker={expected_methods}",
            f"allow_partial={str(args.allow_partial).lower()}",
            f"combined_csv={combined_csv}",
            "",
            *report,
            "",
        ]),
        encoding="utf-8",
    )
    print(f"Merged {len(merged)} rows into {combined_csv}")
    if args.allow_partial:
        print(f"Partial merge report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
