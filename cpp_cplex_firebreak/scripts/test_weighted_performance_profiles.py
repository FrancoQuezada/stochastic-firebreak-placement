#!/usr/bin/env python3
"""Phase 9B runtime performance-profile tests. Covers mandatory cases:
  32.14 missing method row (unsolved treatment where the row was expected)
  32.15 time-limit exact row (documented profile treatment)
  32.19 performance-profile ratio (exact manual ratios and curve points)

Usage: python3 scripts/test_weighted_performance_profiles.py
"""

from __future__ import annotations

import math

import weighted_analysis_core as core
import weighted_analysis_profiles as prof
from weighted_analysis_test_helpers import check, make_exact_row, report


def _enrich(rows):
    return [core.enrich_with_comparison_objective(r) for r in rows]


def test_performance_profile_ratio_values():  # 32.19
    current_valid = _enrich([
        make_exact_row("g1_a", value=100.0, best_bound=100.0, runtime=1.0, method="FPP-SAA", instance_id="g1"),
        make_exact_row("g1_b", value=100.0, best_bound=100.0, runtime=2.0, method="FPP-Branch-Benders-Combinatorial", instance_id="g1"),
        make_exact_row("g2_a", value=100.0, best_bound=100.0, runtime=4.0, method="FPP-SAA", instance_id="g2"),
        make_exact_row("g2_b", value=100.0, best_bound=100.0, runtime=1.0, method="FPP-Branch-Benders-Combinatorial", instance_id="g2"),
    ])
    profiles = prof.build_runtime_performance_profiles(current_valid, [])
    stratum = ("homogeneous", "expected")
    check(stratum in profiles, "the (weight_profile, risk_measure) stratum is present")
    ratios = profiles[stratum]["ratios_by_method"]
    check(abs(ratios["FPP-SAA"][0] - 1.0) < 1e-12, "the fastest method in a group has ratio exactly 1.0")
    check(abs(ratios["FPP-Branch-Benders-Combinatorial"][0] - 2.0) < 1e-12, "a 2x-slower method has ratio exactly 2.0")
    check(abs(ratios["FPP-SAA"][1] - 4.0) < 1e-12, "ratios are computed independently per problem group")


def test_profile_points_curve():
    x_values, y_values = prof.profile_points([1.0, 2.0, 4.0, math.inf], x_max=8.0)
    check(x_values[0] == 1.0, "the curve always starts at tau=1.0")
    check(abs(y_values[0] - 0.25) < 1e-12, "rho(1.0) = fraction solved at ratio exactly 1 (1 of 4 instances)")
    check(abs(y_values[-1] - 0.75) < 1e-12, "the curve plateaus at the fraction of instances ever solved (3 of 4; the inf case never counts)")


def test_time_limit_exact_row_documented_treatment():  # 32.15
    current_valid = _enrich([
        make_exact_row("g1_a", value=100.0, best_bound=90.0, runtime=300.0, method="FPP-SAA",
                        instance_id="g1", solver_status="TimeLimit"),
    ])
    profiles = prof.build_runtime_performance_profiles(current_valid, [], success_criterion="optimal")
    stratum = ("homogeneous", "expected")
    ratios = profiles[stratum]["ratios_by_method"]["FPP-SAA"]
    check(not math.isfinite(ratios[0]), "under the 'optimal' success criterion, a time-limited (non-optimal) row counts as unsolved")

    profiles_tol = prof.build_runtime_performance_profiles(
        [core.enrich_with_comparison_objective(dict(r, gap_to_best_known_feasible=0.005)) for r in [
            make_exact_row("g1_a", value=100.0, best_bound=90.0, runtime=300.0, method="FPP-SAA",
                            instance_id="g1", solver_status="TimeLimit")
        ]],
        [], success_criterion="feasible_tolerance", feasible_tolerance=0.01,
    )
    ratios_tol = profiles_tol[stratum]["ratios_by_method"]["FPP-SAA"]
    check(math.isfinite(ratios_tol[0]),
          "under the 'feasible_tolerance' success criterion, a time-limited row within the gap tolerance counts as solved")


def test_missing_method_row_counts_as_unsolved():  # 32.14
    current_valid = _enrich([
        make_exact_row("g1_a", value=100.0, best_bound=100.0, runtime=1.0, method="FPP-SAA", instance_id="g1"),
        make_exact_row("g1_b", value=100.0, best_bound=100.0, runtime=2.0, method="FPP-Branch-Benders-Combinatorial", instance_id="g1"),
        # g2: FPP-Branch-Benders-Combinatorial never even attempted this group.
        make_exact_row("g2_a", value=100.0, best_bound=100.0, runtime=1.0, method="FPP-SAA", instance_id="g2"),
    ])
    profiles = prof.build_runtime_performance_profiles(current_valid, [])
    stratum = ("homogeneous", "expected")
    ratios = profiles[stratum]["ratios_by_method"]["FPP-Branch-Benders-Combinatorial"]
    check(len(ratios) == 2, "the missing-row group still contributes an entry to the method's ratio series")
    check(not math.isfinite(ratios[1]), "a method row missing entirely for an expected group counts as unsolved (ratio=inf), never dropped")


def main() -> int:
    test_performance_profile_ratio_values()
    test_profile_points_curve()
    test_time_limit_exact_row_documented_treatment()
    test_missing_method_row_counts_as_unsolved()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
