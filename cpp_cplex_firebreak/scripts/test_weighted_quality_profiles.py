#!/usr/bin/env python3
"""Phase 9B quality-profile tests. Covers mandatory case:
  32.20 quality-profile ratio (exact manual calculations)

Usage: python3 scripts/test_weighted_quality_profiles.py
"""

from __future__ import annotations

import weighted_analysis_core as core
import weighted_analysis_profiles as prof
from weighted_analysis_test_helpers import check, make_dpv_row, make_exact_row, report


def test_quality_ratio_formula():  # 32.20
    dpv = core.enrich_with_comparison_objective(make_dpv_row("d1", surrogate_value=1000.0, evaluator_value=61.1, runtime=0.5, instance_id="g1"))

    # Directly attach the gap this test wants to check (isolating the
    # quality-ratio formula itself: q = 1 + max(0, gap), independent of how
    # run_core_analysis computed it).
    dpv["gap_to_best_known_feasible"] = (61.1 - 47.0) / 47.0
    profiles = prof.build_quality_profiles([dpv], namespace="insample")
    stratum = ("homogeneous", "expected")
    ratio = profiles[stratum]["ratios_by_method"]["DPV-SAA"][0]
    expected_ratio = 1.0 + max(0.0, (61.1 - 47.0) / 47.0)
    check(abs(ratio - expected_ratio) < 1e-12, "quality ratio equals exactly 1 + max(0, gap)")
    check(ratio > 1.0, "a positive gap always yields a quality ratio strictly greater than 1")


def test_quality_ratio_never_below_one():
    """Even a row with a (flagged) negative gap must not produce q < 1 --
    the max(0, gap) clamp is load-bearing."""
    row = core.enrich_with_comparison_objective(make_dpv_row("d1", surrogate_value=1000.0, evaluator_value=10.0, runtime=0.5))
    row["gap_to_best_known_feasible"] = -0.5  # materially better than best-known (flagged elsewhere)
    profiles = prof.build_quality_profiles([row], namespace="insample")
    stratum = ("homogeneous", "expected")
    ratio = profiles[stratum]["ratios_by_method"]["DPV-SAA"][0]
    check(ratio == 1.0, "a negative gap is clamped to zero inside the quality ratio, never producing q < 1")


def test_exact_fpp_rows_excluded_from_quality_profile():
    exact = core.enrich_with_comparison_objective(make_exact_row("e1", value=47.0, best_bound=47.0, runtime=1.0))
    exact["gap_to_best_known_feasible"] = 0.0
    profiles = prof.build_quality_profiles([exact], namespace="insample")
    check(profiles == {}, "quality profiles are for approximate/DPV/heuristic methods, never exact_fpp rows")


def main() -> int:
    test_quality_ratio_formula()
    test_quality_ratio_never_below_one()
    test_exact_fpp_rows_excluded_from_quality_profile()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
