#!/usr/bin/env python3
"""Shared synthetic canonical-record builders for the Phase 9B analysis test
suite. Not a test itself.

Records are built directly in the already-normalized, typed shape that
`weighted_result_schema.read_canonical_csv()` produces (i.e. what Phase 9B
actually consumes) -- these tests exercise the analysis layer, not the
Phase 9A normalization layer (already covered by the Phase 9A test suite).
"""

from __future__ import annotations

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


def make_record(**overrides) -> dict:
    record = {
        "result_schema_version": "weighted-result-9a.1",
        "run_id": "new20x20_case00_fpp_saa_wphomogeneous_wr0_whabc123",
        "attempt": 1,
        "execution_status": "",
        "solver_status": "Optimal",
        "method": "FPP-SAA",
        "method_family": "FPP-SAA",
        "method_variant": "",
        "solver_formulation": "base",
        "llbi_type": "none",
        "combinatorial_mode": "",
        "lifting_mode": "",
        "scenario_order": "",
        "sampling_ratio": None,
        "dpv_variant": "",
        "dpv_ignition_policy": "",
        "restricted_candidate_mode": "none",
        "risk_measure": "expected",
        "cvar_beta": 0.9,
        "mean_cvar_lambda": 1.0,
        "alpha": 0.02,
        "budget": 1,
        "canonical_landscape_id": "new20x20__20x20__abc123",
        "paired_landscape_id": "new20x20_reburn__20x20__abc123",
        "weight_profile": "homogeneous",
        "weight_replicate": 0,
        "weight_generation_seed": "0",
        "weight_generator_version": 0,
        "weight_map_path": "/weight_maps/new20x20/homogeneous/replicate_0/weights.csv",
        "weight_map_hash": "fnv1a64:abc123",
        "weight_source_universe_hash": "fnv1a64:univ001",
        "weight_normalization_mode": "raw",
        "weight_mapping_method": "identity",
        "weight_mapping_hash": "fnv1a64:map001",
        "manifest_weight_map_hash": "fnv1a64:abc123",
        "optimization_weight_map_hash": "fnv1a64:abc123",
        "in_sample_weight_map_hash": "fnv1a64:abc123",
        "out_of_sample_weight_map_hash": "fnv1a64:abc123",
        "paired_reburn_weight_map_hash": "fnv1a64:abc123",
        "instance_id": "new20x20",
        "instance_family": "shortest_path",
        "landscape": "new20x20",
        "folder_name": "20x20",
        "experiment_id": "smoke",
        "case_id": "case00",
        "train_ids": [1, 2, 3, 4, 5, 6, 7, 8],
        "test_ids": [21, 22, 23, 24],
        "train_scenario_count": 8,
        "test_scenario_count": 4,
        "in_sample_scenario_ids": [1, 2, 3, 4, 5, 6, 7, 8],
        "out_of_sample_scenario_ids": [21, 22, 23, 24],
        "paired_reburn_scenario_ids": [1, 2, 3, 4, 5, 6, 7, 8],
        "paired_reburn_scenario_count": 8,
        "solver_objective": 47.0,
        "solver_best_bound": 47.0,
        "solver_gap": 0.0,
        "dpv_surrogate_objective": None,
        "dpv_surrogate_best_bound": None,
        "dpv_surrogate_gap": None,
        "in_sample_evaluation_status": "ok",
        "weighted_fpp_expected_in_sample": 47.0,
        "weighted_fpp_cvar_in_sample": 114.0,
        "weighted_fpp_mean_cvar_in_sample": None,
        "mean_burned_cells_in_sample": 47.0,
        "in_sample_evaluation_time_sec": 0.01,
        "out_of_sample_evaluation_status": "ok",
        "weighted_fpp_expected_out_of_sample": 149.25,
        "weighted_fpp_cvar_out_of_sample": 213.0,
        "weighted_fpp_mean_cvar_out_of_sample": None,
        "mean_burned_cells_out_of_sample": 149.25,
        "out_of_sample_evaluation_time_sec": 0.01,
        "paired_reburn_evaluation_status": "ok",
        "paired_reburn_evaluation_time_sec": 0.02,
        "weighted_fpp_expected_paired_reburn": 295.125,
        "weighted_fpp_cvar_paired_reburn": 392.0,
        "weighted_fpp_mean_cvar_paired_reburn": None,
        "mean_burned_cells_paired_reburn": 295.125,
        "paired_evaluation_enabled": False,
        "paired_reburn_instance_id": "new20x20_reburn",
        "paired_reburn_instance_requested": "new20x20_reburn",
        "paired_reburn_instance_resolved": "new20x20_reburn",
        "paired_reburn_resolution_status": "ok",
        "paired_selected_firebreak_count": 1,
        "paired_selected_firebreaks_mapped": 1,
        "paired_selected_firebreaks_missing": 0,
        "paired_selected_mapping_status": "ok",
        "burned_cells_var_in_sample": 100.0,
        "burned_cells_cvar_in_sample": 114.0,
        "burned_cells_worst10_in_sample": 100.0,
        "burned_cells_var_out_of_sample": 180.0,
        "burned_cells_cvar_out_of_sample": 213.0,
        "burned_cells_worst10_out_of_sample": 180.0,
        "percentage_landscape_value_burned_in_sample": 11.75,
        "percentage_landscape_value_burned_out_of_sample": 37.3,
        "percentage_high_value_weight_burned_in_sample": 0.0,
        "percentage_high_value_weight_burned_out_of_sample": 0.0,
        "objective_validation_passed": True,
        "objective_validation_abs_difference": 0.0,
        "objective_validation_rel_difference": 0.0,
        "running_time_sec": 0.45,
        "worker_return_code": "0",
        "validation_classification": "valid",
        "validation_reasons": "",
        "duplicate_category": "unique",
        "is_current_valid": True,
        "content_hash": "deadbeef",
        "migration_status": "modern",
        "legacy_unmigrated_fields": [],
        "source_format": "worker_csv",
        "source_file": "synthetic",
    }
    record.update(overrides)
    return record


