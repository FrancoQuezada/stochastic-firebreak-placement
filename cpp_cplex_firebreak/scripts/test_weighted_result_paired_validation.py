#!/usr/bin/env python3
"""Phase 9A paired-reburn validation tests. Covers mandatory cases:
  27.6 paired missing firebreak -> paired-invalid
  27.7 OOS vs paired distinction (metrics not overwritten)

Usage: python3 scripts/test_weighted_result_paired_validation.py
"""

from __future__ import annotations

import tempfile
from pathlib import Path

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import base_raw_row, check, make_paired_json, report


def test_paired_missing_firebreak():  # 27.6
    with tempfile.TemporaryDirectory() as tmp:
        solver_json_path, _ = make_paired_json(Path(tmp))
        row = base_raw_row(
            output_json=solver_json_path, paired_evaluation_enabled="true", paired_reburn_status="ok",
            paired_selected_firebreaks_missing="1", paired_selected_mapping_status="missing_cells",
        )
        record = norm.normalize_row(row, prefer_json=True)
        classification, reasons = merge.validate_record(record)
        check(classification == schema.INVALID_PAIRED_EVALUATION,
              "a nonzero paired_selected_firebreaks_missing invalidates the paired evaluation")
        check(any("missing" in r for r in reasons), "the rejection reason names the missing-firebreak condition")

        clean_row = base_raw_row(output_json=solver_json_path, paired_evaluation_enabled="true", paired_reburn_status="ok")
        clean_record = norm.normalize_row(clean_row, prefer_json=True)
        clean_classification, _ = merge.validate_record(clean_record)
        check(clean_classification == schema.VALID, "zero missing firebreaks + successful paired eval validates cleanly")


def test_oos_vs_paired_distinction():  # 27.7
    with tempfile.TemporaryDirectory() as tmp:
        solver_json_path, _ = make_paired_json(
            Path(tmp), expected_weighted_burn_loss=295.125, weighted_cvar=392.0,
        )
        row = base_raw_row(
            output_json=solver_json_path, paired_evaluation_enabled="true",
            test_expected_weighted_burn_loss="149.25", test_weighted_cvar="213.0",
        )
        record = norm.normalize_row(row, prefer_json=True)
        check(record["weighted_fpp_expected_out_of_sample"] == 149.25, "OOS expected value is the OOS-namespace one")
        check(record["weighted_fpp_expected_paired_reburn"] == 295.125,
              "paired-reburn expected value is the paired-namespace one, distinct from OOS")
        check(record["weighted_fpp_expected_out_of_sample"] != record["weighted_fpp_expected_paired_reburn"],
              "the paired-reburn evaluation never overwrites the standard out-of-sample metrics")
        check(record["weighted_fpp_cvar_out_of_sample"] == 213.0, "OOS cvar untouched by paired evaluation")
        check(record["weighted_fpp_cvar_paired_reburn"] == 392.0, "paired cvar is its own independent value")


def main() -> int:
    test_paired_missing_firebreak()
    test_oos_vs_paired_distinction()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
