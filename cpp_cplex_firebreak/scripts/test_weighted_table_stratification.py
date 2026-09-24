#!/usr/bin/env python3
"""Phase 10 section 13: exact/heuristic table stratification tests. Default
tables are profile-stratified; aggregate-across-profile tables must be
requested explicitly and clearly labeled. Confirms profile-specific best
values are never compared across profiles (both in the summary tables and
in LaTeX bold-marking).

Usage: python3 scripts/test_weighted_table_stratification.py
"""

from __future__ import annotations

import tempfile
from pathlib import Path

import weighted_analysis_summaries as summ
import weighted_analysis_tables as tables
import weighted_analysis_core as core
from weighted_analysis_test_helpers import check, make_exact_row, report


def _rows():
    # Method A is fast under 'homogeneous' but slow under 'heterogeneous';
    # method B is the reverse -- so "the best mean_gap_to_best_feasible"
    # differs per profile, and a global bold-marking would be wrong.
    rows = [
        make_exact_row("h1", value=100.0, best_bound=100.0, runtime=1.0, method="A", weight_profile="homogeneous", instance_id="i1"),
        make_exact_row("h2", value=100.0, best_bound=100.0, runtime=1.0, method="B", weight_profile="homogeneous", instance_id="i1"),
        make_exact_row("t1", value=100.0, best_bound=100.0, runtime=1.0, method="A", weight_profile="heterogeneous", instance_id="i1"),
        make_exact_row("t2", value=100.0, best_bound=100.0, runtime=1.0, method="B", weight_profile="heterogeneous", instance_id="i1"),
    ]
    # Force distinct gap_to_best_known_feasible per (method, profile) cell,
    # bypassing the full comparison-group computation (irrelevant here).
    rows[0]["gap_to_best_known_feasible"] = 0.0    # A, homogeneous: best in that profile
    rows[1]["gap_to_best_known_feasible"] = 0.05   # B, homogeneous
    rows[2]["gap_to_best_known_feasible"] = 0.02   # A, heterogeneous
    rows[3]["gap_to_best_known_feasible"] = 0.0    # B, heterogeneous: best in that profile
    for r in rows:
        r["objective_space"] = core.OBJECTIVE_SPACE_EXACT_FPP
    return rows


def test_default_is_profile_stratified():
    rows = _rows()
    table = summ.exact_method_summary(rows)
    check(len(table) == 4, "one row per (method, profile) combination by default -- 2 methods x 2 profiles")
    profiles_seen = {row["weight_profile"] for row in table}
    check(profiles_seen == {"homogeneous", "heterogeneous"}, "actual profile names are used, not a pooled placeholder, by default")


def test_aggregated_variant_is_explicit_and_labeled():
    rows = _rows()
    table = summ.exact_method_summary_aggregated(rows)
    check(len(table) == 2, "one row per method when explicitly aggregated across profiles")
    check(all(row["weight_profile"] == "ALL_PROFILES_AGGREGATED" for row in table),
          "the aggregated table clearly labels itself, never silently reusing a real profile name")
    check(all(row["profiles_pooled"] == 2 for row in table), "the aggregated table reports how many profiles were pooled")


def test_heuristic_dpv_summary_same_policy():
    from weighted_analysis_test_helpers import make_dpv_row
    rows = [
        core.enrich_with_comparison_objective(make_dpv_row("d1", surrogate_value=100.0, evaluator_value=10.0, runtime=1.0, weight_profile="homogeneous")),
        core.enrich_with_comparison_objective(make_dpv_row("d2", surrogate_value=100.0, evaluator_value=12.0, runtime=1.0, weight_profile="clustered")),
    ]
    stratified = summ.heuristic_dpv_summary(rows)
    aggregated = summ.heuristic_dpv_summary_aggregated(rows)
    check(len(stratified) == 2, "heuristic/DPV table is also profile-stratified by default")
    check(len(aggregated) == 1 and aggregated[0]["weight_profile"] == "ALL_PROFILES_AGGREGATED",
          "heuristic/DPV aggregated variant is explicit and labeled the same way")


def test_bold_marking_never_crosses_profile_boundary():
    """The 'best' value bolded in a LaTeX table must be computed WITHIN
    each weight_profile group, never globally across profiles."""
    rows = _rows()
    table = summ.exact_method_summary(rows)  # mean_gap_to_best_feasible == the single row's gap per (method, profile) cell
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "t.tex"
        tables.write_latex_table(
            path, table, ["method", "weight_profile", "mean_gap_to_best_feasible"],
            bold_column="mean_gap_to_best_feasible", bold_is_minimum=True,
            bold_within_columns=("weight_profile",),
        )
        text = path.read_text(encoding="utf-8")
        lines = [line for line in text.splitlines() if "&" in line and ("A &" in line or "B &" in line)]
        # Method A must be bolded under homogeneous (its own best there),
        # and method B must be bolded under heterogeneous (its own best
        # there) -- NOT the other way around, and not based on a single
        # global minimum across both profiles.
        a_homogeneous = next(l for l in lines if l.startswith("A ") and "homogeneous" in l and "heterogeneous" not in l)
        b_homogeneous = next(l for l in lines if l.startswith("B ") and "homogeneous" in l and "heterogeneous" not in l)
        a_heterogeneous = next(l for l in lines if l.startswith("A ") and "heterogeneous" in l)
        b_heterogeneous = next(l for l in lines if l.startswith("B ") and "heterogeneous" in l)
        check("\\textbf" in a_homogeneous, "method A's own best-in-profile value is bolded under homogeneous")
        check("\\textbf" not in b_homogeneous, "method B is not bolded under homogeneous (it is not the best there)")
        check("\\textbf" in b_heterogeneous, "method B's own best-in-profile value is bolded under heterogeneous")
        check("\\textbf" not in a_heterogeneous, "method A is not bolded under heterogeneous (it is not the best there)")


def main() -> int:
    test_default_is_profile_stratified()
    test_aggregated_variant_is_explicit_and_labeled()
    test_heuristic_dpv_summary_same_policy()
    test_bold_marking_never_crosses_profile_boundary()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
