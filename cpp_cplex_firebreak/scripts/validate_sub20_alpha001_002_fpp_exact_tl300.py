#!/usr/bin/env python3
"""Validate completed Sub20 alpha 0.01/0.02 FPP exact-study outputs."""

from __future__ import annotations

import argparse
import csv
import re
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")
ALPHAS = ["0.01", "0.02"]
CASES = [f"case{i:02d}" for i in range(5)]
OBJECTIVES = [
    ("Expected", "expected", "", "1.0"),
    ("CVaR", "cvar", "-CVaR", "1.0"),
    ("MeanCVaR", "mean-cvar", "-MeanCVaR", "0.5"),
]
LB_CONFIGS = [
    ("None", "", False, False, False),
    ("LLBI", "-LLBI", True, False, False),
    ("CoverageLLBI", "-CoverageLLBI", False, True, False),
    ("PathLLBI", "-PathLLBI", False, False, True),
    ("CoverageLLBI+PathLLBI", "-CoverageLLBI-PathLLBI", False, True, True),
    ("LLBI+CoverageLLBI+PathLLBI", "-LLBI-CoverageLLBI-PathLLBI", True, True, True),
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

VARIANT_KEY = [
    "objective_family",
    "method_family",
    "method",
    "fpp_mode",
    "lb_config",
    "use_root_cuts",
    "is_combinatorial",
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
    "fpp_mode",
    "risk_measure",
    "lb_config",
    "use_llbi",
    "use_coverage_llbi",
    "use_path_llbi",
    "use_root_cuts",
    "is_combinatorial",
    "threads",
    "train_ids",
    "test_ids",
]

OPTIONAL_DIAGNOSTIC_COLUMNS = [
    "objective_in_sample",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "explored_nodes",
    "branch_benders_lazy_cuts_added",
    "branch_benders_candidate_incumbents_checked",
    "branch_benders_use_root_user_cuts",
    "branch_benders_root_user_cuts_added",
    "benders_lifted_lower_bound_precompute_time_sec",
    "coverage_llbi_enabled",
    "coverage_llbi_num_zeta_vars",
    "coverage_llbi_num_constraints",
    "coverage_llbi_precompute_time_sec",
    "path_llbi_enabled",
    "path_llbi_num_b_vars",
    "path_llbi_num_path_constraints",
    "path_llbi_num_paths_used",
    "path_llbi_precompute_time_sec",
    "combinatorial_benders_enabled",
    "combinatorial_benders_integer_cuts_added",
    "combinatorial_benders_fractional_cuts_added",
    "combinatorial_benders_initial_cuts_added",
    "combinatorial_benders_scenarios_checked",
    "combinatorial_benders_separation_time_sec",
    "subproblem_total_solve_time_sec",
    "subproblem_avg_solve_time_sec",
    "subproblem_max_solve_time_sec",
    "train_expected_burned_area",
    "test_expected_burned_area",
    "train_cvar_burned_area",
    "test_cvar_burned_area",
    "test_worst_10pct_burned_area",
    "selected_firebreaks",
    "budget",
    "validation_status",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--expected-rows", type=int, default=450)
    parser.add_argument("--expected-workers", type=int, default=10)
    parser.add_argument("--expected-rows-per-worker", type=int, default=45)
    parser.add_argument("--seed-base", type=int, default=20260601)
    return parser.parse_args()


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def expected_variants() -> set[tuple[str, ...]]:
    variants: set[tuple[str, ...]] = set()
    for objective_family, _risk, risk_suffix, _lambda in OBJECTIVES:
        method = "FPP-SAA" + risk_suffix
        for fpp_mode in ("fpp_base", "fpp_cut"):
            variants.add((
                objective_family, "FPP-SAA", method, fpp_mode, "None", "false", "false",
            ))
    for objective_family, _risk, risk_suffix, _lambda in OBJECTIVES:
        prefix = "FPP-Branch-Benders" + risk_suffix
        for lb_config, lb_suffix, _llbi, _coverage, _path in LB_CONFIGS:
            for use_root in (False, True):
                variants.add((
                    objective_family,
                    "FPP-Branch-Benders",
                    prefix + lb_suffix + ("-RootCuts" if use_root else ""),
                    "",
                    lb_config,
                    bool_text(use_root),
                    "false",
                ))
    for objective_family, _risk, risk_suffix, _lambda in OBJECTIVES:
        variants.add((
            objective_family,
            "FPP-Branch-Benders-Combinatorial",
            "FPP-Branch-Benders-Combinatorial" + risk_suffix,
            "",
            "Combinatorial",
            "false",
            "true",
        ))
    return variants


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"Merged CSV does not exist: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def key(row: dict[str, str], fields: list[str]) -> tuple[str, ...]:
    return tuple(row.get(field, "") for field in fields)


def normalize_ids(value: str) -> str:
    ids = [int(token) for token in re.split(r"[\s,;]+", value.strip()) if token]
    return ",".join(str(value) for value in ids)


def status_value(row: dict[str, str]) -> str:
    return row.get("solver_status") or row.get("status") or ""


def main() -> int:
    args = parse_args()
    combined_dir = args.results_dir / "combined"
    combined_csv = combined_dir / "batch_results_all.csv"
    report_path = combined_dir / "validation_report.txt"
    missing_path = combined_dir / "missing_columns_report.txt"
    rows = read_rows(combined_csv)
    errors: list[str] = []
    warnings: list[str] = []

    header = set(rows[0].keys()) if rows else set()
    for column in ESSENTIAL_COLUMNS:
        if column not in header:
            errors.append(f"Missing essential column: {column}")
    missing_optional = [column for column in OPTIONAL_DIAGNOSTIC_COLUMNS if column not in header]

    if len(rows) != args.expected_rows:
        errors.append(f"Total row count is {len(rows)}; expected {args.expected_rows}.")

    worker_counts = Counter(row.get("worker_id", "") for row in rows)
    for index in range(args.expected_workers):
        worker = f"worker_{index:02d}"
        if worker_counts[worker] != args.expected_rows_per_worker:
            errors.append(
                f"{worker} contributed {worker_counts[worker]} rows; "
                f"expected {args.expected_rows_per_worker}.")

    logical_counts = Counter(key(row, LOGICAL_KEY) for row in rows)
    duplicates = [logical for logical, count in logical_counts.items() if count > 1]
    if duplicates:
        errors.append(f"Duplicate logical rows detected: {duplicates[:5]}")

    expected_blocks = {(case, alpha) for case in CASES for alpha in ALPHAS}
    expected = expected_variants()
    observed_variants = {key(row, VARIANT_KEY) for row in rows}
    missing_variants = sorted(expected - observed_variants)
    unexpected_variants = sorted(observed_variants - expected)
    if missing_variants:
        errors.append(f"Missing expected variants: {missing_variants[:10]}")
    if unexpected_variants:
        errors.append(f"Unexpected variants present: {unexpected_variants[:10]}")

    blocks_by_variant: dict[tuple[str, ...], set[tuple[str, str]]] = defaultdict(set)
    for row in rows:
        blocks_by_variant[key(row, VARIANT_KEY)].add((row.get("case_id", ""), row.get("alpha", "")))
    for variant, blocks in sorted(blocks_by_variant.items()):
        if blocks != expected_blocks:
            errors.append(
                f"Variant {variant} appears on {len(blocks)} blocks; expected the shared 10-block set.")

    ids_by_case: dict[str, set[tuple[str, str]]] = defaultdict(set)
    for row in rows:
        try:
            ids_by_case[row.get("case_id", "")].add((
                normalize_ids(row.get("train_ids", "")),
                normalize_ids(row.get("test_ids", "")),
            ))
        except ValueError as exc:
            errors.append(f"Invalid train/test ID list in task {row.get('task_id', '')}: {exc}")
    for case, id_pairs in sorted(ids_by_case.items()):
        if len(id_pairs) != 1:
            errors.append(f"{case} has {len(id_pairs)} distinct train/test split pairs.")

    for row in rows:
        task = row.get("task_id", "<missing task_id>")
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
        if not status_value(row):
            errors.append(f"{task}: missing solver_status/status.")
        if row.get("method_family") == "FPP-SAA" and status_value(row) != "No feasible solution":
            if "validation_status" in header and not row.get("validation_status"):
                errors.append(f"{task}: FPP-SAA row lacks validation_status.")

    report_lines = [
        "Sub20 alpha 0.01/0.02 FPP exact-study validation",
        f"Rows: {len(rows)}",
        f"Workers: {dict(sorted(worker_counts.items()))}",
        f"Variants: {len(observed_variants)}",
        f"Expected variants: {len(expected)}",
        f"Missing optional diagnostic columns: {len(missing_optional)}",
        "",
    ]
    if errors:
        report_lines.append("Status: FAIL")
        report_lines.extend(f"ERROR: {message}" for message in errors)
    else:
        report_lines.append("Status: PASS")
    if warnings:
        report_lines.extend(f"WARNING: {message}" for message in warnings)

    combined_dir.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    missing_path.write_text(
        "\n".join(missing_optional) + ("\n" if missing_optional else ""),
        encoding="utf-8")
    print(f"Validation report: {report_path}")
    print(f"Missing optional diagnostic columns: {missing_path}")
    if errors:
        print(f"Validation failed with {len(errors)} error(s).")
        return 1
    print("Validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
