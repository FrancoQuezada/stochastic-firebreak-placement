#!/usr/bin/env python3
"""Validate Sub20 FPP master LP relaxation diagnostic outputs."""

from __future__ import annotations

import argparse
import csv
import math
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/diagnostics/sub20_alpha001_002_fpp_master_lp_relaxation")
EXPECTED_VARIANTS = {
    "fpp_saa_lp_base",
    "master_lp_none",
    "master_lp_llbi",
    "master_lp_coverage_llbi",
    "master_lp_path_llbi",
}
REQUIRED_MODEL_FIELDS = [
    "num_variables",
    "num_constraints",
    "num_nonzeros",
    "num_y_variables",
    "num_eta_variables",
    "num_x_variables",
]
REQUIRED_STATIC_FIELDS = [
    "llbi_num_constraints",
    "llbi_num_nonzeros",
    "coverage_llbi_num_zeta_vars",
    "coverage_llbi_num_constraints",
    "coverage_llbi_num_nonzeros",
    "path_llbi_num_b_vars",
    "path_llbi_num_path_constraints",
    "path_llbi_num_paths_used",
    "path_llbi_num_nonzeros",
    "llbi_precompute_time_sec",
    "coverage_llbi_precompute_time_sec",
    "path_llbi_precompute_time_sec",
    "total_static_lb_precompute_time_sec",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--expected-rows", type=int, default=50)
    parser.add_argument("--seed-base", type=int, default=20260601)
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"Missing diagnostic CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def as_float(value: str) -> float:
    try:
        return float(value)
    except ValueError:
        return math.nan


def normalize_ids(value: str) -> str:
    ids = [token for token in value.replace(",", ";").split(";") if token.strip()]
    return ";".join(str(int(token)) for token in ids)


def main() -> int:
    args = parse_args()
    csv_path = args.results_dir / "lp_relaxation_results.csv"
    report_path = args.results_dir / "validation_report.txt"
    rows = read_rows(csv_path)
    errors: list[str] = []

    header = set(rows[0].keys()) if rows else set()
    for field in [
        "experiment_id",
        "case_id",
        "seed_base",
        "seed",
        "landscape",
        "alpha",
        "train_count",
        "train_ids",
        "variant",
        "lp_status",
        "lp_objective_value",
        "lp_runtime_sec",
        "eta_sum_value",
        "min_eta_s",
        "avg_eta_s",
        "max_eta_s",
        *REQUIRED_MODEL_FIELDS,
        *REQUIRED_STATIC_FIELDS,
    ]:
        if field not in header:
            errors.append(f"Missing required column: {field}")

    if len(rows) != args.expected_rows:
        errors.append(f"Row count is {len(rows)}; expected {args.expected_rows}.")

    logical_counts = Counter((row.get("case_id", ""), row.get("alpha", ""), row.get("variant", "")) for row in rows)
    duplicates = [key for key, count in logical_counts.items() if count > 1]
    if duplicates:
        errors.append(f"Duplicate logical rows: {duplicates[:10]}")

    block_variants: dict[tuple[str, str], set[str]] = defaultdict(set)
    variant_counts = Counter(row.get("variant", "") for row in rows)
    train_ids_by_case: dict[str, set[str]] = defaultdict(set)
    train_ids_by_case_alpha: dict[tuple[str, str], set[str]] = defaultdict(set)

    for row in rows:
        case_id = row.get("case_id", "")
        alpha = row.get("alpha", "")
        variant = row.get("variant", "")
        block_variants[(case_id, alpha)].add(variant)
        try:
            normalized = normalize_ids(row.get("train_ids", ""))
            train_ids_by_case[case_id].add(normalized)
            train_ids_by_case_alpha[(case_id, alpha)].add(normalized)
        except ValueError:
            errors.append(f"Invalid train_ids for {case_id} alpha {alpha} variant {variant}.")

        if row.get("seed_base") != str(args.seed_base):
            errors.append(f"{case_id} {alpha} {variant}: seed_base={row.get('seed_base')}.")
        if row.get("train_count") not in {"100", "100.0"}:
            errors.append(f"{case_id} {alpha} {variant}: train_count={row.get('train_count')}.")

        objective = as_float(row.get("lp_objective_value", ""))
        if not math.isfinite(objective):
            errors.append(f"{case_id} {alpha} {variant}: LP objective is not finite.")
        elif objective < -1.0e-6:
            errors.append(f"{case_id} {alpha} {variant}: LP objective is negative ({objective}).")
        if "unbounded" in row.get("lp_status", "").lower():
            errors.append(f"{case_id} {alpha} {variant}: LP status indicates unbounded.")
        if variant.startswith("master_lp_"):
            min_eta = as_float(row.get("min_eta_s", ""))
            if not math.isfinite(min_eta):
                errors.append(f"{case_id} {alpha} {variant}: min_eta_s is not finite.")
            elif min_eta < -1.0e-6:
                errors.append(f"{case_id} {alpha} {variant}: min_eta_s={min_eta}.")

        for field in REQUIRED_MODEL_FIELDS + REQUIRED_STATIC_FIELDS:
            if row.get(field, "") == "":
                errors.append(f"{case_id} {alpha} {variant}: missing {field}.")

    for block, variants in sorted(block_variants.items()):
        if variants != EXPECTED_VARIANTS:
            errors.append(f"Block {block} has variants {sorted(variants)}; expected {sorted(EXPECTED_VARIANTS)}.")

    for variant in sorted(EXPECTED_VARIANTS):
        if variant_counts[variant] != 10:
            errors.append(f"Variant {variant} appears {variant_counts[variant]} times; expected 10.")

    for case_id, split_set in sorted(train_ids_by_case.items()):
        if len(split_set) != 1:
            errors.append(f"{case_id} has {len(split_set)} distinct train_id sets across alphas/variants.")
    for block, split_set in sorted(train_ids_by_case_alpha.items()):
        if len(split_set) != 1:
            errors.append(f"{block} has {len(split_set)} distinct train_id sets across variants.")

    report_lines = [
        "Sub20 FPP master LP relaxation validation",
        f"Rows: {len(rows)}",
        f"Variant counts: {dict(sorted(variant_counts.items()))}",
        "",
        "Status: " + ("FAIL" if errors else "PASS"),
    ]
    report_lines.extend(f"ERROR: {message}" for message in errors)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(report_lines) + "\n", encoding="utf-8")
    print(f"Validation report: {report_path}")
    if errors:
        print(f"Validation failed with {len(errors)} error(s).")
        return 1
    print("Validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
