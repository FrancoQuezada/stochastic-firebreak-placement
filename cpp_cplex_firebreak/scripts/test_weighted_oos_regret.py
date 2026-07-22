#!/usr/bin/env python3
"""Phase 9B out-of-sample observed-regret tests. Covers mandatory case:
  32.10 OOS best observed (correct terminology and calculation)

Usage: python3 scripts/test_weighted_oos_regret.py
"""

from __future__ import annotations

import weighted_analysis_core as core
from weighted_analysis_test_helpers import check, make_exact_row, report


def test_best_observed_and_regret():  # 32.10
    rows = [
        core.enrich_with_comparison_objective(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0, method="FPP-SAA",
                                                              weighted_fpp_expected_out_of_sample=160.0)),
        core.enrich_with_comparison_objective(make_exact_row("r2", value=47.0, best_bound=47.0, runtime=1.0, method="FPP-Branch-Benders-Combinatorial",
                                                              weighted_fpp_expected_out_of_sample=150.0)),
    ]
    best = core.best_observed(rows, core.oos_metric_field)
    check(best["best_observed_value"] == 150.0, "the minimum OOS value among the compatible group is the best-observed value")
    check(best["best_observed_method"] == "FPP-Branch-Benders-Combinatorial", "the best-observed method is correctly identified")

    regret_a = core.regret_to_best_observed(160.0, best["best_observed_value"])
    regret_b = core.regret_to_best_observed(150.0, best["best_observed_value"])
    check(regret_a > 0, "a worse OOS value has positive regret to best observed")
    check(regret_b == 0.0, "the best-observed row itself has zero regret")
    check(abs(regret_a - 10.0 / 150.0) < 1e-12, "regret is computed relative to the best-observed value, minimization convention")


def test_no_exact_reference_required_for_oos():
    """Best-observed OOS pools ALL method types (unlike best-known-feasible,
    which is exact-only) -- this is deliberate (section 13)."""
    from weighted_analysis_test_helpers import make_dpv_row, make_heuristic_row
    rows = [
        core.enrich_with_comparison_objective(make_dpv_row("r1", surrogate_value=1000.0, evaluator_value=5.0, runtime=1.0,
                                                            weighted_fpp_expected_out_of_sample=140.0)),
        core.enrich_with_comparison_objective(make_heuristic_row("r2", evaluator_value=3.0, runtime=0.1,
                                                                  weighted_fpp_expected_out_of_sample=130.0)),
    ]
    best = core.best_observed(rows, core.oos_metric_field)
    check(best["best_observed_value"] == 130.0, "a heuristic row can define the best-observed OOS value (never called optimal)")


def main() -> int:
    test_best_observed_and_regret()
    test_no_exact_reference_required_for_oos()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
