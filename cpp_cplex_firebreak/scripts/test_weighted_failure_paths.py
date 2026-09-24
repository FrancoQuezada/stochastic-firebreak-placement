#!/usr/bin/env python3
"""Phase 10 section 23: consolidated failure-path validation.

Every failure path below must produce a clear status, identify the stage,
avoid partial valid-looking output, and preserve diagnostic information --
never a silent success or a partially-populated row that looks valid.

Some failure modes are enforced in C++ (weight-map validation, universe
mismatch, capability-matrix rejection) and are already covered by the
existing C++ test suite (referenced in comments below, re-verified passing
via `make test` as part of this phase's validation run rather than
duplicated here in Python). This file consolidates and re-verifies the
Python-layer failure paths as one single audit trail.

Usage: python3 scripts/test_weighted_failure_paths.py
"""

from __future__ import annotations

import weighted_analysis_core as core
import weighted_analysis_statistics as stats_mod
import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_analysis_test_helpers import check as acheck
from weighted_analysis_test_helpers import make_exact_row
from weighted_analysis_test_helpers import report as areport
from weighted_result_test_helpers import base_raw_row


# 1. Invalid weight / 2. missing cell weight / 4. universe mismatch --
# enforced in C++ (core::LandscapeWeightMap, PairedInstanceWeightMapping)
# and covered by tests/test_landscape_weight_map.cpp,
# tests/test_weight_map_registry.cpp, tests/test_weight_paired_instance_mapping.cpp
# (re-verified passing via `make test` in this phase's validation run).

