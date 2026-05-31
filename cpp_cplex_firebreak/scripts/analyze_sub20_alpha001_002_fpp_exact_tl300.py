#!/usr/bin/env python3
"""Aggregate completed Sub20 alpha 0.01/0.02 FPP exact-study results."""

from __future__ import annotations

import argparse
import csv
import statistics
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")

SUMMARY_NUMERIC_COLUMNS = [
    "objective_in_sample",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "train_expected_burned_area",
    "train_worst_10pct_burned_area",
    "train_cvar_burned_area",
    "test_expected_burned_area",
    "test_worst_10pct_burned_area",
    "test_cvar_burned_area",
    "selected_firebreak_count",
    "budget",
    "branch_benders_lazy_cuts_added",
    "branch_benders_candidate_incumbents_checked",
    "benders_lifted_lower_bound_precompute_time_sec",
    "branch_benders_root_user_cuts_added",
    "combinatorial_benders_integer_cuts_added",
    "combinatorial_benders_fractional_cuts_added",
    "combinatorial_benders_initial_cuts_added",
    "combinatorial_benders_scenarios_checked",
    "combinatorial_benders_separation_time_sec",
    "subproblem_total_solve_time_sec",
    "subproblem_avg_solve_time_sec",
    "subproblem_max_solve_time_sec",
]

OPTIONAL_COLUMNS = [
    "objective_in_sample",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "train_expected_burned_area",
    "train_worst_10pct_burned_area",
    "test_expected_burned_area",
    "test_worst_10pct_burned_area",
    "train_cvar_burned_area",
    "test_cvar_burned_area",
    "selected_firebreaks",
    "budget",
    "validation_status",
    "branch_benders_lazy_cuts_added",
    "branch_benders_candidate_incumbents_checked",
    "branch_benders_root_user_cuts_added",
    "benders_lifted_lower_bound_precompute_time_sec",
    "combinatorial_benders_integer_cuts_added",
    "combinatorial_benders_fractional_cuts_added",
    "combinatorial_benders_initial_cuts_added",
    "subproblem_total_solve_time_sec",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"Merged CSV does not exist: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def float_value(value: str) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def selected_count(value: str) -> int | None:
    if value is None or value.strip() == "":
        return None
    tokens = [token for token in value.replace(";", ",").split(",") if token.strip()]
    return len(tokens)


def enrich_rows(rows: list[dict[str, str]]) -> None:
    for row in rows:
        if "selected_firebreak_count" not in row or not row.get("selected_firebreak_count"):
            count = selected_count(row.get("selected_firebreaks", ""))
            if count is not None:
                row["selected_firebreak_count"] = str(count)


def group_rows(rows: list[dict[str, str]], fields: list[str]) -> dict[tuple[str, ...], list[dict[str, str]]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(field, "") for field in fields)].append(row)
    return groups


def summarize_group(
    key: tuple[str, ...],
    fields: list[str],
    rows: list[dict[str, str]],
    numeric_columns: list[str],
) -> dict[str, str]:
    out = {field: key[index] for index, field in enumerate(fields)}
    out["completed_rows"] = str(len(rows))
    status_counts = Counter(row.get("solver_status") or row.get("status") or "" for row in rows)
    out["status_counts"] = ";".join(f"{status}:{count}" for status, count in sorted(status_counts.items()))
    validation_counts = Counter(row.get("validation_status", "") for row in rows if row.get("validation_status", "") != "")
    out["fpp_saa_validation_status_counts"] = ";".join(
        f"{status}:{count}" for status, count in sorted(validation_counts.items()))
    for column in numeric_columns:
        values = [value for value in (float_value(row.get(column, "")) for row in rows) if value is not None]
        if values:
            out[f"avg_{column}"] = f"{statistics.fmean(values):.10g}"
            out[f"min_{column}"] = f"{min(values):.10g}"
            out[f"max_{column}"] = f"{max(values):.10g}"
        else:
            out[f"avg_{column}"] = ""
            out[f"min_{column}"] = ""
            out[f"max_{column}"] = ""
    return out


