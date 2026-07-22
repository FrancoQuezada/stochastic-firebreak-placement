#!/usr/bin/env python3
"""Phase 9B publication-ready table writers: deterministic CSV + LaTeX for
the exact-method, heuristic/DPV, out-of-sample, paired-reburn, best-known,
and statistical-comparison tables (section 25).

No manuscript prose/conclusions are generated here -- only formatted data
tables with explicit units, observation counts, and clearly marked missing
values.
"""

from __future__ import annotations

import csv
from pathlib import Path

_LATEX_SPECIAL = {
    "&": r"\&", "%": r"\%", "$": r"\$", "#": r"\#", "_": r"\_",
    "{": r"\{", "}": r"\}", "~": r"\textasciitilde{}", "^": r"\textasciicircum{}",
    "\\": r"\textbackslash{}",
}


def escape_latex(text: str) -> str:
    out = []
    for ch in str(text):
        out.append(_LATEX_SPECIAL.get(ch, ch))
    return "".join(out)


def _format_value(value, missing_marker: str = "--") -> str:
    if value is None or value == "":
        return missing_marker
    if isinstance(value, float):
        return f"{value:.6g}"
    return str(value)


def write_csv_table(path, rows: list, columns: list) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({c: _format_value(row.get(c)) if row.get(c) is None else row.get(c) for c in columns})


