#!/usr/bin/env python3
"""Phase 9B statistical-comparison tests. Covers mandatory cases:
  32.17 statistical fallback (sign test when Wilcoxon is unavailable/inappropriate)
  32.18 Holm correction (adjusted p-values)

Usage: python3 scripts/test_weighted_statistical_comparisons.py
"""

from __future__ import annotations

import weighted_analysis_statistics as stats
from weighted_analysis_test_helpers import check, report


def test_sign_test_exact_binomial():
    # 6 nonzero differences, all positive -> two-sided exact binomial p-value
    # for k=0 successes out of 6 at p=0.5: 2 * C(6,0) / 2^6 = 2/64 = 0.03125.
    statistic, p_value = stats.sign_test([1, 1, 1, 1, 1, 1])
    check(statistic == 6, "the sign-test statistic counts all-positive differences correctly")
    check(abs(p_value - 2 / 64) < 1e-12, "the sign test computes the exact two-sided binomial p-value with no dependency")


def test_sign_test_handles_zero_differences():
    statistic, p_value = stats.sign_test([0, 0, 0])
    check(p_value == 1.0, "all-zero differences yield a trivial p-value of 1.0, never a crash")


def test_fallback_used_when_scipy_unavailable():  # 32.17
    original = stats.SCIPY_AVAILABLE
    try:
        stats.SCIPY_AVAILABLE = False
        result = stats.paired_comparison(
            [10.5, 12.1, 9.4, 11.7, 15.2, 13.9], [11.2, 13.9, 10.1, 12.9, 14.0, 12.3],
            method_a="A", method_b="B", metric="m", min_pairs=6,
        )
        check(result["test_name"] == "sign_test", "with SciPy unavailable, the deterministic sign-test fallback is used")
        check(result["raw_p_value"] is not None, "the fallback still produces a usable p-value")
    finally:
        stats.SCIPY_AVAILABLE = original


def test_wilcoxon_used_when_appropriate_and_available():
    if not stats.SCIPY_AVAILABLE:
        check(True, "SciPy not installed in this environment; fallback-only path already covered above")
        return
    result = stats.paired_comparison(
        [10.5, 12.1, 9.4, 11.7, 15.2, 13.9, 14.3, 10.1], [11.2, 13.9, 10.1, 12.9, 14.0, 12.3, 15.9, 11.8],
        method_a="A", method_b="B", metric="m", min_pairs=6,
    )
    check(result["test_name"] == "wilcoxon_signed_rank", "Wilcoxon is used when SciPy is available and assumptions are met")


def test_insufficient_pairs_are_skipped_not_run():
    result = stats.paired_comparison([1, 2], [2, 3], method_a="A", method_b="B", metric="m", min_pairs=6)
    check(result["skipped"], "fewer paired observations than the minimum are skipped, never run anyway")
    check(result["test_name"] is None and result["raw_p_value"] is None, "a skipped comparison reports no test result")
    check("6" in result["skip_reason"], "the skip reason names the required minimum")


def test_holm_correction_adjusts_p_values():  # 32.18
    comparisons = [
        {"raw_p_value": 0.01, "skipped": False},
        {"raw_p_value": 0.04, "skipped": False},
        {"raw_p_value": 0.03, "skipped": False},
        {"raw_p_value": 0.001, "skipped": False},
    ]
    adjusted = stats.holm_correct_family(comparisons)
    # Sorted raw p-values: 0.001, 0.01, 0.03, 0.04 -> multipliers 4,3,2,1
    # step-up: max(0.004), max(0.004,0.03)=0.03, max(0.03,0.06)=0.06->1... capped at 1.0
    check(abs(adjusted[3]["adjusted_p_value"] - 0.004) < 1e-12, "the smallest raw p-value is multiplied by m (Holm step 1)")
    check(adjusted[0]["adjusted_p_value"] >= adjusted[3]["adjusted_p_value"], "adjusted p-values are monotonically non-decreasing with rank")
    check(all(c["adjusted_p_value"] <= 1.0 for c in adjusted), "adjusted p-values are capped at 1.0")


def test_holm_correction_skips_skipped_comparisons():
    comparisons = [
        {"raw_p_value": 0.01, "skipped": False},
        {"raw_p_value": None, "skipped": True},
    ]
    adjusted = stats.holm_correct_family(comparisons)
    check(adjusted[1].get("adjusted_p_value") is None, "a skipped comparison is never given a fabricated adjusted p-value")
    check(adjusted[0]["adjusted_p_value"] == 0.01, "with only one testable comparison in the family, Holm reduces to the raw p-value")


def main() -> int:
    test_sign_test_exact_binomial()
    test_sign_test_handles_zero_differences()
    test_fallback_used_when_scipy_unavailable()
    test_wilcoxon_used_when_appropriate_and_available()
    test_insufficient_pairs_are_skipped_not_run()
    test_holm_correction_adjusts_p_values()
    test_holm_correction_skips_skipped_comparisons()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
