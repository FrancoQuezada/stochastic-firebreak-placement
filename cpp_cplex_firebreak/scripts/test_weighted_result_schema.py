#!/usr/bin/env python3
"""Phase 9A unit tests for weighted_result_schema.py: field-list integrity,
strict numeric parsing, comparison-group keys, and the canonical status
filters. Pure in-memory; no solver binary required.

Usage: python3 scripts/test_weighted_result_schema.py
"""

from __future__ import annotations

import weighted_result_schema as schema
from weighted_result_schema import NumericParseError
from weighted_result_test_helpers import check, report


def test_field_list_integrity():
    names = [f.name for f in schema.CANONICAL_FIELDS]
    check(len(names) == len(set(names)), "CANONICAL_FIELDS has no duplicate names")
    check("result_schema_version" in names, "result_schema_version is a canonical field")
    check("solver_objective" in names and "dpv_surrogate_objective" in names,
          "solver_objective and dpv_surrogate_objective are both declared (objective-space separation)")
    check(tuple(names) == schema.CANONICAL_FIELD_ORDER, "CANONICAL_FIELD_ORDER matches declaration order")


def test_strict_numeric_parsing():
    check(schema.parse_numeric_strict("") is None, "blank parses to None, not 0.0")
    check(schema.parse_numeric_strict(None) is None, "None parses to None")
    check(schema.parse_numeric_strict("47.5") == 47.5, "well-formed float parses correctly")
    try:
        schema.parse_numeric_strict("not-a-number")
        check(False, "malformed numeric string must raise, not silently return 0.0")
    except NumericParseError:
        check(True, "malformed numeric string raises NumericParseError")
    try:
        schema.parse_numeric_strict("inf")
        check(False, "non-finite value must raise")
    except NumericParseError:
        check(True, "non-finite value raises NumericParseError")


def test_comparison_group_key():
    a = {"instance_id": "new20x20", "in_sample_scenario_ids": [1, 2, 3], "risk_measure": "expected",
         "alpha": 0.02, "cvar_beta": 0.9, "mean_cvar_lambda": 1.0, "weight_map_hash": "h1", "budget": 1,
         "dpv_surrogate_objective": None}
    b = dict(a)
    c = dict(a, weight_map_hash="h2")
    d = dict(a, dpv_surrogate_objective=123.0)

    check(schema.comparison_group_key(a) == schema.comparison_group_key(b),
          "identical configs produce the same comparison-group key")
    check(schema.comparison_group_key(a) != schema.comparison_group_key(c),
          "different weight_map_hash produces a different comparison-group key")
    check(schema.comparison_group_is_valid([a, b]), "same-key, same-objective-space group is valid")
    check(not schema.comparison_group_is_valid([a, c]), "mixed weight_map_hash group is invalid")
    check(not schema.comparison_group_is_valid([a, d]),
          "mixed FPP-exact / DPV-surrogate objective space group is invalid")


def test_status_filters():
    exact_valid = {"dpv_surrogate_objective": None, "solver_status": "Optimal", "objective_validation_passed": True}
    exact_invalid = {"dpv_surrogate_objective": None, "solver_status": "Optimal", "objective_validation_passed": False}
    heuristic_valid = {"execution_status": "heuristic_completed", "weighted_fpp_expected_in_sample": 161.75}
    dpv_valid = {"dpv_surrogate_objective": 2444.0, "solver_status": "Feasible",
                 "weighted_fpp_expected_in_sample": 270.625}

    check(schema.is_valid_exact_result(exact_valid), "terminal-status, validated FPP-exact row is valid")
    check(not schema.is_valid_exact_result(exact_invalid), "objective_validation_passed=False fails an FPP-exact row")
    check(schema.is_valid_heuristic_result(heuristic_valid), "heuristic_completed + evaluator value is valid")
    check(not schema.is_valid_exact_result(heuristic_valid), "a heuristic row is never a valid exact result")
    check(schema.is_valid_dpv_result(dpv_valid), "DPV surrogate row with evaluator value is valid")
    check(not schema.is_valid_dpv_result(exact_valid), "an FPP-exact row (no surrogate) is never a valid DPV result")


def main() -> int:
    test_field_list_integrity()
    test_strict_numeric_parsing()
    test_comparison_group_key()
    test_status_filters()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