def write_latex_table(
    path, rows: list, columns: list, *, headers: dict | None = None,
    caption: str = "", label: str = "", bold_column: str | None = None,
    bold_is_minimum: bool = True, missing_marker: str = "--",
) -> None:
    """Deterministic row/column order (whatever order `rows`/`columns`
    already have -- callers are responsible for sorting them
    deterministically before calling this). Bolds only the row(s) achieving
    the best (min or max) value of `bold_column`, never across tables/
    groups this function wasn't given."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    headers = headers or {c: c for c in columns}

    best_value = None
    if bold_column is not None:
        values = [row.get(bold_column) for row in rows if isinstance(row.get(bold_column), (int, float))]
        if values:
            best_value = min(values) if bold_is_minimum else max(values)

    lines = []
    lines.append(r"\begin{table}[htbp]")
    lines.append(r"\centering")
    if caption:
        lines.append(rf"\caption{{{escape_latex(caption)}}}")
    if label:
        lines.append(rf"\label{{{label}}}")
    lines.append(r"\begin{tabular}{" + "l" * len(columns) + "}")
    lines.append(r"\toprule")
    lines.append(" & ".join(escape_latex(headers.get(c, c)) for c in columns) + r" \\")
    lines.append(r"\midrule")
    for row in rows:
        cells = []
        for column in columns:
            raw = row.get(column)
            text = escape_latex(_format_value(raw, missing_marker))
            if (
                bold_column is not None and column == bold_column
                and best_value is not None and isinstance(raw, (int, float))
                and abs(raw - best_value) <= 1.0e-9
            ):
                text = rf"\textbf{{{text}}}"
            cells.append(text)
        lines.append(" & ".join(cells) + r" \\")
    lines.append(r"\bottomrule")
    lines.append(r"\end{tabular}")
    lines.append(r"\end{table}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------
# Table column specifications (units documented in the header labels).
# ---------------------------------------------------------------------------

TABLE_EXACT_METHODS_COLUMNS = [
    "method", "instances", "optimal_count", "feasible_count", "time_limit_count", "failure_count",
    "mean_time", "median_time", "geometric_mean_time", "mean_solver_gap", "median_solver_gap",
    "mean_gap_to_best_feasible", "mean_gap_to_best_bound",
]
TABLE_EXACT_METHODS_HEADERS = {
    "method": "Method", "instances": "N", "optimal_count": "Optimal", "feasible_count": "Feasible",
    "time_limit_count": "Time-limit", "failure_count": "Failed",
    "mean_time": "Mean time (s)", "median_time": "Median time (s)", "geometric_mean_time": "GeoMean time (s)",
    "mean_solver_gap": "Mean solver gap", "median_solver_gap": "Median solver gap",
    "mean_gap_to_best_feasible": "Mean gap to best-known (rel.)", "mean_gap_to_best_bound": "Mean gap to best LB (rel.)",
}

TABLE_HEURISTIC_DPV_COLUMNS = [
    "method", "instances", "mean_runtime", "median_runtime",
    "mean_gap_to_best_feasible", "median_gap_to_best_feasible", "maximum_gap_to_best_feasible",
    "mean_oos_regret", "median_oos_regret", "mean_paired_regret", "median_paired_regret",
    "mean_burned_cells_oos", "mean_weighted_loss_oos",
]
TABLE_HEURISTIC_DPV_HEADERS = {
    "method": "Method", "instances": "N", "mean_runtime": "Mean runtime (s)", "median_runtime": "Median runtime (s)",
    "mean_gap_to_best_feasible": "Mean gap to best-known (rel.)", "median_gap_to_best_feasible": "Median gap (rel.)",
    "maximum_gap_to_best_feasible": "Max gap (rel.)",
    "mean_oos_regret": "Mean OOS regret (rel.)", "median_oos_regret": "Median OOS regret (rel.)",
    "mean_paired_regret": "Mean paired regret (rel.)", "median_paired_regret": "Median paired regret (rel.)",
    "mean_burned_cells_oos": "Mean burned cells (OOS, cells)", "mean_weighted_loss_oos": "Mean weighted loss (OOS, loss units)",
}

TABLE_OUT_OF_SAMPLE_COLUMNS = [
    "method", "weight_profile", "risk_measure", "observations",
    "mean_out_of_sample_value", "mean_out_of_sample_regret", "median_out_of_sample_regret",
    "mean_burned_cells_out_of_sample",
]
TABLE_OUT_OF_SAMPLE_HEADERS = {
    "method": "Method", "weight_profile": "Weight profile", "risk_measure": "Risk measure", "observations": "N",
    "mean_out_of_sample_value": "Mean OOS value (loss units)",
    "mean_out_of_sample_regret": "Mean regret to best observed (rel.)",
    "median_out_of_sample_regret": "Median regret to best observed (rel.)",
    "mean_burned_cells_out_of_sample": "Mean burned cells (cells)",
}

TABLE_PAIRED_REBURN_COLUMNS = [
    "method", "weight_profile", "risk_measure", "observations",
    "mean_paired_reburn_value", "mean_paired_reburn_regret", "median_paired_reburn_regret",
    "mean_burned_cells_paired_reburn",
]
TABLE_PAIRED_REBURN_HEADERS = {
    "method": "Method", "weight_profile": "Weight profile", "risk_measure": "Risk measure", "observations": "N",
    "mean_paired_reburn_value": "Mean paired value (loss units)",
    "mean_paired_reburn_regret": "Mean regret to best observed (rel.)",
    "median_paired_reburn_regret": "Median regret to best observed (rel.)",
    "mean_burned_cells_paired_reburn": "Mean burned cells (cells)",
}

TABLE_BEST_KNOWN_COLUMNS = [
    "canonical_landscape_id", "instance_id", "weight_profile", "weight_replicate", "weight_map_hash",
    "risk_measure", "alpha", "budget", "best_known_feasible_value", "best_known_feasible_method",
    "best_known_lower_bound", "best_known_certification_gap", "best_known_certified_optimal", "group_size",
]
TABLE_BEST_KNOWN_HEADERS = {
    "canonical_landscape_id": "Landscape", "instance_id": "Instance", "weight_profile": "Weight profile",
    "weight_replicate": "Replicate", "weight_map_hash": "Weight-map hash", "risk_measure": "Risk measure",
    "alpha": "Alpha", "budget": "Budget",
    "best_known_feasible_value": "Best-known feasible (loss units)", "best_known_feasible_method": "Method",
    "best_known_lower_bound": "Best-known LB (loss units)", "best_known_certification_gap": "Certification gap (rel.)",
    "best_known_certified_optimal": "Certified optimal", "group_size": "Exact rows in group",
}

TABLE_STATISTICAL_COMPARISONS_COLUMNS = [
    "method_a", "method_b", "metric", "weight_profile", "risk_measure", "paired_n",
    "median_difference", "mean_difference", "wins", "ties", "losses",
    "test_name", "test_statistic", "raw_p_value", "adjusted_p_value", "effect_size",
]
TABLE_STATISTICAL_COMPARISONS_HEADERS = {
    "method_a": "Method A", "method_b": "Method B", "metric": "Metric", "weight_profile": "Weight profile",
    "risk_measure": "Risk measure", "paired_n": "N", "median_difference": "Median diff (loss units)",
    "mean_difference": "Mean diff (loss units)", "wins": "A wins", "ties": "Ties", "losses": "A losses",
    "test_name": "Test", "test_statistic": "Statistic", "raw_p_value": "Raw p", "adjusted_p_value": "Holm-adj. p",
    "effect_size": "Effect size (rank-biserial)",
}


def write_all_tables(output_dir, *, exact_methods, heuristic_dpv, out_of_sample, paired_reburn,
                      best_known, statistical_comparisons, generate_latex: bool = True) -> list:
    """Writes CSV (always) and LaTeX (if generate_latex) versions of all six
    required tables. Returns the list of file paths written."""
    output_dir = Path(output_dir)
    written = []

    specs = [
        ("table_exact_methods", exact_methods, TABLE_EXACT_METHODS_COLUMNS, TABLE_EXACT_METHODS_HEADERS,
         "mean_gap_to_best_feasible", "Exact FPP method summary"),
        ("table_heuristic_dpv_methods", heuristic_dpv, TABLE_HEURISTIC_DPV_COLUMNS, TABLE_HEURISTIC_DPV_HEADERS,
         "mean_gap_to_best_feasible", "Heuristic and DPV-surrogate method summary"),
        ("table_out_of_sample", out_of_sample, TABLE_OUT_OF_SAMPLE_COLUMNS, TABLE_OUT_OF_SAMPLE_HEADERS,
         "mean_out_of_sample_regret", "Out-of-sample observed-regret summary"),
        ("table_paired_reburn", paired_reburn, TABLE_PAIRED_REBURN_COLUMNS, TABLE_PAIRED_REBURN_HEADERS,
         "mean_paired_reburn_regret", "Paired-reburn observed-regret summary"),
        ("table_best_known", best_known, TABLE_BEST_KNOWN_COLUMNS, TABLE_BEST_KNOWN_HEADERS,
         None, "Best-known feasible values and lower bounds by comparison group"),
        ("table_statistical_comparisons", statistical_comparisons, TABLE_STATISTICAL_COMPARISONS_COLUMNS,
         TABLE_STATISTICAL_COMPARISONS_HEADERS, None, "Paired statistical comparisons (Holm-corrected within family)"),
    ]
    for name, rows, columns, headers, bold_column, caption in specs:
        csv_path = output_dir / f"{name}.csv"
        write_csv_table(csv_path, rows, columns)
        written.append(csv_path)
        if generate_latex:
            tex_path = output_dir / f"{name}.tex"
            write_latex_table(tex_path, rows, columns, headers=headers, caption=caption,
                               label=f"tab:{name}", bold_column=bold_column, bold_is_minimum=True)
            written.append(tex_path)
    return written
