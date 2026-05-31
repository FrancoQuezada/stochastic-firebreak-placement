#!/usr/bin/env python3
"""Analyze Sub20 projected LLBI LP relaxation diagnostics."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/diagnostics/sub20_alpha001_002_projected_llbi_lp_relaxation")

EXPECTED_COLUMNS = [
    "case_id",
    "alpha",
    "variant",
    "lp_status",
    "lp_objective_value",
    "lp_runtime_sec",
    "num_variables",
    "num_constraints",
    "num_nonzeros",
    "llbi_num_constraints",
    "llbi_num_nonzeros",
    "coverage_llbi_num_constraints",
    "coverage_llbi_num_nonzeros",
    "path_llbi_num_path_constraints",
    "path_llbi_num_nonzeros",
    "projected_llbi_family",
    "projected_llbi_strategy",
    "projected_llbi_cuts_added",
    "projected_llbi_total_nonzeros",
    "projected_llbi_avg_nonzeros_per_cut",
    "projected_llbi_separation_time_sec",
    "projected_llbi_solve_time_sec",
    "projected_llbi_total_time_sec",
    "projected_llbi_root_bound_improvement_abs",
    "projected_llbi_root_bound_improvement_pct",
    "projected_poly_candidate_cuts_generated",
    "projected_poly_candidate_cuts_added",
    "projected_exp_separated_cuts_added",
    "projected_exp_separation_rounds",
]

SUMMARY_NUMERIC_COLUMNS = [
    "lp_objective_value",
    "gap_vs_saa_lp",
    "gap_vs_saa_lp_percent",
    "bound_improvement_vs_master_none",
    "bound_improvement_vs_master_none_percent",
    "bound_improvement_vs_master_llbi",
    "bound_improvement_vs_master_llbi_percent",
    "lp_runtime_sec",
    "num_variables",
    "num_constraints",
    "num_nonzeros",
    "static_lb_constraints",
    "static_lb_nonzeros",
    "projected_llbi_cuts_added",
    "projected_llbi_total_nonzeros",
    "projected_llbi_avg_nonzeros_per_cut",
    "projected_llbi_separation_time_sec",
    "projected_llbi_solve_time_sec",
    "projected_llbi_total_time_sec",
    "projected_llbi_root_bound_improvement_abs",
    "projected_llbi_root_bound_improvement_pct",
]

PROJECTION_COMPARISONS = [
    ("coverage_poly_vs_extended", "master_lp_projected_coverage_poly", "master_lp_coverage_llbi"),
    ("coverage_exp_vs_extended", "master_lp_projected_coverage_exp", "master_lp_coverage_llbi"),
    ("path_poly_vs_extended", "master_lp_projected_path_poly", "master_lp_path_llbi"),
    ("path_exp_vs_extended", "master_lp_projected_path_exp", "master_lp_path_llbi"),
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
        return float(row.get(column, ""))
    except ValueError:
        return math.nan


def safe_percent(numerator: float, denominator: float) -> float:
    if not math.isfinite(numerator) or not math.isfinite(denominator) or abs(denominator) <= 1.0e-12:
        return math.nan
    return 100.0 * numerator / abs(denominator)


def fmt(value: float) -> str:
    return f"{value:.12g}" if math.isfinite(value) else ""


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames: list[str] = []
    for row in rows:
        for field in row:
            if field not in fieldnames:
                fieldnames.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def index_by_block_variant(rows: list[dict[str, str]]) -> dict[tuple[str, str, str], dict[str, str]]:
    return {
        (row.get("case_id", ""), row.get("alpha", ""), row.get("variant", "")): row
        for row in rows
    }


def enrich(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    by_key = index_by_block_variant(rows)
    enriched: list[dict[str, str]] = []
    for row in rows:
        out = dict(row)
        block = (row.get("case_id", ""), row.get("alpha", ""))
        obj = as_float(row, "lp_objective_value")
        saa = as_float(by_key.get((*block, "fpp_saa_lp_base"), {}), "lp_objective_value")
        none = as_float(by_key.get((*block, "master_lp_none"), {}), "lp_objective_value")
        llbi = as_float(by_key.get((*block, "master_lp_llbi"), {}), "lp_objective_value")

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
        gap_vs_saa = saa - obj
        improvement_vs_none = obj - none
        improvement_vs_llbi = obj - llbi

        out["static_lb_constraints"] = fmt(static_constraints)
        out["static_lb_nonzeros"] = fmt(static_nonzeros)
        out["gap_vs_saa_lp"] = fmt(gap_vs_saa)
        out["gap_vs_saa_lp_percent"] = fmt(safe_percent(gap_vs_saa, saa))
        out["bound_improvement_vs_master_none"] = fmt(improvement_vs_none)
        out["bound_improvement_vs_master_none_percent"] = fmt(safe_percent(improvement_vs_none, none))
        out["bound_improvement_vs_master_llbi"] = fmt(improvement_vs_llbi)
        out["bound_improvement_vs_master_llbi_percent"] = fmt(safe_percent(improvement_vs_llbi, llbi))
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
    for column in SUMMARY_NUMERIC_COLUMNS:
        values = [as_float(row, column) for row in rows]
        values = [value for value in values if math.isfinite(value)]
        out[f"avg_{column}"] = fmt(statistics.fmean(values)) if values else ""
        out[f"min_{column}"] = fmt(min(values)) if values else ""
        out[f"max_{column}"] = fmt(max(values)) if values else ""
    return out


def write_summary(path: Path, rows: list[dict[str, str]], fields: list[str]) -> None:
    summary = [summarize(key, fields, group) for key, group in sorted(group_rows(rows, fields).items())]
    write_csv(path, summary)


def projection_comparison_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    by_key = index_by_block_variant(rows)
    out_rows: list[dict[str, str]] = []
    blocks = sorted({(row.get("case_id", ""), row.get("alpha", "")) for row in rows})
    for case_id, alpha in blocks:
        for comparison, projected_variant, extended_variant in PROJECTION_COMPARISONS:
            projected = by_key.get((case_id, alpha, projected_variant))
            extended = by_key.get((case_id, alpha, extended_variant))
            if projected is None or extended is None:
                continue
            projected_obj = as_float(projected, "lp_objective_value")
            extended_obj = as_float(extended, "lp_objective_value")
            bound_gap = extended_obj - projected_obj
            out_rows.append({
                "comparison": comparison,
                "case_id": case_id,
                "alpha": alpha,
                "projected_variant": projected_variant,
                "extended_variant": extended_variant,
                "projected_lp_objective_value": fmt(projected_obj),
                "extended_lp_objective_value": fmt(extended_obj),
                "extended_minus_projected_bound": fmt(bound_gap),
                "relative_bound_gap_percent": fmt(safe_percent(bound_gap, extended_obj)),
                "projected_matches_extended_1e_5": "true" if abs(bound_gap) <= 1.0e-5 else "false",
                "projected_num_variables": projected.get("num_variables", ""),
                "extended_num_variables": extended.get("num_variables", ""),
                "projected_num_constraints": projected.get("num_constraints", ""),
                "extended_num_constraints": extended.get("num_constraints", ""),
                "projected_num_nonzeros": projected.get("num_nonzeros", ""),
                "extended_num_nonzeros": extended.get("num_nonzeros", ""),
                "projected_cuts_added": projected.get("projected_llbi_cuts_added", ""),
                "projected_cut_nonzeros": projected.get("projected_llbi_total_nonzeros", ""),
                "projected_lp_runtime_sec": projected.get("lp_runtime_sec", ""),
                "extended_lp_runtime_sec": extended.get("lp_runtime_sec", ""),
                "projected_total_time_sec": projected.get("projected_llbi_total_time_sec", ""),
                "projected_separation_time_sec": projected.get("projected_llbi_separation_time_sec", ""),
                "projected_solve_time_sec": projected.get("projected_llbi_solve_time_sec", ""),
            })
    return out_rows


def aggregate_projection_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    summaries: list[dict[str, str]] = []
    for comparison, group in sorted(group_rows(rows, ["comparison"]).items()):
        group_rows_list = group
        values = lambda col: [as_float(row, col) for row in group_rows_list if math.isfinite(as_float(row, col))]
        match_count = sum(1 for row in group_rows_list if row.get("projected_matches_extended_1e_5") == "true")
        summaries.append({
            "comparison": comparison[0],
            "completed_pairs": str(len(group_rows_list)),
            "matches_extended_1e_5": str(match_count),
            "avg_extended_minus_projected_bound": fmt(statistics.fmean(values("extended_minus_projected_bound"))) if values("extended_minus_projected_bound") else "",
            "max_extended_minus_projected_bound": fmt(max(values("extended_minus_projected_bound"))) if values("extended_minus_projected_bound") else "",
            "avg_relative_bound_gap_percent": fmt(statistics.fmean(values("relative_bound_gap_percent"))) if values("relative_bound_gap_percent") else "",
            "avg_projected_lp_runtime_sec": fmt(statistics.fmean(values("projected_lp_runtime_sec"))) if values("projected_lp_runtime_sec") else "",
            "avg_extended_lp_runtime_sec": fmt(statistics.fmean(values("extended_lp_runtime_sec"))) if values("extended_lp_runtime_sec") else "",
            "avg_projected_total_time_sec": fmt(statistics.fmean(values("projected_total_time_sec"))) if values("projected_total_time_sec") else "",
            "avg_projected_cuts_added": fmt(statistics.fmean(values("projected_cuts_added"))) if values("projected_cuts_added") else "",
            "avg_projected_cut_nonzeros": fmt(statistics.fmean(values("projected_cut_nonzeros"))) if values("projected_cut_nonzeros") else "",
            "avg_projected_num_variables": fmt(statistics.fmean(values("projected_num_variables"))) if values("projected_num_variables") else "",
            "avg_extended_num_variables": fmt(statistics.fmean(values("extended_num_variables"))) if values("extended_num_variables") else "",
            "avg_projected_num_constraints": fmt(statistics.fmean(values("projected_num_constraints"))) if values("projected_num_constraints") else "",
            "avg_extended_num_constraints": fmt(statistics.fmean(values("extended_num_constraints"))) if values("extended_num_constraints") else "",
            "avg_projected_num_nonzeros": fmt(statistics.fmean(values("projected_num_nonzeros"))) if values("projected_num_nonzeros") else "",
            "avg_extended_num_nonzeros": fmt(statistics.fmean(values("extended_num_nonzeros"))) if values("extended_num_nonzeros") else "",
        })
    return summaries


def projection_runtime_rows(comparison_rows: list[dict[str, str]]) -> list[dict[str, str]]:
    out: list[dict[str, str]] = []
    for row in comparison_rows:
        projected_runtime = as_float(row, "projected_lp_runtime_sec")
        extended_runtime = as_float(row, "extended_lp_runtime_sec")
        projected_vars = as_float(row, "projected_num_variables")
        extended_vars = as_float(row, "extended_num_variables")
        projected_rows = as_float(row, "projected_num_constraints")
        extended_rows = as_float(row, "extended_num_constraints")
        projected_nnz = as_float(row, "projected_num_nonzeros")
        extended_nnz = as_float(row, "extended_num_nonzeros")
        out.append({
            **row,
            "runtime_ratio_projected_over_extended": fmt(projected_runtime / extended_runtime) if extended_runtime > 0 else "",
            "variable_reduction_vs_extended": fmt(extended_vars - projected_vars),
            "constraint_reduction_vs_extended": fmt(extended_rows - projected_rows),
            "nonzero_reduction_vs_extended": fmt(extended_nnz - projected_nnz),
        })
    return out


def projected_cut_diagnostics(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    projected = [row for row in rows if row.get("variant", "").startswith("master_lp_projected_")]
    return [summarize(key, ["variant"], group) for key, group in sorted(group_rows(projected, ["variant"]).items())]


def main() -> int:
    args = parse_args()
    csv_path = args.results_dir / "lp_relaxation_results.csv"
    rows = read_rows(csv_path)
    header = set(rows[0].keys()) if rows else set()
    missing = [column for column in EXPECTED_COLUMNS if column not in header]
    (args.results_dir / "missing_columns_report.txt").write_text(
        "Missing analysis columns:\n" + "\n".join(missing) + ("\n" if missing else "None\n"),
        encoding="utf-8",
    )

    enriched = enrich(rows)
    write_summary(args.results_dir / "summary_by_alpha_variant.csv", enriched, ["alpha", "variant"])
    write_summary(args.results_dir / "summary_by_variant.csv", enriched, ["variant"])
    comparison_rows = projection_comparison_rows(enriched)
    quality_rows = comparison_rows + aggregate_projection_rows(comparison_rows)
    write_csv(args.results_dir / "projection_quality_vs_extended.csv", quality_rows)
    write_csv(args.results_dir / "projection_runtime_vs_extended.csv", projection_runtime_rows(comparison_rows))
    write_csv(args.results_dir / "projection_cut_diagnostics.csv", projected_cut_diagnostics(enriched))
    print(f"Wrote projected LLBI LP diagnostic summaries under {args.results_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