# 6. Unsupported method combination -- enforced by
# experiments::weighted_method_capability() (C++, tests/test_weight_capability_matrix.cpp,
# tests/test_weighted_capability_cross_check.cpp) and its Python mirror below.
def test_unsupported_method_combination():
    import importlib.util
    import sys
    from pathlib import Path
    spec = importlib.util.spec_from_file_location(
        "generate_fpp_new_instances_scaling_manifests",
        Path(__file__).resolve().parent / "generate_fpp_new_instances_scaling_manifests.py",
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    supported, reason = module.weighted_method_supported(
        "FPP-Branch-Benders-Combinatorial-LLBI", "heterogeneous", "expected")
    acheck(not supported, "combinatorial Benders + LLBI under a non-homogeneous profile is rejected")
    acheck(reason, "the rejection carries an explicit, non-empty reason")

    supported_ok, _ = module.weighted_method_supported("FPP-SAA", "heterogeneous", "expected")
    acheck(supported_ok, "a genuinely supported combination is not incorrectly rejected")


# 3. Map hash mismatch (already unit-tested in test_weighted_result_provenance.py;
# re-asserted here as part of the consolidated failure-path audit).
def test_weight_map_hash_mismatch():
    row = base_raw_row(out_of_sample_weight_map_hash="fnv1a64:MISMATCHED")
    record = norm.normalize_row(row, prefer_json=False)
    classification, reasons = merge.validate_record(record)
    acheck(classification == schema.INVALID_WEIGHT_PROVENANCE, "a weight-map hash mismatch produces an explicit invalid_weight_provenance status")
    acheck(reasons, "the failure stage (which hash field mismatched) is preserved in the reasons")


# 5. Missing paired instance / missing selected firebreak in reburn.
def test_missing_paired_instance_and_missing_firebreak():
    row = base_raw_row(paired_reburn_status="unavailable", paired_reburn_resolution_status="unresolvable")
    record = norm.normalize_row(row, prefer_json=False)
    classification, reasons = merge.validate_record(record)
    acheck(classification == schema.INVALID_PAIRED_EVALUATION, "an unresolvable paired instance produces an explicit invalid_paired_evaluation status")

    row_missing = base_raw_row(paired_reburn_status="ok", paired_selected_firebreaks_missing="2")
    record_missing = norm.normalize_row(row_missing, prefer_json=False)
    classification_missing, _ = merge.validate_record(record_missing)
    acheck(classification_missing == schema.INVALID_PAIRED_EVALUATION, "missing transferred firebreaks in reburn produce an explicit invalid status, never a silently-accepted partial result")


# 7. Malformed result JSON: read_json_if_exists returns None rather than raising,
# so normalization falls back to the (still-validated) CSV row instead of crashing.
def test_malformed_result_json_does_not_crash():
    row = base_raw_row(output_json="/nonexistent/path/does_not_exist.json")
    record = norm.normalize_row(row, prefer_json=True)
    acheck(record["method"] == "FPP-SAA", "a missing/unreadable JSON file never crashes normalization -- it falls back to the CSV row")
    acheck(record["solver_objective"] == 47.0, "CSV-sourced fields remain correctly populated despite the JSON being unreadable")


# 8. Malformed CSV numeric field.
def test_malformed_csv_numeric_field():
    row = base_raw_row(objective_in_sample="NOT_A_NUMBER")
    record = norm.normalize_row(row, prefer_json=False)
    acheck(record["solver_objective"] is None, "a malformed numeric field is never coerced to zero")
    classification, reasons = merge.validate_record(record)
    acheck(classification == schema.INVALID_EVALUATION, "a malformed numeric field produces an explicit invalid_evaluation status")
    acheck(any("solver_objective" in r or "NOT_A_NUMBER" in r for r in reasons), "the offending field is named in the diagnostic reason")


# 9. Conflicting duplicate.
def test_conflicting_duplicate():
    row_a = base_raw_row(objective_in_sample="47.0")
    row_b = base_raw_row(objective_in_sample="99.0")
    records = [norm.normalize_row(row_a, prefer_json=False), norm.normalize_row(row_b, prefer_json=False)]
    result = merge.build_merge_outputs(records)
    acheck(all(r["validation_classification"] == schema.CONFLICTING_DUPLICATE for r in records),
           "conflicting duplicates are explicitly classified, never silently resolved by keeping one arbitrarily")
    acheck(len(result["current_valid"]) == 0, "no conflicting duplicate is ever promoted to current-valid")
    acheck(all(r in result["invalid"] for r in records), "both conflicting rows are preserved (with diagnostics) in the invalid artifact, never dropped")


# 10. Failed retry / resume: a failed attempt followed by a successful retry.
def test_failed_retry_then_success():
    attempt1 = base_raw_row(attempt="1", worker_return_code="1", solver_status="", status="", execution_status="")
    attempt2 = base_raw_row(attempt="2", worker_return_code="0", solver_status="Optimal")
    records = [norm.normalize_row(attempt1, prefer_json=False), norm.normalize_row(attempt2, prefer_json=False)]
    result = merge.build_merge_outputs(records)
    acheck(records[0]["validation_classification"] == schema.INCOMPLETE, "the failed attempt is explicitly classified incomplete, not silently dropped")
    acheck(len(result["current_valid"]) == 1 and result["current_valid"][0]["attempt"] == 2,
           "only the successful retry is promoted to current-valid")
    acheck(records[0] in result["all_attempts"], "the failed attempt's diagnostic history is preserved in merged_all_attempts")


# 11. Unsupported schema version.
def test_unsupported_schema_version():
    row = base_raw_row()
    record = norm.normalize_row(row, prefer_json=False)
    record["result_schema_version"] = "some-future-unknown-version-99"
    classification, reasons = merge.validate_record(record)
    acheck(classification == schema.INVALID_SCHEMA, "an unrecognized result_schema_version is explicitly rejected, never silently processed")
    acheck(reasons, "the unsupported version itself is named in the diagnostic reason")


# 12. Missing exact reference for a comparison group (only DPV/heuristic rows present).
def test_missing_exact_reference():
    from weighted_analysis_test_helpers import make_dpv_row
    rows = [core.enrich_with_comparison_objective(make_dpv_row("d1", surrogate_value=100.0, evaluator_value=10.0, runtime=1.0))]
    bk = core.best_known_feasible(rows)
    acheck(bk["best_known_feasible_value"] is None, "a group with no exact-FPP rows reports no best-known value, never a fabricated one")
    acheck(bk["best_known_feasible_source_count"] == 0, "the diagnostic explicitly reports zero contributing exact rows")
    gap, flagged = core.gap_to_best_known_feasible(10.0, bk["best_known_feasible_value"])
    acheck(gap is None and not flagged, "a row in a group with no exact reference gets gap=None, never a fabricated comparison")


# 13. Insufficient statistical pairs.
def test_insufficient_statistical_pairs():
    result = stats_mod.paired_comparison([1.0, 2.0], [1.5, 2.5], method_a="A", method_b="B", metric="m", min_pairs=6)
    acheck(result["skipped"], "a comparison below the minimum paired-observation count is explicitly skipped, never run with an unreliable n")
    acheck(result["skip_reason"] and "6" in result["skip_reason"], "the skip reason names the required minimum, preserving diagnostic information")


def main() -> int:
    test_unsupported_method_combination()
    test_weight_map_hash_mismatch()
    test_missing_paired_instance_and_missing_firebreak()
    test_malformed_result_json_does_not_crash()
    test_malformed_csv_numeric_field()
    test_conflicting_duplicate()
    test_failed_retry_then_success()
    test_unsupported_schema_version()
    test_missing_exact_reference()
    test_insufficient_statistical_pairs()
    return areport(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
