#!/usr/bin/env python3
"""Phase 10 section 11: final regression tests for the paired-evaluation
gating fix (the real signal is `paired_reburn_evaluation_status`, never the
unreliable `paired_evaluation_enabled` flag -- see
docs/WEIGHTED_LANDSCAPES_PHASE9B.md section 1.2). Verifies the fix is
applied consistently across all three entry points that touch paired
provenance: normalization (weighted_result_normalize), merge validation
(weighted_result_merge / weighted_result_schema), and analysis
(weighted_analysis_core).

Usage: python3 scripts/test_weighted_paired_gate_regression.py
"""

from __future__ import annotations

import tempfile
from pathlib import Path

import weighted_analysis_core as core
import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_analysis_test_helpers import check as acheck
from weighted_analysis_test_helpers import report as areport
from weighted_result_test_helpers import base_raw_row, make_paired_json


def test_paired_not_attempted_never_requires_paired_fields():
    """paired-disabled/not-attempted rows do not require paired fields."""
    row = base_raw_row()  # default paired_reburn_status="n/a"
    record = norm.normalize_row(row, prefer_json=False)
    # "n/a" is itself one of the shared is_missing() sentinel tokens, so it
    # normalizes to "" through the same first_value() fallback chain every
    # other optional field uses -- both "" and "n/a" mean "not attempted"
    # to the gate (weighted_result_merge._paired_evaluation_attempted).
    acheck(record["paired_reburn_evaluation_status"] == "", "default fixture row normalizes to paired-not-attempted")
    classification, reasons = merge.validate_record(record)
    acheck(classification == schema.VALID, "a paired-not-attempted row validates without needing any paired field")
    acheck(not any("paired" in r for r in reasons), "no paired-related reasons are attached to a not-attempted row")

    analysis_row = core.enrich_with_comparison_objective(record)
    acheck(not core.paired_eligible(analysis_row), "a not-attempted row is never paired-eligible in analysis")


def test_paired_attempted_requires_success():
    """paired-enabled (attempted) rows require paired success."""
    row = base_raw_row(paired_reburn_status="failed")
    record = norm.normalize_row(row, prefer_json=False)
    classification, reasons = merge.validate_record(record)
    acheck(classification == schema.INVALID_PAIRED_EVALUATION, "a failed paired-reburn attempt is rejected, not silently accepted")
    acheck(any("!= 'ok'" in r for r in reasons), "the rejection reason names the non-ok status")


def test_paired_missing_firebreaks_invalid():
    """paired-enabled rows with missing firebreaks are invalid."""
    with tempfile.TemporaryDirectory() as tmp:
        solver_json_path, _ = make_paired_json(Path(tmp))
        row = base_raw_row(output_json=solver_json_path, paired_reburn_status="ok",
                            paired_selected_firebreaks_missing="3")
        record = norm.normalize_row(row, prefer_json=True)
        classification, _ = merge.validate_record(record)
        acheck(classification == schema.INVALID_PAIRED_EVALUATION, "nonzero missing firebreaks invalidates the row")


def test_paired_map_mismatch_invalid():
    """paired-enabled rows with a weight-map hash mismatch are invalid."""
    with tempfile.TemporaryDirectory() as tmp:
        solver_json_path, _ = make_paired_json(Path(tmp))
        row = base_raw_row(output_json=solver_json_path, paired_reburn_status="ok",
                            paired_reburn_weight_map_hash="fnv1a64:DIFFERENT_HASH")
        record = norm.normalize_row(row, prefer_json=True)
        classification, reasons = merge.validate_record(record)
        acheck(classification == schema.INVALID_WEIGHT_PROVENANCE, "a paired weight-map hash mismatch is rejected")
        acheck(any("paired_reburn_weight_map_hash" in r for r in reasons), "the reason names the mismatched field")


def test_oos_success_does_not_imply_paired_success():
    """OOS success does not imply paired success."""
    row = base_raw_row(paired_reburn_status="unavailable",
                        weighted_fpp_expected_out_of_sample="149.25", weighted_fpp_cvar_out_of_sample="213.0")
    record = norm.normalize_row(row, prefer_json=False)
    acheck(record["weighted_fpp_expected_out_of_sample"] == 149.25, "OOS metrics are present and valid on their own")
    classification, _ = merge.validate_record(record)
    acheck(classification == schema.INVALID_PAIRED_EVALUATION,
           "a row with valid OOS data but an 'unavailable' paired attempt is still rejected on paired grounds -- "
           "OOS validity never substitutes for paired validity")


def test_paired_success_never_overwrites_oos_fields():
    """paired success does not overwrite OOS fields (also covered in Phase
    9A, re-verified here as part of the final regression pass)."""
    with tempfile.TemporaryDirectory() as tmp:
        solver_json_path, _ = make_paired_json(
            Path(tmp), expected_weighted_burn_loss=295.125, weighted_cvar=392.0,
        )
        row = base_raw_row(output_json=solver_json_path, paired_reburn_status="ok",
                            weighted_fpp_expected_out_of_sample="149.25", weighted_fpp_cvar_out_of_sample="213.0")
        record = norm.normalize_row(row, prefer_json=True)
        classification, _ = merge.validate_record(record)
        acheck(classification == schema.VALID, "a fully successful paired row (matching hashes, zero missing) validates cleanly")
        acheck(record["weighted_fpp_expected_out_of_sample"] == 149.25, "OOS expected value is untouched by the paired evaluation")
        acheck(record["weighted_fpp_expected_paired_reburn"] == 295.125, "paired expected value remains independently correct")
        acheck(record["weighted_fpp_expected_out_of_sample"] != record["weighted_fpp_expected_paired_reburn"],
               "the two namespaces never collapse into the same value by the merge validator's own accounting")

        analysis_row = core.enrich_with_comparison_objective(record)
        acheck(core.paired_eligible(analysis_row), "a fully successful paired row is paired-eligible in the analysis layer")
        acheck(analysis_row["weighted_fpp_expected_out_of_sample"] == 149.25,
               "the analysis layer's enrichment step also never overwrites OOS with paired data")


def main() -> int:
    test_paired_not_attempted_never_requires_paired_fields()
    test_paired_attempted_requires_success()
    test_paired_missing_firebreaks_invalid()
    test_paired_map_mismatch_invalid()
    test_oos_success_does_not_imply_paired_success()
    test_paired_success_never_overwrites_oos_fields()
    return areport(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
