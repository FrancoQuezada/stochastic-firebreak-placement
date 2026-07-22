#!/usr/bin/env python3
"""Phase 9A comparison-group-key tests (section 23). These validate the
Phase 9B best-known-value comparison-group key builder itself -- Phase 9A
does not compute best-known values or performance profiles.

Usage: python3 scripts/test_weighted_comparison_group_keys.py
"""

from __future__ import annotations

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import base_raw_row, check, report


def _record(**overrides) -> dict:
    return norm.normalize_row(base_raw_row(**overrides), prefer_json=False)


def test_same_group_different_methods_is_valid():
    a = _record(run_id="run_a", method="FPP-SAA")
    b = _record(run_id="run_b", method="FPP-Branch-Benders-Combinatorial")
    check(schema.comparison_group_key(a) == schema.comparison_group_key(b),
          "two methods solved on the identical instance/split/risk/weight-map/budget share one comparison-group key")
    check(schema.comparison_group_is_valid([a, b]), "same-key rows across methods are a valid comparison group")


def test_mixed_map_hash_is_invalid():
    a = _record(run_id="run_a", weight_map_hash="fnv1a64:h1", optimization_weight_map_hash="fnv1a64:h1",
                out_of_sample_weight_map_hash="fnv1a64:h1", paired_reburn_weight_map_hash="fnv1a64:h1")
    b = _record(run_id="run_b", weight_map_hash="fnv1a64:h2", optimization_weight_map_hash="fnv1a64:h2",
                out_of_sample_weight_map_hash="fnv1a64:h2", paired_reburn_weight_map_hash="fnv1a64:h2")
    check(not schema.comparison_group_is_valid([a, b]), "never compare rows solved under different weight maps")


def test_mixed_risk_config_is_invalid():
    a = _record(run_id="run_a", risk_measure="expected", cvar_beta="0.9")
    b = _record(run_id="run_b", risk_measure="cvar", cvar_beta="0.95")
    check(not schema.comparison_group_is_valid([a, b]), "never compare rows solved under different risk configurations")


def test_mixed_budget_is_invalid():
    a = _record(run_id="run_a", budget="1")
    b = _record(run_id="run_b", budget="2")
    check(not schema.comparison_group_is_valid([a, b]), "never compare rows solved under different budgets")


def test_mixed_objective_space_is_invalid():
    fpp_exact = _record(run_id="run_a")
    dpv_surrogate = _record(
        run_id="run_b", method="DPV-SAA", dpv_surrogate_objective="11385.5",
        objective_validation_passed="false",
    )
    check(not schema.comparison_group_is_valid([fpp_exact, dpv_surrogate]),
          "never compare an FPP-exact objective against a DPV surrogate objective, even within the same group key")


def test_empty_and_singleton_groups_are_trivially_valid():
    check(schema.comparison_group_is_valid([]), "an empty group is trivially valid")
    check(schema.comparison_group_is_valid([_record()]), "a singleton group is trivially valid")


def main() -> int:
    test_same_group_different_methods_is_valid()
    test_mixed_map_hash_is_invalid()
    test_mixed_risk_config_is_invalid()
    test_mixed_budget_is_invalid()
    test_mixed_objective_space_is_invalid()
    test_empty_and_singleton_groups_are_trivially_valid()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
