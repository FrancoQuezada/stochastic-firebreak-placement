#!/usr/bin/env python3
"""Phase 9B per-row gap computation tests. Covers mandatory case:
  32.6 negative numeric noise (small negative -> zero; material negative ->
       flagged, never silently normal)

Usage: python3 scripts/test_weighted_gap_computation.py
"""

from __future__ import annotations

import weighted_analysis_core as core
from weighted_analysis_test_helpers import check, report


def test_noise_tolerance_zeroes_small_negative():
    gap, flagged = core.gap_to_best_known_feasible(46.999999, 47.0, noise_tolerance=1.0e-4)
    check(gap == 0.0, "a value fractionally below best-known within tolerance reports gap 0.0, not a negative sliver")
    check(not flagged, "noise-tolerance zeroing is not flagged as a material negative gap")


def test_material_negative_gap_is_flagged_not_hidden():
    gap, flagged = core.gap_to_best_known_feasible(40.0, 47.0, noise_tolerance=1.0e-4)
    check(gap is not None and gap < 0, "a materially better value than best-known produces a negative raw gap")
    check(abs(gap - (40.0 - 47.0) / 47.0) < 1e-12, "the raw gap value itself is preserved exactly, not rounded to zero")
    check(flagged, "a materially negative gap is explicitly flagged, never silently reported as ordinary")


def test_positive_gap_unaffected():
    gap, flagged = core.gap_to_best_known_feasible(50.0, 47.0)
    check(gap > 0, "a worse value produces a positive gap")
    check(not flagged, "a positive gap is never flagged as negative noise")


def test_missing_inputs_yield_none():
    gap, flagged = core.gap_to_best_known_feasible(None, 47.0)
    check(gap is None and not flagged, "a missing row value yields gap=None, not zero or an exception")
    gap2, flagged2 = core.gap_to_best_known_feasible(47.0, None)
    check(gap2 is None and not flagged2, "a missing best-known reference yields gap=None (section 10: 'keep missing when no exact reference exists')")


def test_gap_to_lower_bound_uses_own_magnitude_denominator():
    gap = core.gap_to_best_known_lower_bound(100.0, 90.0)
    check(abs(gap - 10.0 / 100.0) < 1e-12, "gap-to-LB normalizes by the row's OWN value, not the LB (section 11 formula)")


def main() -> int:
    test_noise_tolerance_zeroes_small_negative()
    test_material_negative_gap_is_flagged_not_hidden()
    test_positive_gap_unaffected()
    test_missing_inputs_yield_none()
    test_gap_to_lower_bound_uses_own_magnitude_denominator()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