def make_exact_row(run_id: str, *, value: float, best_bound: float, runtime: float,
                    method: str = "FPP-SAA", weight_profile: str = "homogeneous",
                    weight_map_hash: str = "fnv1a64:abc123", solver_status: str = "Optimal",
                    **overrides) -> dict:
    return make_record(
        run_id=run_id, method=method, method_family=method, weight_profile=weight_profile,
        weight_map_hash=weight_map_hash, manifest_weight_map_hash=weight_map_hash,
        optimization_weight_map_hash=weight_map_hash, in_sample_weight_map_hash=weight_map_hash,
        out_of_sample_weight_map_hash=weight_map_hash, paired_reburn_weight_map_hash=weight_map_hash,
        solver_status=solver_status, solver_objective=value, solver_best_bound=best_bound,
        solver_gap=0.0 if best_bound == value else abs(value - best_bound) / max(abs(value), 1e-9),
        weighted_fpp_expected_in_sample=value, running_time_sec=runtime,
        objective_validation_passed=True,
        **overrides,
    )


def make_dpv_row(run_id: str, *, surrogate_value: float, evaluator_value: float, runtime: float,
                  method: str = "DPV-SAA", weight_profile: str = "homogeneous",
                  weight_map_hash: str = "fnv1a64:abc123", **overrides) -> dict:
    return make_record(
        run_id=run_id, method=method, method_family=method, weight_profile=weight_profile,
        weight_map_hash=weight_map_hash, manifest_weight_map_hash=weight_map_hash,
        optimization_weight_map_hash=weight_map_hash, in_sample_weight_map_hash=weight_map_hash,
        out_of_sample_weight_map_hash=weight_map_hash, paired_reburn_weight_map_hash=weight_map_hash,
        solver_status="Feasible", dpv_surrogate_objective=surrogate_value, dpv_surrogate_best_bound=surrogate_value,
        dpv_surrogate_gap=0.0, solver_objective=None, solver_best_bound=None, solver_gap=None,
        weighted_fpp_expected_in_sample=evaluator_value, running_time_sec=runtime,
        objective_validation_passed=False,
        **overrides,
    )


def make_heuristic_row(run_id: str, *, evaluator_value: float, runtime: float,
                        method: str = "Static-DPV", weight_profile: str = "homogeneous",
                        weight_map_hash: str = "fnv1a64:abc123", **overrides) -> dict:
    return make_record(
        run_id=run_id, method=method, method_family=method, weight_profile=weight_profile,
        weight_map_hash=weight_map_hash, manifest_weight_map_hash=weight_map_hash,
        optimization_weight_map_hash=weight_map_hash, in_sample_weight_map_hash=weight_map_hash,
        out_of_sample_weight_map_hash=weight_map_hash, paired_reburn_weight_map_hash=weight_map_hash,
        solver_status="NotApplicable", execution_status="heuristic_completed",
        dpv_surrogate_objective=evaluator_value * 10.0, solver_objective=None, solver_best_bound=None, solver_gap=None,
        weighted_fpp_expected_in_sample=evaluator_value, running_time_sec=runtime,
        objective_validation_passed=False,
        **overrides,
    )