def write_summary(
    path: Path,
    rows: list[dict[str, str]],
    group_fields: list[str],
    numeric_columns: list[str],
) -> None:
    groups = group_rows(rows, group_fields)
    summary = [
        summarize_group(key, group_fields, group_rows_, numeric_columns)
        for key, group_rows_ in sorted(groups.items())
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    fields: list[str] = []
    for row in summary:
        for field in row:
            if field not in fields:
                fields.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summary)


def write_missing(path: Path, header: set[str]) -> None:
    missing = [column for column in OPTIONAL_COLUMNS if column not in header]
    path.write_text("\n".join(missing) + ("\n" if missing else ""), encoding="utf-8")


def main() -> int:
    args = parse_args()
    combined_csv = args.results_dir / "combined" / "batch_results_all.csv"
    summary_dir = args.results_dir / "combined" / "summary"
    rows = read_rows(combined_csv)
    enrich_rows(rows)
    header = set(rows[0].keys()) if rows else set()

    variant_fields = [
        "objective_family",
        "method_family",
        "method",
        "fpp_mode",
        "lb_config",
        "use_root_cuts",
        "is_combinatorial",
    ]
    alpha_variant_fields = ["alpha"] + variant_fields

    write_summary(
        summary_dir / "summary_by_alpha_objective_method_variant.csv",
        rows,
        alpha_variant_fields,
        SUMMARY_NUMERIC_COLUMNS,
    )
    write_summary(
        summary_dir / "summary_by_objective_method_variant.csv",
        rows,
        variant_fields,
        SUMMARY_NUMERIC_COLUMNS,
    )
    write_summary(
        summary_dir / "runtime_summary.csv",
        rows,
        variant_fields,
        [
            "runtime_seconds",
            "mip_gap",
            "explored_nodes",
            "branch_benders_candidate_incumbents_checked",
            "subproblem_total_solve_time_sec",
            "subproblem_avg_solve_time_sec",
            "subproblem_max_solve_time_sec",
        ],
    )
    write_summary(
        summary_dir / "oos_quality_summary.csv",
        rows,
        variant_fields,
        [
            "objective_in_sample",
            "train_expected_burned_area",
            "train_worst_10pct_burned_area",
            "train_cvar_burned_area",
            "test_expected_burned_area",
            "test_worst_10pct_burned_area",
            "test_cvar_burned_area",
            "selected_firebreak_count",
            "budget",
        ],
    )
    bb_rows = [row for row in rows if "Branch-Benders" in row.get("method_family", "")]
    write_summary(
        summary_dir / "bb_diagnostics_summary.csv",
        bb_rows,
        variant_fields,
        [
            "runtime_seconds",
            "explored_nodes",
            "branch_benders_lazy_cuts_added",
            "branch_benders_candidate_incumbents_checked",
            "branch_benders_root_user_cuts_added",
            "benders_lifted_lower_bound_precompute_time_sec",
            "coverage_llbi_num_zeta_vars",
            "path_llbi_num_paths_used",
            "combinatorial_benders_integer_cuts_added",
            "combinatorial_benders_fractional_cuts_added",
            "combinatorial_benders_initial_cuts_added",
            "combinatorial_benders_separation_time_sec",
            "subproblem_total_solve_time_sec",
        ],
    )
    saa_rows = [row for row in rows if row.get("method_family") == "FPP-SAA"]
    write_summary(
        summary_dir / "saa_formulation_comparison.csv",
        saa_rows,
        ["alpha", "objective_family", "method", "fpp_mode"],
        [
            "objective_in_sample",
            "best_bound",
            "mip_gap",
            "runtime_seconds",
            "train_expected_burned_area",
            "test_expected_burned_area",
            "test_cvar_burned_area",
            "selected_firebreak_count",
        ],
    )
    write_missing(summary_dir / "missing_columns_report.txt", header)
    print(f"Wrote summaries under {summary_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
