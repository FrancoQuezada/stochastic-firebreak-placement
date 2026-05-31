#!/usr/bin/env python3
"""Validate completed Sub20 projected LLBI integral experiment outputs."""

from __future__ import annotations

import argparse
import csv
import math
import re
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_projected_llbi_integral_alpha001_002_tl300")
DEFAULT_PREVIOUS_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")
PROJECTED_OUTPUT_NAME = "batch_results_projected_integral_all.csv"
PREVIOUS_OUTPUT_NAME = "batch_results_all.csv"

ALPHAS = ["0.01", "0.02"]
CASES = [f"case{i:02d}" for i in range(5)]
OBJECTIVES = [
    ("Expected", ""),
    ("CVaR", "-CVaR"),
    ("MeanCVaR", "-MeanCVaR"),
]
PROJECTED_VARIANTS = [
    ("ProjectedCoverageLLBI-poly", "poly"),
    ("ProjectedPathLLBI-poly", "poly"),
    ("ProjectedCoverageLLBI-exp", "exp"),
    ("ProjectedPathLLBI-exp", "exp"),
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

VARIANT_KEY = [
    "objective_family",
    "method",
    "projected_variant",
    "projected_llbi_strategy",
    "use_root_cuts",
]

ESSENTIAL_COLUMNS = [
    "task_id",
    "worker_id",
    "case_id",
    "seed_base",
    "seed",
    "landscape",
    "alpha",
    "train_count",
    "test_count",
    "objective_family",
    "method",
    "method_family",
    "projected_variant",
    "projected_llbi_strategy",
    "risk_measure",
    "use_root_cuts",
    "threads",
    "split_dir",
    "train_ids",
    "test_ids",
]

PROJECTED_DIAGNOSTIC_COLUMNS = [
    "projected_coverage_llbi_enabled",
    "projected_path_llbi_enabled",
    "projected_llbi_strategy",
    "projected_llbi_root_rounds",
    "projected_llbi_cuts_added",
    "projected_llbi_coverage_cuts_added",
    "projected_llbi_path_cuts_added",
    "projected_llbi_separation_time_sec",
    "projected_llbi_solve_time_sec",
    "projected_llbi_total_time_sec",
    "projected_llbi_total_nonzeros",
    "projected_llbi_avg_nonzeros_per_cut",
    "projected_llbi_max_nonzeros_per_cut",
    "projected_llbi_root_bound_initial",
    "projected_llbi_root_bound_final",
    "projected_llbi_root_bound_improvement_abs",
    "projected_llbi_root_bound_improvement_pct",
    "coverage_llbi_num_zeta_vars",
    "path_llbi_num_b_vars",
]

OPTIONAL_COLUMNS = [
    "objective_value",
    "objective_in_sample",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "runtime_sec",
    "explored_nodes",
    "branch_benders_lazy_cuts_added",
    "branch_benders_candidate_incumbents_checked",
    "num_variables",
    "num_constraints",
    "num_nonzeros",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--previous-results-dir", type=Path, default=DEFAULT_PREVIOUS_RESULTS_DIR)
    parser.add_argument("--expected-rows", type=int, default=240)
    parser.add_argument("--expected-workers", type=int, default=10)
    parser.add_argument("--expected-rows-per-worker", type=int, default=24)
    parser.add_argument("--seed-base", type=int, default=20260601)
    return parser.parse_args()


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"Missing CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def key(row: dict[str, str], fields: list[str]) -> tuple[str, ...]:
    return tuple(row.get(field, "") for field in fields)


def normalize_ids(value: str) -> str:
    ids = [int(token) for token in re.split(r"[\s,;]+", value.strip()) if token]
    return ",".join(str(value) for value in ids)


def parse_float(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        result = float(value)
    except ValueError:
        return None
    return result if math.isfinite(result) else None


def expected_variants() -> set[tuple[str, ...]]:
    variants: set[tuple[str, ...]] = set()
    for objective, risk_suffix in OBJECTIVES:
        prefix = "FPP-Branch-Benders" + risk_suffix
        for projected_variant, strategy in PROJECTED_VARIANTS:
            for root_cuts in (False, True):
                variants.add((
                    objective,
                    prefix + "-" + projected_variant + ("-RootCuts" if root_cuts else ""),
                    projected_variant,
                    strategy,
                    bool_text(root_cuts),
                ))
    return variants


def previous_split_pairs(previous_rows: list[dict[str, str]]) -> tuple[dict[str, tuple[str, str]], set[str]]:
    by_case: dict[str, set[tuple[str, str]]] = defaultdict(set)
    split_dirs: set[str] = set()
    for row in previous_rows:
        case = row.get("case_id", "")
        if not case:
            continue
        if row.get("train_ids") and row.get("test_ids"):
            by_case[case].add((normalize_ids(row["train_ids"]), normalize_ids(row["test_ids"])))
        if row.get("split_dir"):
            split_dirs.add(row["split_dir"])
    fixed: dict[str, tuple[str, str]] = {}
    for case, pairs in by_case.items():
        if len(pairs) == 1:
            fixed[case] = next(iter(pairs))
    return fixed, split_dirs


def write_reports(
    *,
    report_path: Path,
    missing_path: Path,
    errors: list[str],
    warnings: list[str],
    missing_columns: list[str],
    row_count: int,
) -> None:
    report_path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "Sub20 projected LLBI integral validation",
        f"Rows: {row_count}",
        f"Errors: {len(errors)}",
        f"Warnings: {len(warnings)}",
        "",
    ]
    if errors:
        lines.append("ERRORS")
        lines.extend(f"- {item}" for item in errors)
        lines.append("")
    if warnings:
        lines.append("WARNINGS")
        lines.extend(f"- {item}" for item in warnings)
        lines.append("")
    lines.append("Status: " + ("PASS" if not errors else "FAIL"))
    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    missing_path.write_text("\n".join(missing_columns) + ("\n" if missing_columns else ""), encoding="utf-8")


def main() -> int:
    args = parse_args()
    combined_dir = args.results_dir / "combined"
    projected_csv = combined_dir / PROJECTED_OUTPUT_NAME
    previous_csv = args.previous_results_dir / "combined" / PREVIOUS_OUTPUT_NAME
    report_path = combined_dir / "validation_report.txt"
    missing_path = combined_dir / "missing_columns_report.txt"

    rows = read_rows(projected_csv)
    previous_rows = read_rows(previous_csv)
    errors: list[str] = []
    warnings: list[str] = []
    header = set(rows[0].keys()) if rows else set()
    missing_columns = [
        column for column in ESSENTIAL_COLUMNS + PROJECTED_DIAGNOSTIC_COLUMNS + OPTIONAL_COLUMNS
        if column not in header
    ]

    for column in ESSENTIAL_COLUMNS + PROJECTED_DIAGNOSTIC_COLUMNS:
        if column not in header:
            errors.append(f"Missing essential column: {column}")

    if len(rows) != args.expected_rows:
        errors.append(f"Total row count is {len(rows)}; expected {args.expected_rows}.")

    worker_counts = Counter(row.get("worker_id", "") for row in rows)
    for index in range(args.expected_workers):
        worker = f"worker_{index:02d}"
        if worker_counts[worker] != args.expected_rows_per_worker:
            errors.append(
                f"{worker} contributed {worker_counts[worker]} rows; expected {args.expected_rows_per_worker}.")

    logical_counts = Counter(key(row, LOGICAL_KEY) for row in rows)
    duplicates = [logical for logical, count in logical_counts.items() if count > 1]
    if duplicates:
        errors.append(f"Duplicate logical rows detected: {duplicates[:5]}")

    expected_blocks = {(case, alpha) for case in CASES for alpha in ALPHAS}
    expected = expected_variants()
    observed = {key(row, VARIANT_KEY) for row in rows}
    missing_variants = sorted(expected - observed)
    unexpected_variants = sorted(observed - expected)
    if missing_variants:
        errors.append(f"Missing expected projected variants: {missing_variants[:10]}")
    if unexpected_variants:
        errors.append(f"Unexpected projected variants present: {unexpected_variants[:10]}")

    blocks_by_variant: dict[tuple[str, ...], set[tuple[str, str]]] = defaultdict(set)
    for row in rows:
        blocks_by_variant[key(row, VARIANT_KEY)].add((row.get("case_id", ""), row.get("alpha", "")))
    for variant, blocks in sorted(blocks_by_variant.items()):
        if blocks != expected_blocks:
            errors.append(f"Variant {variant} appears on {len(blocks)} blocks; expected all 10 blocks.")

    previous_pairs, previous_split_dirs = previous_split_pairs(previous_rows)
    if set(previous_pairs) != set(CASES):
        errors.append("Previous results do not provide one comparable train/test split pair for every case.")

    ids_by_case: dict[str, set[tuple[str, str]]] = defaultdict(set)
    for row in rows:
        task = row.get("task_id", "<missing task_id>")
        case = row.get("case_id", "")
        try:
            pair = (normalize_ids(row.get("train_ids", "")), normalize_ids(row.get("test_ids", "")))
            ids_by_case[case].add(pair)
            if case in previous_pairs and pair != previous_pairs[case]:
                errors.append(f"{task}: train/test split does not match previous experiment for {case}.")
        except ValueError as exc:
            errors.append(f"{task}: invalid train/test ID list: {exc}")

    for case in CASES:
        pairs = ids_by_case.get(case, set())
        if len(pairs) != 1:
            errors.append(f"{case} has {len(pairs)} distinct train/test split pairs across alphas/variants.")

    expected_split_dir = str(args.previous_results_dir / "splits")
    compatible_split_dirs = {expected_split_dir, str(Path(expected_split_dir))}
    compatible_split_dirs.update(previous_split_dirs)
    for row in rows:
        task = row.get("task_id", "<missing task_id>")
        if row.get("split_dir") not in compatible_split_dirs:
            errors.append(
                f"{task}: split_dir={row.get('split_dir')} does not match previous split dir {expected_split_dir}.")
        if row.get("train_count") not in {"100", "100.0"}:
            errors.append(f"{task}: train_count={row.get('train_count')} but expected 100.")
        if row.get("test_count") not in {"900", "900.0"}:
            errors.append(f"{task}: test_count={row.get('test_count')} but expected 900.")
        if row.get("seed_base") != str(args.seed_base):
            errors.append(f"{task}: seed_base={row.get('seed_base')} but expected {args.seed_base}.")
        if row.get("threads") not in {"1", "1.0"}:
            errors.append(f"{task}: threads={row.get('threads')} but expected 1.")
        if row.get("worker_return_code", "0") not in {"", "0"}:
            errors.append(f"{task}: worker_return_code={row.get('worker_return_code')}.")

        method = row.get("method", "")
        strategy = row.get("projected_llbi_strategy", "")
        if "ProjectedCoverageLLBI-poly" in method or "ProjectedPathLLBI-poly" in method:
            if strategy != "poly":
                errors.append(f"{task}: {method} reports projected_llbi_strategy={strategy}; expected poly.")
        if "ProjectedCoverageLLBI-exp" in method or "ProjectedPathLLBI-exp" in method:
            if strategy != "exp":
                errors.append(f"{task}: {method} reports projected_llbi_strategy={strategy}; expected exp.")
            if row.get("projected_llbi_root_rounds") not in {"10", "10.0"}:
                errors.append(
                    f"{task}: exp variant has projected_llbi_root_rounds={row.get('projected_llbi_root_rounds')}; "
                    "expected 10.")

        zeta = parse_float(row.get("coverage_llbi_num_zeta_vars"))
        bvars = parse_float(row.get("path_llbi_num_b_vars"))
        if zeta is None:
            errors.append(f"{task}: coverage_llbi_num_zeta_vars is missing or nonnumeric.")
        elif abs(zeta) > 1e-9:
            errors.append(f"{task}: projected variant created {zeta:g} zeta variables.")
        if bvars is None:
            errors.append(f"{task}: path_llbi_num_b_vars is missing or nonnumeric.")
        elif abs(bvars) > 1e-9:
            errors.append(f"{task}: projected variant created {bvars:g} b variables.")

        for column in PROJECTED_DIAGNOSTIC_COLUMNS:
            if column in header and row.get(column, "") == "":
                warnings.append(f"{task}: projected diagnostic column {column} is blank.")

    write_reports(
        report_path=report_path,
        missing_path=missing_path,
        errors=errors,
        warnings=warnings,
        missing_columns=missing_columns,
        row_count=len(rows),
    )
    print(f"Validation report: {report_path}")
    print(f"Missing columns report: {missing_path}")
    if errors:
        for error in errors[:20]:
            print(f"ERROR: {error}")
        raise SystemExit(1)
    print("Projected LLBI integral validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
