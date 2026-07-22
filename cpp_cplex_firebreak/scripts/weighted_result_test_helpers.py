#!/usr/bin/env python3
"""Shared synthetic-row builders for the Phase 9A result-schema test suite.

Not a test itself. Centralizes the baseline raw-row shape so the nine
test_weighted_result_*.py / test_weighted_comparison_group_keys.py scripts
don't each hand-roll a slightly different fixture (Phase 9A section 21's
"no hand-maintained conflicting column lists" applies to test fixtures too).
"""

from __future__ import annotations

import json
from pathlib import Path


def base_raw_row(**overrides) -> dict:
    row = {
        "run_id": "new20x20_case00_train8_test4_alpha0p02_fpp_saa_wphomogeneous_wr0_whabc12345",
        "attempt": "1",
        "worker_return_code": "0",
        "worker_status": "ok",
        "worker_started_at_epoch": "1000.0",
        "worker_finished_at_epoch": "1010.0",
        "worker_runtime_seconds": "10.0",
        "output_json": "",
        "solution_dir": "",
        "experiment_id": "smoke",
        "case_id": "case00",
        "instance_id": "new20x20",
        "instance_type": "shortest_path",
        "folder_name": "20x20",
        "landscape": "new20x20",
        "method": "FPP-SAA",
        "method_family": "FPP-SAA",
        "objective_family": "Expected",
        "fpp_mode": "base",
        "formulation": "base",
        "alpha": "0.02",
        "budget": "1",
        "train_ids": "1,2,3,4,5,6,7,8",
        "test_ids": "21,22,23,24",
        "train_count": "8",
        "test_count": "4",
        "train_scenario_count": "8",
        "test_scenario_count": "4",
        "solver_status": "Optimal",
        "status": "Optimal",
        "execution_status": "",
        "objective_in_sample": "47.0",
        "best_bound": "47.0",
        "mip_gap": "0.0",
        "solver_mip_gap": "0.0",
        "dpv_surrogate_objective": "",
        "dpv_surrogate_best_bound": "",
        "dpv_surrogate_gap": "",
        "risk_measure": "expected",
        "cvar_beta": "0.9",
        "cvar_lambda": "1.0",
        "train_expected_weighted_burn_loss": "47.0",
        "train_weighted_cvar": "114.0",
        "test_expected_weighted_burn_loss": "149.25",
        "test_weighted_cvar": "213.0",
        "train_expected_burned_area": "47.0",
        "test_expected_burned_area": "149.25",
        "train_empirical_var_burned_area": "100.0",
        "train_empirical_cvar_burned_area": "114.0",
        "test_empirical_var_burned_area": "180.0",
        "test_empirical_cvar_burned_area": "213.0",
        "train_percentage_landscape_value_burned": "11.75",
        "test_percentage_landscape_value_burned": "37.3",
        "train_percentage_high_value_weight_burned": "0.0",
        "test_percentage_high_value_weight_burned": "0.0",
        "train_evaluation_runtime_seconds": "0.01",
        "test_evaluation_runtime_seconds": "0.01",
        "canonical_landscape_id": "new20x20__20x20__a43d93884168abc8",
        "paired_landscape_id": "new20x20_reburn__20x20__a43d93884168abc8",
        "weight_profile": "homogeneous",
        "weight_replicate": "0",
        "weight_generation_seed": "0",
        "weight_generator_version": "0",
        "weight_map_file": "/weight_maps/new20x20/homogeneous/replicate_0/weights.csv",
        "weight_map_hash": "fnv1a64:abc12345",
        "weight_source_universe_hash": "fnv1a64:univ0001",
        "weight_normalized": "false",
        "weight_mapping_method": "identity",
        "weight_mapping_hash": "fnv1a64:map0001",
        "optimization_weight_map_hash": "fnv1a64:abc12345",
        "out_of_sample_weight_map_hash": "fnv1a64:abc12345",
        "paired_reburn_weight_map_hash": "fnv1a64:abc12345",
        # Off by default so tests unrelated to paired-reburn provenance don't
        # need to reason about it; paired-validation tests turn it on
        # explicitly (paired_reburn_status="ok", the real gating signal --
        # NOT paired_evaluation_enabled, which is unreliable on real data,
        # see weighted_result_schema.is_fully_paired_valid_result) and supply
        # a JSON-backed weighted_fpp_expected_paired_reburn.
        "paired_evaluation_enabled": "false",
        "paired_reburn_instance_id": "new20x20_reburn",
        "paired_reburn_instance_requested": "new20x20_reburn",
        "paired_reburn_instance_resolved": "new20x20_reburn",
        "paired_reburn_resolution_status": "ok",
        "paired_reburn_status": "n/a",
        "paired_reburn_train_expected_burned_area": "295.125",
        "paired_reburn_train_eval_runtime_sec": "0.02",
        "paired_reburn_train_scenario_count": "8",
        "paired_selected_firebreak_count": "1",
        "paired_selected_firebreaks_mapped": "1",
        "paired_selected_firebreaks_missing": "0",
        "paired_selected_mapping_status": "ok",
        "objective_validation_passed": "true",
        "objective_validation_abs_difference": "0.0",
        "objective_validation_rel_difference": "0.0",
    }
    row.update({k: str(v) for k, v in overrides.items()})
    return row


def make_paired_json(tmp_path: Path, *, expected_weighted_burn_loss=295.125, weighted_cvar=392.0,
                      weight_map_hash="fnv1a64:abc12345") -> tuple:
    """Write a fake solver JSON + sibling paired-reburn-eval JSON and return
    (output_json_path, paired_json_path) as strings, for tests that exercise
    the JSON-preferred normalization path for the paired_reburn namespace."""
    solver_json = tmp_path / "task_000.json"
    paired_json = tmp_path / "task_000_paired_reburn_eval.json"
    solver_json.write_text(json.dumps({"weight_map_hash": weight_map_hash}), encoding="utf-8")
    paired_json.write_text(json.dumps({
        "expected_burned_area": expected_weighted_burn_loss,
        "expected_weighted_burn_loss": expected_weighted_burn_loss,
        "weighted_var": weighted_cvar,
        "weighted_cvar": weighted_cvar,
        "weight_map_hash": weight_map_hash,
    }), encoding="utf-8")
    return str(solver_json), str(paired_json)


_PASS = 0
_FAIL = 0


def check(condition: bool, message: str) -> None:
    global _PASS, _FAIL
    if condition:
        _PASS += 1
    else:
        _FAIL += 1
        print(f"FAIL: {message}")


def report(test_file: str) -> int:
    print(f"{test_file}: {_PASS} passed, {_FAIL} failed")
    return 1 if _FAIL else 0
