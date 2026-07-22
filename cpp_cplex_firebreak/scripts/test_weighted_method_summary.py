#!/usr/bin/env python3
"""Phase 9B method-summary and physical-metric tests. Covers mandatory
cases:
  32.12 physical metrics (verify separate aggregation from weighted loss)
  32.13 retry history (only current-valid attempts enter scientific summaries)

Usage: python3 scripts/test_weighted_method_summary.py
"""

from __future__ import annotations

import weighted_analysis_core as core
import weighted_analysis_summaries as summ
import weighted_result_merge as merge
import weighted_result_normalize as norm
from weighted_analysis_test_helpers import check, make_exact_row, report
from weighted_result_test_helpers import base_raw_row


def test_physical_metrics_separate_from_weighted_loss():  # 32.12
    rows = [
        core.enrich_with_comparison_objective(make_exact_row(
            "r1", value=47.0, best_bound=47.0, runtime=1.0,
            mean_burned_cells_in_sample=47.0, mean_burned_cells_out_of_sample=149.25,
        )),
    ]
    physical = summ.physical_metric_summary(rows)
    check(len(physical) == 1, "one physical-metric summary row is produced")
    row = physical[0]
    check(row["mean_burned_cells_in_sample"] == 47.0, "physical burned-cell metric reported")
    check("mean_gap_to_best_known_feasible" not in row, "the physical summary never mixes in weighted-loss gap columns")

    method_rows = summ.method_summary(rows)
    check("mean_burned_cells_in_sample" not in method_rows[0],
          "the weighted-loss method summary never mixes in raw physical burned-cell columns "
          "(they live in the separate physical_metric_summary table)")


def test_ranking_can_differ_between_weighted_and_physical():
    """A method may rank differently under weighted loss vs. burned-cell
    count -- both rankings must be independently reconstructable."""
    a = core.enrich_with_comparison_objective(make_exact_row(
        "a", value=100.0, best_bound=100.0, runtime=1.0, method="FPP-SAA",
        mean_burned_cells_in_sample=10.0,
    ))
    b = core.enrich_with_comparison_objective(make_exact_row(
        "b", value=50.0, best_bound=50.0, runtime=1.0, method="FPP-Branch-Benders-Combinatorial",
        mean_burned_cells_in_sample=20.0,
    ))
    check(a["fpp_comparison_objective_in_sample"] > b["fpp_comparison_objective_in_sample"],
          "method b has the better (lower) weighted loss")
    check(a["mean_burned_cells_in_sample"] < b["mean_burned_cells_in_sample"],
          "method a has the better (lower) physical burned-cell count -- the opposite ranking, preserved as such")


def test_retry_history_only_current_valid_enters_summaries():  # 32.13
    attempt1 = base_raw_row(attempt="1", worker_return_code="1", solver_status="", status="", execution_status="")
    attempt2 = base_raw_row(attempt="2", worker_return_code="0", solver_status="Optimal")
    records = [
        norm.normalize_row(attempt1, prefer_json=False),
        norm.normalize_row(attempt2, prefer_json=False),
    ]
    merge_result = merge.build_merge_outputs(records)
    check(len(merge_result["current_valid"]) == 1, "Phase 9A selects exactly one current-valid row for this logical run")

    analysis_rows = [core.enrich_with_comparison_objective(r) for r in merge_result["current_valid"]]
    method_rows = summ.method_summary(analysis_rows)
    check(sum(r["instances_total"] for r in method_rows) == 1,
          "Phase 9B's method summary counts exactly one instance for this logical run, "
          "never one per retry attempt")


def main() -> int:
    test_physical_metrics_separate_from_weighted_loss()
    test_ranking_can_differ_between_weighted_and_physical()
    test_retry_history_only_current_valid_enters_summaries()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
