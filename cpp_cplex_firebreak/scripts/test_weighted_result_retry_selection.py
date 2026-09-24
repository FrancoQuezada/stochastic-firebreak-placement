#!/usr/bin/env python3
"""Phase 9A retry-selection test. Covers mandatory case:
  27.3 retry selection (attempt 1 failed, attempt 2 succeeded -> attempt 2 selected)

Usage: python3 scripts/test_weighted_result_retry_selection.py
"""

from __future__ import annotations

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import base_raw_row, check, report


def test_retry_selection():
    attempt1 = base_raw_row(
        attempt="1", worker_return_code="1", solver_status="", status="", execution_status="",
        worker_finished_at_epoch="1010.0",
    )
    attempt2 = base_raw_row(
        attempt="2", worker_return_code="0", solver_status="Optimal",
        worker_finished_at_epoch="2020.0",
    )
    records = [
        norm.normalize_row(attempt1, source_file="attempt1.csv", prefer_json=False),
        norm.normalize_row(attempt2, source_file="attempt2.csv", prefer_json=False),
    ]
    result = merge.build_merge_outputs(records)

    check(records[0]["validation_classification"] == schema.INCOMPLETE,
          "the failed attempt (return_code=1, no solver_status) is classified incomplete")
    check(records[1]["validation_classification"] == schema.VALID, "the succeeded attempt is classified valid")
    check(len(result["current_valid"]) == 1, "exactly one current-valid row is selected for this logical run")
    check(result["current_valid"][0]["attempt"] == 2, "attempt 2 (the latest valid terminal attempt) is selected")
    check(len(result["all_attempts"]) == 2, "both attempts remain visible in merged_all_attempts (retry history preserved)")
    check(records[0] in result["invalid"], "the failed attempt still appears in merged_invalid with a reason")
    check(result["diagnostics"]["retry_attempts"] == 2, "retry_attempts diagnostic counts both attempts in the group")


def main() -> int:
    test_retry_selection()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
