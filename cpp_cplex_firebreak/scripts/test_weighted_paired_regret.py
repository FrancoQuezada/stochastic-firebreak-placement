#!/usr/bin/env python3
"""Phase 9B paired-reburn observed-regret tests. Covers mandatory case:
  32.11 paired reburn separation (paired metrics never populate OOS results)

Usage: python3 scripts/test_weighted_paired_regret.py
"""

from __future__ import annotations

import weighted_analysis_core as core
from weighted_analysis_test_helpers import check, make_exact_row, report


def test_paired_regret_independent_of_oos():  # 32.11
    row = core.enrich_with_comparison_objective(make_exact_row(
        "r1", value=47.0, best_bound=47.0, runtime=1.0,
        weighted_fpp_expected_out_of_sample=150.0, weighted_fpp_expected_paired_reburn=300.0,
    ))
    check(row["weighted_fpp_expected_out_of_sample"] == 150.0, "OOS value is independent")
    check(row["weighted_fpp_expected_paired_reburn"] == 300.0, "paired-reburn value is independent, not equal to OOS by coincidence of this test")
    check(row["weighted_fpp_expected_out_of_sample"] != row["weighted_fpp_expected_paired_reburn"],
          "paired-reburn and OOS values are never conflated into one field")


def test_paired_eligibility_gating():
    eligible = core.paired_eligible(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0,
                                                    paired_reburn_evaluation_status="ok", paired_selected_firebreaks_missing=0))
    check(eligible, "a row with status=ok and zero missing firebreaks is paired-eligible")

    ineligible_missing = core.paired_eligible(make_exact_row("r2", value=47.0, best_bound=47.0, runtime=1.0,
                                                              paired_reburn_evaluation_status="ok", paired_selected_firebreaks_missing=2))
    check(not ineligible_missing, "a row with missing transferred firebreaks is never paired-eligible")

    ineligible_status = core.paired_eligible(make_exact_row("r3", value=47.0, best_bound=47.0, runtime=1.0,
                                                             paired_reburn_evaluation_status="failed"))
    check(not ineligible_status, "a row whose paired evaluation failed is never paired-eligible")


def test_best_observed_paired_uses_only_eligible_rows():
    eligible_row = core.enrich_with_comparison_objective(make_exact_row(
        "r1", value=47.0, best_bound=47.0, runtime=1.0, weighted_fpp_expected_paired_reburn=200.0,
        paired_reburn_evaluation_status="ok", paired_selected_firebreaks_missing=0,
    ))
    ineligible_row = core.enrich_with_comparison_objective(make_exact_row(
        "r2", value=47.0, best_bound=47.0, runtime=1.0, weighted_fpp_expected_paired_reburn=1.0,
        method="FPP-Branch-Benders-Combinatorial", paired_reburn_evaluation_status="failed",
    ))
    candidates = [r for r in [eligible_row, ineligible_row] if core.paired_eligible(r)]
    check(len(candidates) == 1, "only the paired-eligible row is a candidate for best-observed-paired")
    best = core.best_observed(candidates, core.paired_metric_field)
    check(best["best_observed_value"] == 200.0,
          "a numerically smaller paired value from an INELIGIBLE row never wins best-observed-paired")


def main() -> int:
    test_paired_regret_independent_of_oos()
    test_paired_eligibility_gating()
    test_best_observed_paired_uses_only_eligible_rows()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
