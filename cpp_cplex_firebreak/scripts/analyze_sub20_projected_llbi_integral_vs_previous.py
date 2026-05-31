#!/usr/bin/env python3
"""Compare Sub20 projected LLBI integral results against the previous exact study."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path


DEFAULT_PREVIOUS_RESULTS = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300/combined/batch_results_all.csv")
DEFAULT_PROJECTED_RESULTS = Path(
    "results/batch/sub20_projected_llbi_integral_alpha001_002_tl300/combined/"
    "batch_results_projected_integral_all.csv"
)
DEFAULT_OUTPUT_DIR = Path(
    "results/batch/sub20_projected_llbi_integral_alpha001_002_tl300/combined/"
    "comparison_with_previous"
)

METRIC_COLUMNS = [
    "objective_metric",
    "best_bound",
    "gap_metric",
    "runtime_metric",
    "explored_nodes",
    "train_expected_burned_area",
    "train_worst10_metric",
    "test_expected_burned_area",
    "test_worst10_metric",
    "branch_benders_lazy_cuts_added",
    "branch_benders_candidate_incumbents_checked",
    "projected_llbi_cuts_added",
    "projected_llbi_total_nonzeros",
    "projected_llbi_avg_nonzeros_per_cut",
    "projected_llbi_max_nonzeros_per_cut",
    "projected_llbi_separation_time_sec",
    "projected_llbi_total_time_sec",
    "projected_llbi_root_bound_improvement_abs",
    "projected_llbi_root_bound_improvement_pct",
    "num_variables",
    "num_constraints",
    "num_nonzeros",
    "coverage_llbi_num_zeta_vars",
    "path_llbi_num_b_vars",
]

OPTIONAL_INPUT_COLUMNS = [
    "objective_value",
    "objective_in_sample",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "runtime_sec",
    "explored_nodes",
    "train_expected_burned_area",
    "train_worst10_burned_area",
    "train_worst_10pct_burned_area",
    "test_expected_burned_area",
    "test_worst10_burned_area",
    "test_worst_10pct_burned_area",
    "branch_benders_lazy_cuts_added",
    "branch_benders_candidate_incumbents_checked",
    "projected_llbi_cuts_added",
    "projected_llbi_total_nonzeros",
    "projected_llbi_avg_nonzeros_per_cut",
    "projected_llbi_max_nonzeros_per_cut",
    "projected_llbi_separation_time_sec",
    "projected_llbi_total_time_sec",
    "projected_llbi_root_bound_improvement_abs",
    "projected_llbi_root_bound_improvement_pct",
    "num_variables",
    "num_constraints",
    "num_nonzeros",
    "coverage_llbi_num_zeta_vars",
    "path_llbi_num_b_vars",
]

BASELINE_ORDER = [
    "FPP-SAA:fpp_base",
    "FPP-SAA:fpp_cut",
    "FPP-Branch-Benders",
    "FPP-Branch-Benders-RootCuts",
    "FPP-Branch-Benders-LLBI",
    "FPP-Branch-Benders-LLBI-RootCuts",
    "FPP-Branch-Benders-CoverageLLBI",
    "FPP-Branch-Benders-CoverageLLBI-RootCuts",
    "FPP-Branch-Benders-PathLLBI",
    "FPP-Branch-Benders-PathLLBI-RootCuts",
    "FPP-Branch-Benders-CoverageLLBI-PathLLBI",
    "FPP-Branch-Benders-CoverageLLBI-PathLLBI-RootCuts",
    "FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI",
    "FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI-RootCuts",
    "FPP-Branch-Benders-Combinatorial",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous-results", type=Path, default=DEFAULT_PREVIOUS_RESULTS)
    parser.add_argument("--projected-results", type=Path, default=DEFAULT_PROJECTED_RESULTS)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise SystemExit(f"Missing CSV: {path}")
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


def as_float(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        result = float(value)
    except ValueError:
        return None
    return result if math.isfinite(result) else None


def first_float(row: dict[str, str], names: list[str]) -> float | None:
    for name in names:
        value = as_float(row.get(name))
        if value is not None:
            return value
    return None


def fmt(value: float | None) -> str:
    return "" if value is None else f"{value:.10g}"


def calculated_gap(row: dict[str, str]) -> float | None:
    objective = first_float(row, ["objective_value", "objective_in_sample"])
    best_bound = first_float(row, ["best_bound"])
    if objective is None or best_bound is None:
        return None
    if abs(best_bound) > 1e20:
        return None
    return max(0.0, abs(objective - best_bound) / max(abs(objective), 1e-10))


def enrich_rows(rows: list[dict[str, str]], source: str) -> None:
    for row in rows:
        row["source"] = source
        row["objective_metric"] = fmt(first_float(row, ["objective_value", "objective_in_sample"]))
        row["gap_metric"] = fmt(calculated_gap(row))
        row["runtime_metric"] = fmt(first_float(row, ["runtime_seconds", "runtime_sec"]))
        row["train_worst10_metric"] = fmt(first_float(row, [
            "train_worst10_burned_area",
            "train_worst_10pct_burned_area",
            "train_cvar_burned_area",
        ]))
        row["test_worst10_metric"] = fmt(first_float(row, [
            "test_worst10_burned_area",
            "test_worst_10pct_burned_area",
            "test_cvar_burned_area",
        ]))


def group_rows(rows: list[dict[str, str]], fields: list[str]) -> dict[tuple[str, ...], list[dict[str, str]]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(field, "") for field in fields)].append(row)
    return groups


def summarize(key: tuple[str, ...], fields: list[str], rows: list[dict[str, str]], metrics: list[str]) -> dict[str, str]:
    out = {field: key[index] for index, field in enumerate(fields)}
    out["completed_rows"] = str(len(rows))
    statuses = Counter(row.get("solver_status") or row.get("status") or row.get("worker_status") or "" for row in rows)
    out["status_counts"] = ";".join(f"{status}:{count}" for status, count in sorted(statuses.items()))
    for metric in metrics:
        values = [value for value in (as_float(row.get(metric)) for row in rows) if value is not None]
        if values:
            out[f"avg_{metric}"] = fmt(statistics.fmean(values))
            out[f"min_{metric}"] = fmt(min(values))
            out[f"max_{metric}"] = fmt(max(values))
        else:
            out[f"avg_{metric}"] = ""
            out[f"min_{metric}"] = ""
            out[f"max_{metric}"] = ""
    return out


def summarize_groups(rows: list[dict[str, str]], fields: list[str], metrics: list[str]) -> list[dict[str, str]]:
    return [
        summarize(key, fields, group, metrics)
        for key, group in sorted(group_rows(rows, fields).items())
    ]


def baseline_category(row: dict[str, str]) -> str | None:
    method_family = row.get("method_family", "")
    method = row.get("method", "")
    if method_family == "FPP-SAA" or method.startswith("FPP-SAA"):
        mode = row.get("fpp_mode", "")
        if mode in {"fpp_base", "fpp_cut"}:
            return f"FPP-SAA:{mode}"
        return None
    if "Combinatorial" in method:
        return "FPP-Branch-Benders-Combinatorial"
    if "Branch-Benders" not in method:
        return None

    if "LLBI-CoverageLLBI-PathLLBI" in method:
        category = "FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI"
    elif "CoverageLLBI-PathLLBI" in method:
        category = "FPP-Branch-Benders-CoverageLLBI-PathLLBI"
    elif "CoverageLLBI" in method:
        category = "FPP-Branch-Benders-CoverageLLBI"
    elif "PathLLBI" in method:
        category = "FPP-Branch-Benders-PathLLBI"
    elif "LLBI" in method:
        category = "FPP-Branch-Benders-LLBI"
    else:
        category = "FPP-Branch-Benders"
    if row.get("use_root_cuts", "").lower() == "true" or method.endswith("-RootCuts"):
        category += "-RootCuts"
    return category


def project_group_key(row: dict[str, str]) -> tuple[str, str, str, str, str, str]:
    return (
        row.get("alpha", ""),
        row.get("objective_family", ""),
        row.get("method", ""),
        row.get("projected_variant", ""),
        row.get("projected_llbi_strategy", ""),
        row.get("use_root_cuts", ""),
    )


def group_avg(rows: list[dict[str, str]], metric: str) -> float | None:
    values = [value for value in (as_float(row.get(metric)) for row in rows) if value is not None]
    return statistics.fmean(values) if values else None


def compare_projected_to_previous(
    projected_rows: list[dict[str, str]],
    previous_rows: list[dict[str, str]],
) -> list[dict[str, str]]:
    previous_filtered: list[dict[str, str]] = []
    for row in previous_rows:
        category = baseline_category(row)
        if category in BASELINE_ORDER:
            row["baseline_category"] = category
            previous_filtered.append(row)

    previous_groups = group_rows(previous_filtered, ["alpha", "objective_family", "baseline_category"])
    projected_groups = group_rows(
        projected_rows,
        ["alpha", "objective_family", "method", "projected_variant", "projected_llbi_strategy", "use_root_cuts"],
    )
    rows: list[dict[str, str]] = []
    for projected_key, group in sorted(projected_groups.items()):
        alpha, objective, method, variant, strategy, root_cuts = projected_key
        for baseline in BASELINE_ORDER:
            previous_group = previous_groups.get((alpha, objective, baseline), [])
            if not previous_group:
                continue
            out = {
                "alpha": alpha,
                "objective_family": objective,
                "projected_method": method,
                "projected_variant": variant,
                "projected_llbi_strategy": strategy,
                "projected_use_root_cuts": root_cuts,
                "baseline_category": baseline,
                "projected_rows": str(len(group)),
                "baseline_rows": str(len(previous_group)),
            }
            for metric in [
                "objective_metric",
                "best_bound",
                "gap_metric",
                "runtime_metric",
                "explored_nodes",
                "train_expected_burned_area",
                "test_expected_burned_area",
                "branch_benders_lazy_cuts_added",
                "branch_benders_candidate_incumbents_checked",
                "num_variables",
                "num_constraints",
                "num_nonzeros",
            ]:
                projected_avg = group_avg(group, metric)
                baseline_avg = group_avg(previous_group, metric)
                out[f"projected_avg_{metric}"] = fmt(projected_avg)
                out[f"baseline_avg_{metric}"] = fmt(baseline_avg)
                out[f"delta_{metric}"] = fmt(
                    None if projected_avg is None or baseline_avg is None else projected_avg - baseline_avg
                )
            rows.append(out)
    return rows


def write_missing(path: Path, previous_header: set[str], projected_header: set[str]) -> None:
    lines: list[str] = []
    for column in OPTIONAL_INPUT_COLUMNS:
        if column not in previous_header:
            lines.append(f"previous missing: {column}")
        if column not in projected_header:
            lines.append(f"projected missing: {column}")
    path.write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")


def main() -> int:
    args = parse_args()
    previous_rows = read_rows(args.previous_results)
    projected_rows = read_rows(args.projected_results)
    enrich_rows(previous_rows, "previous")
    enrich_rows(projected_rows, "projected")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    projected_fields = ["alpha", "objective_family", "projected_variant", "projected_llbi_strategy", "use_root_cuts", "method"]

    write_csv(
        args.output_dir / "summary_projected_by_alpha_objective_variant.csv",
        summarize_groups(projected_rows, projected_fields, METRIC_COLUMNS),
    )
    write_csv(
        args.output_dir / "summary_projected_vs_previous_by_alpha_objective.csv",
        compare_projected_to_previous(projected_rows, previous_rows),
    )
    write_csv(
        args.output_dir / "summary_projected_runtime_quality.csv",
        summarize_groups(projected_rows, projected_fields, [
            "objective_metric",
            "best_bound",
            "gap_metric",
            "runtime_metric",
            "explored_nodes",
            "train_expected_burned_area",
            "test_expected_burned_area",
            "train_worst10_metric",
            "test_worst10_metric",
        ]),
    )
    write_csv(
        args.output_dir / "summary_projected_cut_diagnostics.csv",
        summarize_groups(projected_rows, projected_fields, [
            "branch_benders_lazy_cuts_added",
            "branch_benders_candidate_incumbents_checked",
            "projected_llbi_cuts_added",
            "projected_llbi_coverage_cuts_added",
            "projected_llbi_path_cuts_added",
            "projected_llbi_total_nonzeros",
            "projected_llbi_avg_nonzeros_per_cut",
            "projected_llbi_max_nonzeros_per_cut",
            "projected_llbi_separation_time_sec",
            "projected_llbi_total_time_sec",
            "projected_llbi_root_bound_improvement_abs",
            "projected_llbi_root_bound_improvement_pct",
        ]),
    )
    write_csv(
        args.output_dir / "summary_projected_model_size.csv",
        summarize_groups(projected_rows, projected_fields, [
            "num_variables",
            "num_constraints",
            "num_nonzeros",
            "coverage_llbi_num_zeta_vars",
            "path_llbi_num_b_vars",
        ]),
    )
    write_missing(
        args.output_dir / "missing_columns_report.txt",
        set(previous_rows[0].keys()) if previous_rows else set(),
        set(projected_rows[0].keys()) if projected_rows else set(),
    )
    print(f"Wrote projected comparison summaries under {args.output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
