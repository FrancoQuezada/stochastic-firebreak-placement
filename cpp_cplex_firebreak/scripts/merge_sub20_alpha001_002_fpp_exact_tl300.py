#!/usr/bin/env python3
"""Merge the 10 Sub20 alpha 0.01/0.02 FPP exact-study worker CSVs."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")
EXPECTED_WORKERS = 10
EXPECTED_ROWS_PER_WORKER = 45
EXPECTED_ROWS = EXPECTED_WORKERS * EXPECTED_ROWS_PER_WORKER

SORT_FIELDS = [
    "case_id",
    "alpha",
    "objective_family",
    "method_family",
    "method",
    "fpp_mode",
    "lb_config",
    "use_root_cuts",
    "is_combinatorial",
    "task_id",
]

LOGICAL_KEY = [
    "landscape",
    "case_id",
    "alpha",
    "objective_family",
    "method_family",
    "method",
    "fpp_mode",
    "lb_config",
    "use_root_cuts",
    "is_combinatorial",
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


def key(row: dict[str, str], fields: list[str]) -> tuple[str, ...]:
    return tuple(row.get(field, "") for field in fields)


def main() -> int:
    args = parse_args()
    expected_rows = args.expected_workers * args.expected_rows_per_worker
    report_lines: list[str] = []
    all_rows: list[dict[str, str]] = []

    for index in range(args.expected_workers):
        worker = f"worker_{index:02d}"
        path = args.results_dir / worker / f"batch_results_{worker}.csv"
        if not path.exists():
            raise SystemExit(f"Missing worker CSV: {path}")
        rows = read_csv(path)
        if len(rows) != args.expected_rows_per_worker:
            raise SystemExit(
                f"{path} has {len(rows)} data rows; expected {args.expected_rows_per_worker}.")
        report_lines.append(f"{worker}: {len(rows)} rows")
        all_rows.extend(rows)

    if len(all_rows) != expected_rows:
        raise SystemExit(f"Merged row count is {len(all_rows)}; expected {expected_rows}.")

    seen: set[tuple[str, ...]] = set()
    duplicates: list[tuple[str, ...]] = []
    for row in all_rows:
        logical = key(row, LOGICAL_KEY)
        if logical in seen:
            duplicates.append(logical)
        seen.add(logical)
    if duplicates:
        raise SystemExit(f"Duplicate logical rows detected: {duplicates[:5]}")

    all_rows.sort(key=lambda row: key(row, SORT_FIELDS))
    combined_dir = args.results_dir / "combined"
    combined_csv = combined_dir / "batch_results_all.csv"
    write_csv(combined_csv, all_rows)

    report_lines.extend([
        "",
        f"Expected workers: {args.expected_workers}",
        f"Expected rows per worker: {args.expected_rows_per_worker}",
        f"Merged rows: {len(all_rows)}",
        f"Output CSV: {combined_csv}",
        "Status: PASS",
    ])
    report_path = combined_dir / "merge_report.txt"
    report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print(f"Merged {len(all_rows)} rows into {combined_csv}")
    print(f"Merge report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
