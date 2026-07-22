#!/usr/bin/env python3
"""Phase 9A provenance tests. Covers mandatory cases:
  27.5  weight mismatch -> invalid
  27.12 different weight replicates remain separate logical groups
  27.13 different train splits remain separate groups

Usage: python3 scripts/test_weighted_result_provenance.py
"""

from __future__ import annotations

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import base_raw_row, check, report


def test_weight_hash_mismatch():  # 27.5
    row = base_raw_row(out_of_sample_weight_map_hash="fnv1a64:DIFFERENT")
    record = norm.normalize_row(row, prefer_json=False)
    classification, reasons = merge.validate_record(record)
    check(classification == schema.INVALID_WEIGHT_PROVENANCE,
          "a mismatched out_of_sample_weight_map_hash is rejected as invalid_weight_provenance")
    check(any("out_of_sample_weight_map_hash" in r for r in reasons), "the mismatch reason names the offending field")

    ok_row = base_raw_row()
    ok_record = norm.normalize_row(ok_row, prefer_json=False)
    ok_classification, _ = merge.validate_record(ok_record)
    check(ok_classification == schema.VALID, "matching weight hashes across stages validate cleanly")


def test_different_replicates_stay_separate():  # 27.12
    row_r0 = base_raw_row(
        run_id="run_wr0", weight_replicate="0", weight_map_hash="fnv1a64:replicate0",
        optimization_weight_map_hash="fnv1a64:replicate0", out_of_sample_weight_map_hash="fnv1a64:replicate0",
        paired_reburn_weight_map_hash="fnv1a64:replicate0",
    )
    row_r1 = base_raw_row(
        run_id="run_wr1", weight_replicate="1", weight_map_hash="fnv1a64:replicate1",
        optimization_weight_map_hash="fnv1a64:replicate1", out_of_sample_weight_map_hash="fnv1a64:replicate1",
        paired_reburn_weight_map_hash="fnv1a64:replicate1",
    )
    records = [norm.normalize_row(row_r0, prefer_json=False), norm.normalize_row(row_r1, prefer_json=False)]
    result = merge.build_merge_outputs(records)
    check(result["diagnostics"]["logical_runs"] == 2,
          "two different weight replicates form two distinct logical runs, never pooled")
    check(len(result["current_valid"]) == 2, "both replicate rows survive independently into merged_current_valid")
    check(not schema.comparison_group_is_valid(records),
          "different weight_map_hash values (one per replicate) fail the comparison-group validity check")


def test_different_train_splits_stay_separate():  # 27.13
    row_split_a = base_raw_row(run_id="run_split_a", train_ids="1,2,3,4,5,6,7,8")
    row_split_b = base_raw_row(run_id="run_split_b", train_ids="9,10,11,12,13,14,15,16")
    records = [norm.normalize_row(row_split_a, prefer_json=False), norm.normalize_row(row_split_b, prefer_json=False)]
    result = merge.build_merge_outputs(records)
    check(result["diagnostics"]["logical_runs"] == 2,
          "two different train splits form two distinct logical runs, never pooled")
    check(not schema.comparison_group_is_valid(records),
          "different train_scenario_ids fail the comparison-group validity check (never aggregate across splits)")


def main() -> int:
    test_weight_hash_mismatch()
    test_different_replicates_stay_separate()
    test_different_train_splits_stay_separate()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
