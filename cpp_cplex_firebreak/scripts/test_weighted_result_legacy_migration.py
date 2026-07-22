#!/usr/bin/env python3
"""Phase 9A legacy-migration test. Covers mandatory case:
  27.10 legacy homogeneous migration (explicit migration metadata)

Usage: python3 scripts/test_weighted_result_legacy_migration.py
"""

from __future__ import annotations

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import check, report


def _legacy_unweighted_row() -> dict:
    """A pre-Phase-8A row: no weight_profile/weight_map_hash at all, no
    run_id suffix, none of the canonical-registry provenance columns."""
    return {
        "run_id": "new20x20_case00_train8_test4_alpha0p02_fpp_saa",
        "attempt": "1",
        "worker_return_code": "0",
        "instance_id": "new20x20",
        "landscape": "new20x20",
        "method": "FPP-SAA",
        "alpha": "0.02",
        "train_ids": "1,2,3,4,5,6,7,8",
        "test_ids": "21,22,23,24",
        "train_scenario_count": "8",
        "test_scenario_count": "4",
        "solver_status": "Optimal",
        "objective_in_sample": "47.0",
        "best_bound": "47.0",
        "mip_gap": "0.0",
        "risk_measure": "expected",
        "train_expected_burned_area": "47.0",
        "test_expected_burned_area": "149.25",
    }


def _legacy_weighted_pre8a_row() -> dict:
    """A Phase 5A-style row: weight_profile/weight_map_hash exist, but none
    of the Phase 8A canonical-registry columns do (matches
    results/weighted_phase5a_smoke/phase5a_benders_smoke.csv on disk)."""
    row = _legacy_unweighted_row()
    row.update({
        "weight_profile": "homogeneous",
        "weight_map_hash": "fnv1a64:legacy0001",
        "weight_map_file": "/weight_maps/new20x20/weights.csv",
    })
    return row


def test_legacy_unweighted_migration():
    record = norm.normalize_row(_legacy_unweighted_row(), prefer_json=False)
    check(record["result_schema_version"] == schema.LEGACY_SCHEMA_UNWEIGHTED,
          "a fully unweighted legacy row is detected as legacy-unweighted-pre8a")
    check(record["migration_status"] == "legacy_migrated", "migration_status explicitly flags this as migrated")
    check("canonical_landscape_id" in record["legacy_unmigrated_fields"],
          "missing canonical-registry fields are explicitly reported, not silently left blank with no trace")

    classification, reasons = merge.validate_record(record)
    check(classification == schema.VALID_LEGACY_MIGRATED,
          "a well-formed legacy row validates as valid_legacy_migrated, never plain 'valid'")
    check(any("migrated from" in r for r in reasons), "the validation reasons record what version this was migrated from")


def test_legacy_weighted_pre8a_migration():
    record = norm.normalize_row(_legacy_weighted_pre8a_row(), prefer_json=False)
    check(record["result_schema_version"] == schema.LEGACY_SCHEMA_WEIGHTED_PRE8A,
          "a weighted-but-pre-registry row is detected as legacy-weighted-pre8a, distinct from fully unweighted legacy")
    check(record["weight_profile"] == "homogeneous", "the legacy weight_profile value is preserved, not fabricated")
    check(not record["canonical_landscape_id"], "no canonical_landscape_id is fabricated for a pre-registry legacy row")


def test_legacy_never_collides_with_modern():
    legacy = norm.normalize_row(_legacy_unweighted_row(), prefer_json=False)
    modern_row = dict(_legacy_unweighted_row())
    modern_row["run_id"] = _legacy_unweighted_row()["run_id"] + "_wphomogeneous_wr0_whabc12345"
    modern_row["weight_profile"] = "homogeneous"
    modern_row["weight_replicate"] = "0"
    modern_row["weight_generation_seed"] = "0"
    modern_row["weight_generator_version"] = "1"
    modern_row["canonical_landscape_id"] = "new20x20__20x20__a43d93884168abc8"
    modern_row["weight_source_universe_hash"] = "fnv1a64:univ0001"
    modern_row["weight_mapping_method"] = "identity"
    modern_row["weight_map_hash"] = "fnv1a64:abc12345"
    modern = norm.normalize_row(modern_row, prefer_json=False)

    check(legacy["logical_run_key"] != modern["logical_run_key"],
          "a legacy row and its modern weighted counterpart never share a logical run key")
    check(modern["result_schema_version"] == schema.RESULT_SCHEMA_VERSION,
          "the modern row with full registry provenance is detected as the current schema version")


def main() -> int:
    test_legacy_unweighted_migration()
    test_legacy_weighted_pre8a_migration()
    test_legacy_never_collides_with_modern()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
