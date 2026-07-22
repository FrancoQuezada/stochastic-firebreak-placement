#!/usr/bin/env python3
"""Phase 9B best-known-feasible-value tests. Covers mandatory cases:
  32.1 exact best-known selection
  32.2 DPV exclusion
  32.3 heuristic exclusion
  32.7 different map hash -> separate groups
  32.8 different train split -> separate groups
  32.9 different risk configuration -> separate groups

Usage: python3 scripts/test_weighted_best_known_values.py
"""

from __future__ import annotations

import weighted_analysis_core as core
from weighted_analysis_test_helpers import check, make_dpv_row, make_exact_row, make_heuristic_row, report


def test_exact_best_known_selection():  # 32.1
    rows = [
        core.enrich_with_comparison_objective(make_exact_row("r1", value=50.0, best_bound=50.0, runtime=1.0, method="FPP-SAA")),
        core.enrich_with_comparison_objective(make_exact_row("r2", value=47.0, best_bound=47.0, runtime=2.0, method="FPP-Branch-Benders-Combinatorial")),
        core.enrich_with_comparison_objective(make_exact_row("r3", value=55.0, best_bound=45.0, runtime=1.5, method="FPP-Benders")),
    ]
    bk = core.best_known_feasible(rows)
    check(bk["best_known_feasible_value"] == 47.0, "the minimum valid FPP value across several exact methods is selected")
    check(bk["best_known_feasible_method"] == "FPP-Branch-Benders-Combinatorial", "the winning method is correctly identified")
    check(bk["best_known_feasible_source_count"] == 3, "all three exact rows are counted as eligible sources")


def test_dpv_exclusion():  # 32.2
    rows = [
        core.enrich_with_comparison_objective(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0)),
        core.enrich_with_comparison_objective(make_dpv_row("r2", surrogate_value=5.0, evaluator_value=1.0, runtime=0.5)),
    ]
    check(rows[1]["objective_space"] == core.OBJECTIVE_SPACE_DPV_OPTIMIZATION, "the DPV row is classified as dpv_optimization")
    bk = core.best_known_feasible(rows)
    check(bk["best_known_feasible_value"] == 47.0,
          "a numerically smaller DPV surrogate/evaluator value never defines the FPP best-known reference")
    check(bk["best_known_feasible_method"] == "FPP-SAA", "only the exact-FPP row defines the reference")


def test_heuristic_exclusion():  # 32.3
    rows = [
        core.enrich_with_comparison_objective(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0)),
        core.enrich_with_comparison_objective(make_heuristic_row("r2", evaluator_value=1.0, runtime=0.1)),
    ]
    check(rows[1]["objective_space"] == core.OBJECTIVE_SPACE_HEURISTIC, "the heuristic row is classified as heuristic")
    bk = core.best_known_feasible(rows)
    check(bk["best_known_feasible_value"] == 47.0,
          "a numerically smaller heuristic evaluator value never defines the FPP best-known reference")
    gap, flagged = core.gap_to_best_known_feasible(1.0, bk["best_known_feasible_value"])
    check(gap is not None, "the heuristic row still receives a gap relative to the (unaffected) best-known reference")
    check(gap < 0 and flagged, "a materially better heuristic value is preserved and flagged, never silently hidden")


def test_different_map_hash_separate_groups():  # 32.7
    a = core.enrich_with_comparison_objective(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0, weight_map_hash="fnv1a64:h1"))
    b = core.enrich_with_comparison_objective(make_exact_row("r2", value=40.0, best_bound=40.0, runtime=1.0, weight_map_hash="fnv1a64:h2"))
    groups = core.build_comparison_groups([a, b])
    check(len(groups) == 2, "different weight_map_hash values produce separate comparison groups")
    for group_records in groups.values():
        bk = core.best_known_feasible(group_records)
        check(bk["best_known_feasible_source_count"] == 1, "each group's best-known is computed from only its own rows")


def test_different_train_split_separate_groups():  # 32.8
    a = core.enrich_with_comparison_objective(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0, in_sample_scenario_ids=[1, 2, 3]))
    b = core.enrich_with_comparison_objective(make_exact_row("r2", value=40.0, best_bound=40.0, runtime=1.0, in_sample_scenario_ids=[4, 5, 6]))
    groups = core.build_comparison_groups([a, b])
    check(len(groups) == 2, "different train scenario IDs produce separate comparison groups")


def test_different_risk_configuration_separate_groups():  # 32.9
    a = core.enrich_with_comparison_objective(make_exact_row("r1", value=47.0, best_bound=47.0, runtime=1.0, risk_measure="expected"))
    b = core.enrich_with_comparison_objective(make_exact_row("r2", value=47.0, best_bound=47.0, runtime=1.0, risk_measure="cvar", cvar_beta=0.95,
                                                              weighted_fpp_cvar_in_sample=100.0))
    groups = core.build_comparison_groups([a, b])
    check(len(groups) == 2, "different risk_measure/cvar_beta configurations produce separate comparison groups")
    check(b["fpp_comparison_objective_in_sample_source"] == "weighted_fpp_cvar_in_sample",
          "a cvar-risk row selects the cvar comparison metric, not the expected-value one")


def main() -> int:
    test_exact_best_known_selection()
    test_dpv_exclusion()
    test_heuristic_exclusion()
    test_different_map_hash_separate_groups()
    test_different_train_split_separate_groups()
    test_different_risk_configuration_separate_groups()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
