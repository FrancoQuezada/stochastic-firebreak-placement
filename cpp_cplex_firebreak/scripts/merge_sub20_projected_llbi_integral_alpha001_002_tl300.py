#!/usr/bin/env python3
"""Merge Sub20 projected LLBI integral worker CSVs."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_projected_llbi_integral_alpha001_002_tl300")
EXPECTED_WORKERS = 10
EXPECTED_ROWS_PER_WORKER = 24
OUTPUT_NAME = "batch_results_projected_integral_all.csv"

SORT_FIELDS = [
    "case_id",
    "alpha",
    "objective_family",
    "method",
    "projected_variant",
    "projected_llbi_strategy",
    "use_root_cuts",
    "task_id",
]

LOGICAL_KEY = [
    "landscape",
    "case_id",
    "alpha",
    "objective_family",
    "method",
    "projected_variant",
    "projected_llbi_strategy",
    "use_root_cuts",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--expected-workers", type=int, default=EXPECTED_WORKERS)
    parser.add_argument("--expected-rows-per-worker", type=int, default=EXPECTED_ROWS_PER_WORKER)
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


def row_key(row: dict[str, str], fields: list[str]) -> tuple[str, ...]:
    return tuple(row.get(field, "") for field in fields)


def main() -> int:
    args = parse_args()
    expected_total = args.expected_workers * args.expected_rows_per_worker
    report_lines: list[str] = []
    rows: list[dict[str, str]] = []

    for index in range(args.expected_workers):
        worker = f"worker_{index:02d}"
        path = args.results_dir / worker / f"batch_results_{worker}.csv"
        if not path.exists():
            raise SystemExit(f"Missing worker CSV: {path}")
        worker_rows = read_csv(path)
        if len(worker_rows) != args.expected_rows_per_worker:
            raise SystemExit(f"{path} has {len(worker_rows)} rows; expected {args.expected_rows_per_worker}.")
        report_lines.append(f"{worker}: {len(worker_rows)} rows")
        rows.extend(worker_rows)

    if len(rows) != expected_total:
        raise SystemExit(f"Merged row count is {len(rows)}; expected {expected_total}.")

    seen: set[tuple[str, ...]] = set()
    duplicates: list[tuple[str, ...]] = []
    for row in rows:
        logical = row_key(row, LOGICAL_KEY)
        if logical in seen:
            duplicates.append(logical)
        seen.add(logical)
    if duplicates:
        raise SystemExit(f"Duplicate logical rows detected: {duplicates[:5]}")

    rows.sort(key=lambda row: row_key(row, SORT_FIELDS))
    combined_dir = args.results_dir / "combined"
    output_csv = combined_dir / OUTPUT_NAME
    write_csv(output_csv, rows)
    report_lines.extend([
        "",
        f"Expected workers: {args.expected_workers}",
        f"Expected rows per worker: {args.expected_rows_per_worker}",
        f"Merged rows: {len(rows)}",
        f"Output CSV: {output_csv}",
        "Status: PASS",
    ])
    report_path = combined_dir / "merge_report.txt"
    report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print(f"Merged {len(rows)} rows into {output_csv}")
    print(f"Merge report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
