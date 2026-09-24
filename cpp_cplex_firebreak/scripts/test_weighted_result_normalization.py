#!/usr/bin/env python3
"""Phase 9A normalization tests. Covers mandatory cases:
  27.1  modern valid row normalization
  27.8  DPV surrogate distinction (surrogate objective doesn't populate FPP objective fields)
  27.9  heuristic status (NotApplicable solver status accepted with successful heuristic execution)
  27.11 malformed numeric value -> rejection not zero coercion
  27.14 same logical run across CSV and JSON (prefer JSON, detect duplicate representation)

Usage: python3 scripts/test_weighted_result_normalization.py
"""

from __future__ import annotations

import tempfile
from pathlib import Path

import weighted_result_normalize as norm
from weighted_result_test_helpers import base_raw_row, check, make_paired_json, report


def test_modern_valid_row_normalization():  # 27.1
    row = base_raw_row()
    record = norm.normalize_row(row, source_file="worker.csv", source_format="worker_csv", prefer_json=False)
    check(record["result_schema_version"] == "weighted-result-9a.1", "modern row stamped with current schema version")
    check(record["migration_status"] == "modern", "modern row is not flagged as legacy")
    check(record["method"] == "FPP-SAA", "method copied through")
    check(record["solver_objective"] == 47.0, "solver_objective populated for an FPP-exact row")
    check(record["dpv_surrogate_objective"] is None, "dpv_surrogate_objective is blank for an FPP-exact row")
    check(record["weighted_fpp_expected_in_sample"] == 47.0, "weighted_fpp_expected_in_sample populated")
    check(record["mean_burned_cells_in_sample"] == 47.0, "mean_burned_cells_in_sample populated (physical metric)")
    check(record["train_ids"] == [1, 2, 3, 4, 5, 6, 7, 8], "train_ids parsed as a list of ints")
    check(not record["_numeric_errors"], "no numeric errors on a well-formed row")


def test_dpv_surrogate_distinction():  # 27.8
    row = base_raw_row(
        method="DPV-SAA", method_family="DPV-SAA",
        objective_in_sample="11385.5", best_bound="11385.5", mip_gap="0.0",
        dpv_surrogate_objective="11385.5", dpv_surrogate_best_bound="11385.5", dpv_surrogate_gap="0.0",
        solver_status="Feasible", objective_validation_passed="false",
        train_expected_weighted_burn_loss="270.625", train_weighted_cvar="300.0",
    )
    record = norm.normalize_row(row, prefer_json=False)
    check(record["dpv_surrogate_objective"] == 11385.5, "dpv_surrogate_objective is populated for a DPV row")
    check(record["solver_objective"] is None,
          "solver_objective must NOT be populated from a DPV surrogate solve (never copy surrogate into FPP fields)")
    check(record["weighted_fpp_expected_in_sample"] == 270.625,
          "the exact weighted-FPP evaluator value is independent of the DPV surrogate objective")
    check(record["weighted_fpp_expected_in_sample"] != record["dpv_surrogate_objective"],
          "weighted_fpp_expected_in_sample is never equal to the surrogate objective by construction here")


def test_heuristic_status():  # 27.9
    row = base_raw_row(
        method="Static-DPV", method_family="Static-DPV",
        solver_status="NotApplicable", execution_status="heuristic_completed",
        objective_in_sample="2444.0", dpv_surrogate_objective="2444.0",
        objective_validation_passed="false",
        train_expected_weighted_burn_loss="161.75",
    )
    record = norm.normalize_row(row, prefer_json=False)
    check(record["solver_status"] == "NotApplicable", "heuristic row reports solver_status=NotApplicable, never 'optimal'")
    check(record["execution_status"] == "heuristic_completed", "heuristic row reports execution_status=heuristic_completed")
    check(record["weighted_fpp_expected_in_sample"] == 161.75, "heuristic row still carries an exact evaluator value")


def test_malformed_numeric_rejected():  # 27.11
    row = base_raw_row(objective_in_sample="not-a-number")
    record = norm.normalize_row(row, prefer_json=False)
    check(record["solver_objective"] is None, "malformed numeric value is not coerced to a number")
    check(any("solver_objective" in e or "not-a-number" in e for e in record["_numeric_errors"]),
          "malformed numeric value is reported as a numeric error, not silently dropped")
    check(record["solver_objective"] != 0.0, "malformed numeric value is never silently coerced to zero")


def test_prefer_json_for_paired_reburn():  # 27.14
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        solver_json_path, _ = make_paired_json(
            tmp_path, expected_weighted_burn_loss=999.0, weighted_cvar=1234.0, weight_map_hash="fnv1a64:abc12345",
        )
        row = base_raw_row(output_json=solver_json_path)
        record_with_json = norm.normalize_row(row, prefer_json=True)
        record_without_json = norm.normalize_row(row, prefer_json=False)

        check(record_with_json["weighted_fpp_expected_paired_reburn"] == 999.0,
              "prefer_json=True recovers weighted_fpp_expected_paired_reburn from the paired-reburn-eval JSON "
              "(no CSV column carries this value today)")
        check(record_with_json["weighted_fpp_cvar_paired_reburn"] == 1234.0,
              "prefer_json=True recovers weighted_fpp_cvar_paired_reburn from JSON")
        check(record_without_json["weighted_fpp_expected_paired_reburn"] is None,
              "without JSON, the paired weighted-FPP fields are honestly blank rather than fabricated from the "
              "unweighted paired_reburn_train_expected_burned_area column")


def main() -> int:
    test_modern_valid_row_normalization()
    test_dpv_surrogate_distinction()
    test_heuristic_status()
    test_malformed_numeric_rejected()
    test_prefer_json_for_paired_reburn()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
