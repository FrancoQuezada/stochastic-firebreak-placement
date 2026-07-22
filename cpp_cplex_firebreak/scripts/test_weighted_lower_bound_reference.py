#!/usr/bin/env python3
"""Phase 9B best-known lower bound and certification-gap tests. Covers
mandatory cases:
  32.4 best lower bound (maximum valid exact lower bound)
  32.5 certification gap (certified and uncertified cases)

Usage: python3 scripts/test_weighted_lower_bound_reference.py
"""

from __future__ import annotations

import weighted_analysis_core as core
from weighted_analysis_test_helpers import check, make_dpv_row, make_exact_row, report


def test_maximum_valid_lower_bound():  # 32.4
    rows = [
        core.enrich_with_comparison_objective(make_exact_row("r1", value=50.0, best_bound=40.0, runtime=1.0)),
        core.enrich_with_comparison_objective(make_exact_row("r2", value=48.0, best_bound=45.0, runtime=1.0)),
        core.enrich_with_comparison_objective(make_exact_row("r3", value=52.0, best_bound=42.0, runtime=1.0)),
    ]
    lb = core.best_known_lower_bound(rows)
    check(lb["best_known_lower_bound"] == 45.0, "the maximum (tightest) valid exact lower bound across methods is selected")
    check(lb["best_known_lower_bound_source_count"] == 3, "all three exact rows contribute to the lower-bound search")


def test_dpv_bound_excluded():
    rows = [
        core.enrich_with_comparison_objective(make_exact_row("r1", value=50.0, best_bound=40.0, runtime=1.0)),
        core.enrich_with_comparison_objective(make_dpv_row("r2", surrogate_value=1000.0, evaluator_value=1.0, runtime=0.1)),
    ]
    lb = core.best_known_lower_bound(rows)
    check(lb["best_known_lower_bound"] == 40.0, "a DPV surrogate bound is never mistaken for an original-FPP lower bound")
    check(lb["best_known_lower_bound_source_count"] == 1, "only the exact row contributes")


def test_certified_optimal_case():  # 32.5
    cert = core.certification_gap(47.0, 47.0, optimality_tolerance=1.0e-4)
    check(cert["best_known_certification_gap"] == 0.0, "a matching best-known value and lower bound yield a zero certification gap")
    check(cert["best_known_certified_optimal"], "a zero-gap group is certified optimal")


def test_uncertified_case():
    cert = core.certification_gap(50.0, 40.0, optimality_tolerance=1.0e-4)
    check(cert["best_known_certification_gap"] > 1.0e-4, "a wide bound-to-feasible gap is not within tolerance")
    check(not cert["best_known_certified_optimal"], "an uncertified group is never labeled optimal")


def test_proven_optimal_overrides_gap():
    cert = core.certification_gap(50.0, 40.0, optimality_tolerance=1.0e-4, proven_optimal=True)
    check(cert["best_known_certified_optimal"],
          "an explicit solver Optimal status certifies the group even if the reported bound gap looks wide")


def test_bound_consistency_validation():
    ok, reason = core.validate_bound_consistency(47.0, 47.0)
    check(ok, "LB == best-known passes the LB <= best-known + tolerance check")
    ok2, reason2 = core.validate_bound_consistency(47.0, 50.0)
    check(not ok2, "a lower bound exceeding the best-known feasible value by more than tolerance fails validation")
    check(reason2 is not None and "exceeds" in reason2, "the violation reason names the inconsistency")


def main() -> int:
    test_maximum_valid_lower_bound()
    test_dpv_bound_excluded()
    test_certified_optimal_case()
    test_uncertified_case()
    test_proven_optimal_overrides_gap()
    test_bound_consistency_validation()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
