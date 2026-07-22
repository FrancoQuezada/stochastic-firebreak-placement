#!/usr/bin/env python3
"""Phase 9A duplicate-detection tests. Covers mandatory cases:
  27.2 exact duplicate -> one canonical row + diagnostic count
  27.4 conflicting duplicate (same run_id+attempt, different objective/hash) -> quarantine

Usage: python3 scripts/test_weighted_result_duplicate_detection.py
"""

from __future__ import annotations

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import base_raw_row, check, report


def test_exact_duplicate():  # 27.2
    row = base_raw_row()
    records = [
        norm.normalize_row(row, source_file="a.csv", prefer_json=False),
        norm.normalize_row(row, source_file="a.csv", prefer_json=False),
    ]
    result = merge.build_merge_outputs(records)
    check(result["diagnostics"]["exact_duplicates"] == 1, "exact-duplicate count is reported in diagnostics")
    check(len(result["current_valid"]) == 1, "exact duplicates collapse to exactly one canonical row")
    check(len(result["all_attempts"]) == 2, "both attempts are still visible in merged_all_attempts")
    categories = {r["duplicate_category"] for r in records}
    check(categories == {schema.DUP_UNIQUE, schema.DUP_EXACT_DUPLICATE},
          "one record is kept unique, the other flagged exact_duplicate")


def test_conflicting_duplicate():  # 27.4
    row_a = base_raw_row(objective_in_sample="47.0")
    row_b = base_raw_row(objective_in_sample="99.0")  # same run_id + attempt, different content
    records = [
        norm.normalize_row(row_a, source_file="worker_a.csv", prefer_json=False),
        norm.normalize_row(row_b, source_file="worker_b.csv", prefer_json=False),
    ]
    result = merge.build_merge_outputs(records)
    check(result["diagnostics"]["conflicting_duplicates"] == 2,
          "both conflicting rows are counted (same run_id+attempt, different content)")
    check(all(r["validation_classification"] == schema.CONFLICTING_DUPLICATE for r in records),
          "conflicting duplicates are classified as conflicting_duplicate, not silently kept as valid")
    check(len(result["current_valid"]) == 0, "a conflicting duplicate is never selected as the current valid row")
    check(all(r in result["invalid"] for r in records),
          "conflicting duplicates are quarantined into the invalid artifact, never silently kept (first or last)")


def main() -> int:
    test_exact_duplicate()
    test_conflicting_duplicate()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
