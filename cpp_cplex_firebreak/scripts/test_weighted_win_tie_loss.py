#!/usr/bin/env python3
"""Phase 9B win/tie/loss tests. Covers mandatory case:
  32.16 win/tie/loss tolerance (deterministic counts)

Usage: python3 scripts/test_weighted_win_tie_loss.py
"""

from __future__ import annotations

import weighted_analysis_core as core
import weighted_analysis_summaries as summ
import weighted_result_schema as schema
from weighted_analysis_test_helpers import check, make_exact_row, report


def _rows():
    specs = [
        ("g1", 100.0, 100.0),  # tie
        ("g2", 100.0, 99.0),   # b wins (lower)
        ("g3", 100.0, 101.0),  # a wins (lower)
        ("g4", 100.0, 100.0 + 1e-9),  # within tolerance -> tie
    ]
    rows = []
    for group, va, vb in specs:
        rows.append(core.enrich_with_comparison_objective(make_exact_row(
            f"{group}_a", value=va, best_bound=va, runtime=1.0, method="FPP-SAA",
            instance_id=group, canonical_landscape_id=group,
        )))
        rows.append(core.enrich_with_comparison_objective(make_exact_row(
            f"{group}_b", value=vb, best_bound=vb, runtime=1.0, method="FPP-Branch-Benders-Combinatorial",
            instance_id=group, canonical_landscape_id=group,
        )))
    return rows


def test_win_tie_loss_counts_and_tolerance():  # 32.16
    rows = _rows()
    results = summ.win_tie_loss(
        rows, metric="in_sample_weighted_fpp_objective",
        group_key_fn=lambda r: schema.comparison_group_key(r),
        metric_field_fn=lambda r: r.get("fpp_comparison_objective_in_sample"),
        tolerance=1.0e-6,
    )
    check(len(results) == 1, "exactly one method pair is compared")
    result = results[0]
    check(result["paired_observations"] == 4, "all four groups contribute a paired observation")
    check(result["ties"] == 2, "one exact tie and one within-tolerance near-tie both count as ties")
    check(result["method_a_wins"] == 1, "FPP-SAA wins exactly once (g3, where it has the lower value)")
    check(result["method_b_wins"] == 1, "FPP-Branch-Benders-Combinatorial wins exactly once (g2)")
    check(result["method_a_wins"] + result["ties"] + result["method_b_wins"] == result["paired_observations"],
          "wins + ties + losses always sum to the paired observation count")


def test_missing_rows_never_counted_as_win_or_loss():
    rows = _rows()
    # Remove method B's row from one group entirely (simulating a method
    # that never attempted that instance).
    rows = [r for r in rows if not (r["method"] == "FPP-Branch-Benders-Combinatorial" and r["instance_id"] == "g1")]
    results = summ.win_tie_loss(
        rows, metric="in_sample_weighted_fpp_objective",
        group_key_fn=lambda r: schema.comparison_group_key(r),
        metric_field_fn=lambda r: r.get("fpp_comparison_objective_in_sample"),
        tolerance=1.0e-6,
    )
    check(results[0]["paired_observations"] == 3, "a group missing one method's row is excluded, not counted as a win or a loss")


def main() -> int:
    test_win_tie_loss_counts_and_tolerance()
    test_missing_rows_never_counted_as_win_or_loss()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
