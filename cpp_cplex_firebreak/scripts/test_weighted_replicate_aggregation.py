#!/usr/bin/env python3
"""Phase 10 section 12: replicate-level and replicate-aggregated summary
tests, using a synthetic multi-instance, multi-replicate fixture (the real
Phase 8B/9A fixture only has one replicate, so Phase 9B deferred this).

Fixture: 2 physical instances x 2 weight replicates per profile x 2 methods
(one exact_fpp, one dpv_optimization), all under the same generator
configuration, with distinct weight_map_hash per (instance, replicate).

Usage: python3 scripts/test_weighted_replicate_aggregation.py
"""

from __future__ import annotations

import weighted_analysis_summaries as summ
from weighted_analysis_test_helpers import check, make_dpv_row, make_exact_row, report


def _build_fixture() -> list:
    rows = []
    instances = ["new20x20", "new30x30"]
    replicates = [0, 1]
    # Deterministic per-(instance, replicate) objective values so the
    # expected mean/std are easy to hand-verify.
    exact_values = {
        ("new20x20", 0): 100.0, ("new20x20", 1): 104.0,
        ("new30x30", 0): 200.0, ("new30x30", 1): 208.0,
    }
    dpv_values = {
        ("new20x20", 0): 130.0, ("new20x20", 1): 134.0,
        ("new30x30", 0): 260.0, ("new30x30", 1): 268.0,
    }
    for instance in instances:
        for replicate in replicates:
            map_hash = f"fnv1a64:{instance}_r{replicate}"
            landscape_id = f"{instance}__canonical"
            exact_value = exact_values[(instance, replicate)]
            rows.append(make_exact_row(
                f"{instance}_r{replicate}_saa", value=exact_value, best_bound=exact_value, runtime=1.0,
                method="FPP-SAA", weight_profile="heterogeneous", weight_replicate=replicate,
                weight_map_hash=map_hash, instance_id=instance, canonical_landscape_id=landscape_id,
            ))
            dpv_value = dpv_values[(instance, replicate)]
            rows.append(make_dpv_row(
                f"{instance}_r{replicate}_dpv", surrogate_value=dpv_value * 20, evaluator_value=dpv_value, runtime=0.5,
                weight_profile="heterogeneous", weight_replicate=replicate,
                weight_map_hash=map_hash, instance_id=instance, canonical_landscape_id=landscape_id,
            ))
    return rows


def test_replicate_level_preserves_full_granularity():
    rows = _build_fixture()
    # Uses a field the synthetic rows carry directly (weighted_fpp_expected_in_sample)
    # rather than fpp_comparison_objective_in_sample, which only exists after
    # enrich_with_comparison_objective() -- these raw rows haven't gone through that step.
    table = summ.replicate_level_method_summary(rows, metric_field="weighted_fpp_expected_in_sample", metric_name="objective")
    check(len(table) == 8, "one replicate-level row per (method, instance, replicate) = 2 methods x 2 instances x 2 replicates = 8")
    map_hashes = {row["weight_map_hash"] for row in table}
    # 4 distinct (instance, replicate) map hashes -- both methods within one
    # cell legitimately share the same map, since the map is per-instance/
    # replicate, not per-method.
    check(len(map_hashes) == 4, "every replicate-level row preserves its own distinct raw weight_map_hash, never averaged")
    saa_new20x20_r0 = next(r for r in table if r["method"] == "FPP-SAA" and r["canonical_landscape_id"] == "new20x20__canonical" and r["weight_replicate"] == 0)
    check(saa_new20x20_r0["mean_metric"] == 100.0, "replicate-level mean_metric matches the single observation for that cell")
    check(saa_new20x20_r0["observations"] == 1, "one observation per (method, instance, replicate) cell in this fixture")


def test_replicate_aggregated_reports_required_columns():
    rows = _build_fixture()
    table = summ.replicate_aggregated_method_summary(rows, metric_field="weighted_fpp_expected_in_sample", metric_name="objective")
    required_columns = {"weight_profile", "method", "risk_measure", "physical_landscapes", "weight_replicates",
                         "observations", "metric", "mean_metric", "median_metric", "between_replicate_std"}
    check(required_columns.issubset(table[0].keys()), "every required replicate-aggregated column is present")
    check(len(table) == 2, "one aggregated row per method (FPP-SAA, DPV-SAA), pooling both instances and both replicates")

    saa_row = next(r for r in table if r["method"] == "FPP-SAA")
    check(saa_row["physical_landscapes"] == 2, "both physical instances are counted")
    check(saa_row["weight_replicates"] == 2, "both weight replicates are counted")
    check(saa_row["observations"] == 4, "4 total observations (2 instances x 2 replicates)")
    check(abs(saa_row["mean_metric"] - (100.0 + 104.0 + 200.0 + 208.0) / 4) < 1e-9,
          "mean_metric is the mean across all pooled observations")


def test_between_replicate_std_is_between_not_within():
    """between_replicate_std must reflect variability BETWEEN replicate
    means, not the raw pooled standard deviation across every observation."""
    rows = _build_fixture()
    table = summ.replicate_aggregated_method_summary(rows, metric_field="weighted_fpp_expected_in_sample", metric_name="objective")
    saa_row = next(r for r in table if r["method"] == "FPP-SAA")
    # Replicate 0 mean = (100+200)/2 = 150; replicate 1 mean = (104+208)/2 = 156.
    import statistics
    expected_between = statistics.stdev([150.0, 156.0])
    check(abs(saa_row["between_replicate_std"] - expected_between) < 1e-9,
          "between_replicate_std is computed from per-replicate means, not from the 4 raw observations directly")


def test_single_replicate_reports_none_not_zero():
    """With only one replicate contributing, between_replicate_std must be
    None (insufficient data), never fabricated as 0.0."""
    rows = _build_fixture()
    single_replicate_rows = [r for r in rows if r["weight_replicate"] == 0]
    table = summ.replicate_aggregated_method_summary(single_replicate_rows, metric_field="weighted_fpp_expected_in_sample", metric_name="objective")
    saa_row = next(r for r in table if r["method"] == "FPP-SAA")
    check(saa_row["weight_replicates"] == 1, "only one replicate contributed")
    check(saa_row["between_replicate_std"] is None,
          "a single contributing replicate yields None for between_replicate_std, never a fabricated 0.0")


def test_raw_map_hashes_never_averaged():
    """Averaging happens only to metric VALUES, never to the weight maps
    themselves -- confirmed by the replicate-level table retaining every
    distinct raw hash untouched."""
    rows = _build_fixture()
    replicate_level = summ.replicate_level_method_summary(rows, metric_field="weighted_fpp_expected_in_sample", metric_name="objective")
    raw_hashes_in_fixture = {r["weight_map_hash"] for r in rows}
    raw_hashes_in_table = {r["weight_map_hash"] for r in replicate_level}
    check(raw_hashes_in_fixture == raw_hashes_in_table,
          "every original weight_map_hash from the fixture survives verbatim into the replicate-level table")


def main() -> int:
    test_replicate_level_preserves_full_granularity()
    test_replicate_aggregated_reports_required_columns()
    test_between_replicate_std_is_between_not_within()
    test_single_replicate_reports_none_not_zero()
    test_raw_map_hashes_never_averaged()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
