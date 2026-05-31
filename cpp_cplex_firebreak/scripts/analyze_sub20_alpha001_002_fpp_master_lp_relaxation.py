#!/usr/bin/env python3
"""Analyze Sub20 FPP master LP relaxation diagnostics."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/diagnostics/sub20_alpha001_002_fpp_master_lp_relaxation")


NUMERIC_COLUMNS = [
    "lp_objective_value",
    "lp_runtime_sec",
    "num_variables",
    "num_constraints",
    "num_nonzeros",
    "static_lb_constraints",
    "static_lb_nonzeros",
    "projected_llbi_root_rounds",
    "projected_llbi_cuts_added",
    "projected_llbi_total_nonzeros",
    "projected_llbi_separation_time_sec",
    "projected_llbi_solve_time_sec",
    "projected_llbi_total_time_sec",
    "total_static_lb_precompute_time_sec",
    "eta_sum_value",
    "min_eta_s",
    "avg_eta_s",
    "max_eta_s",
    "absolute_bound_improvement_vs_master_none",
    "relative_bound_improvement_vs_master_none_percent",
    "gap_to_fpp_saa_lp_base",
    "relative_gap_to_fpp_saa_lp_base_percent",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"Missing diagnostic CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def as_float(row: dict[str, str], column: str) -> float:
    try:
        value = float(row.get(column, ""))
    except ValueError:
        return math.nan
    return value


def safe_percent(numerator: float, denominator: float) -> float:
    if not math.isfinite(numerator) or not math.isfinite(denominator) or abs(denominator) <= 1.0e-12:
        return math.nan
    return 100.0 * numerator / abs(denominator)


def enrich(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    none_by_block: dict[tuple[str, str], float] = {}
    saa_by_block: dict[tuple[str, str], float] = {}
    for row in rows:
        key = (row.get("case_id", ""), row.get("alpha", ""))
        obj = as_float(row, "lp_objective_value")
        if row.get("variant") == "master_lp_none":
            none_by_block[key] = obj
        if row.get("variant") == "fpp_saa_lp_base":
            saa_by_block[key] = obj

    enriched: list[dict[str, str]] = []
    for row in rows:
        out = dict(row)
        static_constraints = (
            as_float(row, "llbi_num_constraints")
            + as_float(row, "coverage_llbi_num_constraints")
            + as_float(row, "path_llbi_num_path_constraints")
            + as_float(row, "projected_llbi_cuts_added")
        )
        static_nonzeros = (
            as_float(row, "llbi_num_nonzeros")
            + as_float(row, "coverage_llbi_num_nonzeros")
            + as_float(row, "path_llbi_num_nonzeros")
            + as_float(row, "projected_llbi_total_nonzeros")
        )
        key = (row.get("case_id", ""), row.get("alpha", ""))
        obj = as_float(row, "lp_objective_value")
        none_obj = none_by_block.get(key, math.nan)
        saa_obj = saa_by_block.get(key, math.nan)
        improvement = obj - none_obj
        gap_to_saa = saa_obj - obj
        out["static_lb_constraints"] = f"{static_constraints:.12g}"
        out["static_lb_nonzeros"] = f"{static_nonzeros:.12g}"
        out["absolute_bound_improvement_vs_master_none"] = f"{improvement:.12g}"
        out["relative_bound_improvement_vs_master_none_percent"] = (
            f"{safe_percent(improvement, none_obj):.12g}")
        out["gap_to_fpp_saa_lp_base"] = f"{gap_to_saa:.12g}"
        out["relative_gap_to_fpp_saa_lp_base_percent"] = f"{safe_percent(gap_to_saa, saa_obj):.12g}"
        enriched.append(out)
    return enriched


def group_rows(rows: list[dict[str, str]], fields: list[str]) -> dict[tuple[str, ...], list[dict[str, str]]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(field, "") for field in fields)].append(row)
    return groups


def summarize(key: tuple[str, ...], fields: list[str], rows: list[dict[str, str]]) -> dict[str, str]:
    out = {field: key[index] for index, field in enumerate(fields)}
    out["completed_rows"] = str(len(rows))
    statuses = Counter(row.get("lp_status", "") for row in rows)
    out["status_counts"] = ";".join(f"{status}:{count}" for status, count in sorted(statuses.items()))
    for column in NUMERIC_COLUMNS:
        values = [as_float(row, column) for row in rows]
        values = [value for value in values if math.isfinite(value)]
        out[f"avg_{column}"] = f"{statistics.fmean(values):.12g}" if values else ""
        out[f"min_{column}"] = f"{min(values):.12g}" if values else ""
        out[f"max_{column}"] = f"{max(values):.12g}" if values else ""
    return out


def write_summary(path: Path, rows: list[dict[str, str]], fields: list[str]) -> None:
    summary = [summarize(key, fields, group) for key, group in sorted(group_rows(rows, fields).items())]
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames: list[str] = []
    for row in summary:
        for field in row:
            if field not in fieldnames:
                fieldnames.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(summary)


def main() -> int:
    args = parse_args()
    csv_path = args.results_dir / "lp_relaxation_results.csv"
    rows = enrich(read_rows(csv_path))
    write_summary(args.results_dir / "summary_by_alpha_variant.csv", rows, ["alpha", "variant"])
    write_summary(args.results_dir / "summary_by_variant.csv", rows, ["variant"])
    print(f"Wrote summaries under {args.results_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
