#!/usr/bin/env python3
"""Aggregate FPP risk-study JSON outputs into analysis-ready CSV files."""

from __future__ import annotations

import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path
from statistics import mean, median
from typing import Any

import fpp_risk_study_config as cfg


RUN_FIELDS = [
    "run_id",
    "method_family",
    "method_variant",
    "method_label",
    "method_key",
    "landscape",
    "alpha",
    "lambda",
    "risk_measure",
    "cvar_beta",
    "cvar_lambda",
    "case_id",
    "seed",
    "split_key",
    "train_count",
    "test_count",
    "train_ids",
    "test_ids",
    "status",
    "objective",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "time_limit",
    "time_limit_like",
    "threads",
    "explored_nodes",
    "expected_loss_component",
    "cvar_loss_component",
    "risk_threshold_value",
    "selected_firebreaks",
    "selected_count",
    "train_expected_burned_area",
    "test_expected_burned_area",
    "train_empirical_var_burned_area",
    "train_empirical_cvar_burned_area",
    "test_empirical_var_burned_area",
    "test_empirical_cvar_burned_area",
    "lazy_cuts_added",
    "candidate_incumbents_checked",
    "final_max_cut_violation",
    "restricted_candidate_enabled",
    "restricted_heuristic_mode",
    "global_optimality_certified",
    "stopped_before_full_activation",
    "initial_candidate_size",
    "initial_active_candidate_count",
    "candidate_activation_policy",
    "candidate_maintenance_policy",
    "candidate_score_mode",
    "candidate_tail_score_gamma",
    "candidate_tail_protection_size",
    "final_active_candidate_count",
    "final_active_candidate_fraction",
    "candidate_rounds",
    "deactivation_rounds",
    "activated_candidate_count",
    "deactivated_candidate_count",
    "deactivation_candidate_count",
    "protected_tail_count",
    "deactivation_blocked_by_tail_protection_count",
    "reactivation_blocked_by_cooldown_count",
    "oscillation_event_count",
    "activated_by_tail_blend_count",
    "activated_tail_top_k_overlap",
    "deactivated_tail_top_k_warning_count",
    "cut_pool_size",
    "cuts_reused_in_full_stage",
    "duplicate_cuts_skipped",
    "global_time_budget_enabled",
    "time_budget_exhausted",
    "persistent_subproblem_build_count",
    "persistent_subproblem_update_count",
    "persistent_subproblem_solve_count",
    "master_rebuild_count",
    "notes",
    "json_path",
    "log_path",
]

PAIRWISE_COMPARISONS = [
    ("fpp-saa", "fpp-bb"),
    ("fpp-bb", "restricted-generic-no-maintenance"),
    ("fpp-bb", "restricted-generic-maintenance"),
    ("fpp-bb", "restricted-tailblend-maintenance"),
    ("restricted-generic-maintenance", "restricted-tailblend-maintenance"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Aggregate FPP risk-study outputs.")
    parser.add_argument("--output-dir", type=Path, default=cfg.DEFAULT_OUTPUT_DIR)
    parser.add_argument("--manifest", type=Path, default=None)
    return parser.parse_args()


def load_manifest(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def get_nested(data: dict[str, Any], keys: list[str], default: Any = "") -> Any:
    current: Any = data
    for key in keys:
        if not isinstance(current, dict) or key not in current:
            return default
        current = current[key]
    return current


def first_present(data: dict[str, Any], keys: list[str], default: Any = "") -> Any:
    for key in keys:
        if key in data and data[key] is not None:
            return data[key]
    return default


def as_float(value: Any) -> float | None:
    if value in ("", None):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    if not math.isfinite(parsed):
        return None
    return parsed


def as_int(value: Any) -> int | None:
    if value in ("", None):
        return None
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return None


def as_bool_text(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if value in ("true", "false"):
        return str(value)
    if value in ("", None):
        return ""
    return "true" if bool(value) else "false"


def join_values(values: Any) -> str:
    if isinstance(values, list):
        return ";".join(str(value) for value in values)
    if values is None:
        return ""
    return str(values)


def count_round_list_items(rounds: Any, key: str) -> int:
    if not isinstance(rounds, list):
        return 0
    total = 0
    for round_payload in rounds:
        if not isinstance(round_payload, dict):
            continue
        values = round_payload.get(key, [])
        if isinstance(values, list):
            total += len(values)
    return total


def sum_round_numeric_values(rounds: Any, key: str) -> int:
    if not isinstance(rounds, list):
        return 0
    total = 0
    for round_payload in rounds:
        if not isinstance(round_payload, dict):
            continue
        parsed = as_int(round_payload.get(key, 0))
        if parsed is not None:
            total += parsed
    return total


def first_restricted_value(restricted: dict[str, Any], keys: list[str], default: Any = "") -> Any:
    for key in keys:
        if key in restricted and restricted[key] not in ("", None):
            return restricted[key]
    return default


def load_json_payload(path: Path) -> tuple[dict[str, Any] | None, str]:
    if not path.exists():
        return None, "missing_json"
    try:
        return json.loads(path.read_text(encoding="utf-8")), ""
    except json.JSONDecodeError as exc:
        return None, f"invalid_json: {exc}"


def row_from_manifest_and_json(manifest_row: dict[str, str], payload: dict[str, Any] | None, error: str) -> dict[str, str]:
    payload = payload or {}
    branch = payload.get("branch_benders", {}) if isinstance(payload.get("branch_benders"), dict) else {}
    restricted = payload.get("restricted_candidate", {}) if isinstance(payload.get("restricted_candidate"), dict) else {}
    restricted_rounds = restricted.get("round_log", [])
    selected = first_present(payload, ["selected_firebreaks"], [])
    notes = first_present(payload, ["notes"], [])
    method_key = manifest_row.get("method_key", "")
    status = error or str(first_present(payload, ["solver_status"], ""))
    lazy_cuts = branch.get("lazy_cuts_added", "")
    if lazy_cuts == "":
        lazy_cuts = payload.get("branch_benders_lazy_cuts_added", "")
    if lazy_cuts == "" and restricted:
        lazy_cuts = restricted.get("cut_pool_size", "")
    candidate_checks = branch.get("candidate_incumbents_checked", "")
    if candidate_checks == "" and restricted:
        rounds = restricted.get("round_log", [])
        if isinstance(rounds, list):
            candidate_checks = sum(int(round.get("candidate_incumbents_checked", 0) or 0) for round in rounds if isinstance(round, dict))
    max_violation = first_present(payload, ["max_cut_violation"], "")
    if max_violation == "" and branch:
        max_violation = branch.get("max_cut_violation", "")
    if max_violation == "" and restricted:
        if isinstance(restricted_rounds, list) and restricted_rounds:
            max_violation = max((float(round.get("max_cut_violation", 0.0) or 0.0)
                                 for round in restricted_rounds if isinstance(round, dict)), default=0.0)

    activated_candidate_count = count_round_list_items(restricted_rounds, "newly_activated_candidates")
    deactivated_candidate_count = count_round_list_items(restricted_rounds, "deactivated_candidates")
    cuts_reused_in_full_stage = first_restricted_value(
        restricted,
        ["cuts_reused_in_full_stage", "cuts_reused"],
        sum_round_numeric_values(restricted_rounds, "cuts_reused_at_stage"))
    deactivation_candidate_count = first_restricted_value(
        restricted,
        ["deactivation_candidate_count"],
        sum_round_numeric_values(restricted_rounds, "deactivation_candidate_count"))
    protected_tail_count = first_restricted_value(
        restricted,
        ["protected_tail_count"],
        sum_round_numeric_values(restricted_rounds, "protected_tail_count"))
    oscillation_event_count = first_restricted_value(
        restricted,
        ["oscillation_event_count"],
        sum_round_numeric_values(restricted_rounds, "oscillation_event_count"))

    row = {
        "run_id": manifest_row.get("run_id") or str(first_present(payload, ["run_id"], "")),
        "method_family": manifest_row.get("method_family", ""),
        "method_variant": manifest_row.get("method_variant", ""),
        "method_label": manifest_row.get("method_label") or str(first_present(payload, ["method"], "")),
        "method_key": method_key,
        "landscape": manifest_row.get("landscape") or str(first_present(payload, ["landscape"], "")),
        "alpha": manifest_row.get("alpha") or str(first_present(payload, ["alpha"], "")),
        "lambda": manifest_row.get("lambda", ""),
        "risk_measure": manifest_row.get("risk_measure") or str(first_present(payload, ["risk_measure"], "")),
        "cvar_beta": manifest_row.get("cvar_beta") or str(first_present(payload, ["cvar_beta"], "")),
        "cvar_lambda": manifest_row.get("cvar_lambda") or str(first_present(payload, ["cvar_lambda"], "")),
        "case_id": manifest_row.get("case_id") or str(first_present(payload, ["case_id"], "")),
        "seed": manifest_row.get("seed", ""),
        "split_key": manifest_row.get("split_key", ""),
        "train_count": manifest_row.get("train_count") or str(first_present(payload, ["train_scenario_count"], "")),
        "test_count": manifest_row.get("test_count") or str(first_present(payload, ["test_scenario_count"], "")),
        "train_ids": manifest_row.get("train_ids") or join_values(first_present(payload, ["train_ids"], [])),
        "test_ids": manifest_row.get("test_ids") or join_values(first_present(payload, ["test_ids"], [])),
        "status": status,
        "objective": str(first_present(payload, ["objective_in_sample"], "")),
        "best_bound": str(first_present(payload, ["best_bound"], "")),
        "mip_gap": str(first_present(payload, ["mip_gap"], "")),
        "runtime_seconds": str(first_present(payload, ["runtime_seconds"], "")),
        "time_limit": manifest_row.get("time_limit", ""),
        "time_limit_like": "",
        "threads": manifest_row.get("threads", ""),
        "explored_nodes": str(first_present(payload, ["explored_nodes"], "")),
        "expected_loss_component": str(first_present(payload, ["expected_loss_component"], "")),
        "cvar_loss_component": str(first_present(payload, ["cvar_loss_component"], "")),
        "risk_threshold_value": str(first_present(payload, ["risk_threshold_value"], "")),
        "selected_firebreaks": join_values(selected),
        "selected_count": str(len(selected) if isinstance(selected, list) else ""),
        "train_expected_burned_area": str(first_present(payload, ["train_expected_burned_area"], "")),
        "test_expected_burned_area": str(first_present(payload, ["test_expected_burned_area"], "")),
        "train_empirical_var_burned_area": str(first_present(payload, ["train_empirical_var_burned_area"], "")),
        "train_empirical_cvar_burned_area": str(first_present(payload, ["train_empirical_cvar_burned_area"], "")),
        "test_empirical_var_burned_area": str(first_present(payload, ["test_empirical_var_burned_area"], "")),
        "test_empirical_cvar_burned_area": str(first_present(payload, ["test_empirical_cvar_burned_area"], "")),
        "lazy_cuts_added": str(lazy_cuts),
        "candidate_incumbents_checked": str(candidate_checks),
        "final_max_cut_violation": str(max_violation),
        "restricted_candidate_enabled": as_bool_text(restricted.get("enabled", "")),
        "restricted_heuristic_mode": as_bool_text(restricted.get("heuristic_mode_enabled", "")),
        "global_optimality_certified": as_bool_text(restricted.get("global_optimality_certified", "")),
        "stopped_before_full_activation": as_bool_text(restricted.get("stopped_before_full_activation", "")),
        "initial_candidate_size": manifest_row.get("initial_candidate_size") or str(restricted.get("initial_active_count", "")),
        "initial_active_candidate_count": str(restricted.get("initial_active_count", "")),
        "candidate_activation_policy": manifest_row.get("candidate_activation_policy") or str(restricted.get("activation_policy", "")),
        "candidate_maintenance_policy": manifest_row.get("candidate_maintenance_policy") or str(restricted.get("maintenance_policy", "")),
        "candidate_score_mode": manifest_row.get("candidate_score_mode") or str(restricted.get("candidate_score_mode", "")),
        "candidate_tail_score_gamma": manifest_row.get("candidate_tail_score_gamma") or str(restricted.get("candidate_tail_score_gamma", "")),
        "candidate_tail_protection_size": manifest_row.get("candidate_tail_protection_size") or str(restricted.get("candidate_tail_protection_size", "")),
        "final_active_candidate_count": str(restricted.get("final_active_count", "")),
        "final_active_candidate_fraction": str(restricted.get("final_active_fraction", "")),
        "candidate_rounds": str(restricted.get("candidate_rounds", "")),
        "deactivation_rounds": str(restricted.get("deactivation_rounds", "")),
        "activated_candidate_count": str(activated_candidate_count),
        "deactivated_candidate_count": str(deactivated_candidate_count),
        "deactivation_candidate_count": str(deactivation_candidate_count),
        "protected_tail_count": str(protected_tail_count),
        "deactivation_blocked_by_tail_protection_count": str(restricted.get("deactivation_blocked_by_tail_protection_count", "")),
        "reactivation_blocked_by_cooldown_count": str(restricted.get("reactivation_blocked_by_cooldown_count", "")),
        "oscillation_event_count": str(oscillation_event_count),
        "activated_by_tail_blend_count": str(restricted.get("activated_by_tail_blend_count", "")),
        "activated_tail_top_k_overlap": str(restricted.get("activated_tail_top_k_overlap", "")),
        "deactivated_tail_top_k_warning_count": str(restricted.get("deactivated_tail_top_k_warning_count", "")),
        "cut_pool_size": str(restricted.get("cut_pool_size", "")),
        "cuts_reused_in_full_stage": str(cuts_reused_in_full_stage),
        "duplicate_cuts_skipped": str(restricted.get("duplicate_cuts_skipped", "")),
        "global_time_budget_enabled": as_bool_text(restricted.get("global_time_budget_enabled", "")),
        "time_budget_exhausted": as_bool_text(restricted.get("time_budget_exhausted", "")),
        "persistent_subproblem_build_count": str(restricted.get("persistent_subproblem_build_count", "")),
        "persistent_subproblem_update_count": str(restricted.get("persistent_subproblem_update_count", "")),
        "persistent_subproblem_solve_count": str(restricted.get("persistent_subproblem_solve_count", "")),
        "master_rebuild_count": str(restricted.get("master_rebuild_count", "")),
        "notes": join_values(notes),
        "json_path": manifest_row.get("json_path", ""),
        "log_path": manifest_row.get("log_path", ""),
    }
    row["time_limit_like"] = "true" if is_time_limit_like(row) else "false"
    return {field: row.get(field, "") for field in RUN_FIELDS}


def discover_json_rows(output_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in sorted((output_dir / "json").glob("*.json")):
        payload, error = load_json_payload(path)
        rows.append(row_from_manifest_and_json({"json_path": str(path)}, payload, error))
    return rows


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fieldnames})


def numeric_values(rows: list[dict[str, str]], field: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        parsed = as_float(row.get(field))
        if parsed is not None:
            values.append(parsed)
    return values


def is_time_limit_like(row: dict[str, str]) -> bool:
    status = row.get("status", "").lower()
    if "time" in status:
        return True
    runtime = as_float(row.get("runtime_seconds"))
    time_limit = as_float(row.get("time_limit"))
    if runtime is None or time_limit is None or time_limit <= 0.0:
        return False
    return runtime >= 0.999 * time_limit and status in {"feasible", "unknown", "aborted"}


def method_summary(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row.get("method_label", row.get("method_key", ""))].append(row)
    summary: list[dict[str, Any]] = []
    def avg(group: list[dict[str, str]], field: str) -> float | str:
        values = numeric_values(group, field)
        return mean(values) if values else ""
    def med(group: list[dict[str, str]], field: str) -> float | str:
        values = numeric_values(group, field)
        return median(values) if values else ""
    for method_label, group in sorted(grouped.items()):
        statuses = [row.get("status", "") for row in group]
        normalized_statuses = [status.lower() for status in statuses]
        failure_count = sum(
            1 for status in normalized_statuses
            if not status or "missing" in status or "fail" in status or "error" in status)
        time_limit_count = sum(1 for row in group if is_time_limit_like(row))
        summary.append({
            "method_label": method_label,
            "method_key": group[0].get("method_key", ""),
            "method_family": group[0].get("method_family", ""),
            "runs": len(group),
            "optimal_count": sum(1 for status in normalized_statuses if status == "optimal"),
            "feasible_count": sum(1 for status in normalized_statuses if status == "feasible"),
            "restricted_heuristic_count": sum(1 for status in normalized_statuses if "restrictedheuristic" in status),
            "failure_count": failure_count,
            "time_limit_like_count": time_limit_count,
            "time_limit_count": time_limit_count,
            "mean_objective": avg(group, "objective"),
            "median_objective": med(group, "objective"),
            "avg_objective": avg(group, "objective"),
            "mean_runtime_seconds": avg(group, "runtime_seconds"),
            "median_runtime_seconds": med(group, "runtime_seconds"),
            "max_runtime_seconds": max(numeric_values(group, "runtime_seconds")) if numeric_values(group, "runtime_seconds") else "",
            "avg_runtime_seconds": avg(group, "runtime_seconds"),
            "mean_mip_gap": avg(group, "mip_gap"),
            "mean_explored_nodes": avg(group, "explored_nodes"),
            "mean_test_expected_burned_area": avg(group, "test_expected_burned_area"),
            "avg_test_expected_burned_area": avg(group, "test_expected_burned_area"),
            "mean_test_empirical_cvar_burned_area": avg(group, "test_empirical_cvar_burned_area"),
            "avg_test_empirical_cvar_burned_area": avg(group, "test_empirical_cvar_burned_area"),
            "mean_final_active_candidate_fraction": avg(group, "final_active_candidate_fraction"),
            "mean_candidate_rounds": avg(group, "candidate_rounds"),
            "mean_deactivation_rounds": avg(group, "deactivation_rounds"),
            "mean_activated_candidate_count": avg(group, "activated_candidate_count"),
            "mean_deactivated_candidate_count": avg(group, "deactivated_candidate_count"),
            "mean_lazy_cuts_added": avg(group, "lazy_cuts_added"),
            "mean_candidate_incumbents_checked": avg(group, "candidate_incumbents_checked"),
            "mean_cut_pool_size": avg(group, "cut_pool_size"),
            "statuses": ";".join(sorted(set(statuses))),
        })
    return summary


def group_key(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row.get("landscape", ""),
        row.get("alpha", ""),
        row.get("lambda", ""),
        row.get("case_id", ""),
        row.get("train_count", ""),
        row.get("test_count", ""),
        row.get("split_key", ""),
    )


def pairwise_summary(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, ...], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in rows:
        grouped[group_key(row)][row.get("method_key", "")] = row

    out: list[dict[str, Any]] = []
    for key, by_method in sorted(grouped.items()):
        for left_key, right_key in PAIRWISE_COMPARISONS:
            left = by_method.get(left_key)
            right = by_method.get(right_key)
            reason = ""
            valid = True
            if left is None or right is None:
                valid = False
                reason = "missing method row"
            left_objective = as_float(left.get("objective")) if left else None
            right_objective = as_float(right.get("objective")) if right else None
            if valid and (left_objective is None or right_objective is None):
                valid = False
                reason = "missing objective"
            left_runtime = as_float(left.get("runtime_seconds")) if left else None
            right_runtime = as_float(right.get("runtime_seconds")) if right else None
            objective_diff = right_objective - left_objective if valid else ""
            relative_diff = (
                objective_diff / max(abs(left_objective), 1.0e-12)
                if valid and left_objective is not None else "")
            runtime_ratio = (
                right_runtime / left_runtime
                if left_runtime and right_runtime is not None and left_runtime > 0.0 else "")
            out.append({
                "landscape": key[0],
                "alpha": key[1],
                "lambda": key[2],
                "case_id": key[3],
                "split_key": key[6],
                "left_method": left_key,
                "right_method": right_key,
                "objective_diff": objective_diff,
                "relative_objective_diff": relative_diff,
                "runtime_ratio": runtime_ratio,
                "test_expected_burned_area_diff": (
                    (as_float(right.get("test_expected_burned_area")) or 0.0) -
                    (as_float(left.get("test_expected_burned_area")) or 0.0)
                    if valid and left and right else ""),
                "test_cvar_burned_area_diff": (
                    (as_float(right.get("test_empirical_cvar_burned_area")) or 0.0) -
                    (as_float(left.get("test_empirical_cvar_burned_area")) or 0.0)
                    if valid and left and right else ""),
                "same_selected_firebreaks": (
                    "true" if valid and left and right and
                    left.get("selected_firebreaks") == right.get("selected_firebreaks") else
                    ("false" if valid else "")),
                "left_status": left.get("status", "") if left else "",
                "right_status": right.get("status", "") if right else "",
                "comparison_valid": "true" if valid else "false",
                "reason_if_invalid": reason,
            })
    return out


def performance_profile_rows(rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[group_key(row)].append(row)
    out: list[dict[str, Any]] = []
    for key, group in grouped.items():
        objectives = [value for value in (as_float(row.get("objective")) for row in group) if value is not None]
        runtimes = [value for value in (as_float(row.get("runtime_seconds")) for row in group) if value and value > 0.0]
        test_expected_values = [
            value for value in (as_float(row.get("test_expected_burned_area")) for row in group)
            if value is not None
        ]
        test_cvar_values = [
            value for value in (as_float(row.get("test_empirical_cvar_burned_area")) for row in group)
            if value is not None
        ]
        best_objective = min(objectives) if objectives else None
        best_runtime = min(runtimes) if runtimes else None
        best_test_expected = min(test_expected_values) if test_expected_values else None
        best_test_cvar = min(test_cvar_values) if test_cvar_values else None
        for row in group:
            objective = as_float(row.get("objective"))
            runtime = as_float(row.get("runtime_seconds"))
            test_expected = as_float(row.get("test_expected_burned_area"))
            test_cvar = as_float(row.get("test_empirical_cvar_burned_area"))
            gap = ""
            if best_objective is not None and objective is not None:
                gap = (objective - best_objective) / max(abs(best_objective), 1.0e-12)
            ratio = ""
            if best_runtime is not None and runtime is not None and best_runtime > 0.0:
                ratio = runtime / best_runtime
            test_expected_gap = ""
            if best_test_expected is not None and test_expected is not None:
                test_expected_gap = test_expected - best_test_expected
            test_cvar_gap = ""
            if best_test_cvar is not None and test_cvar is not None:
                test_cvar_gap = test_cvar - best_test_cvar
            out.append({
                "run_id": row.get("run_id", ""),
                "method_key": row.get("method_key", ""),
                "method_label": row.get("method_label", ""),
                "landscape": key[0],
                "alpha": key[1],
                "lambda": key[2],
                "case_id": key[3],
                "split_key": key[6],
                "objective": row.get("objective", ""),
                "runtime_seconds": row.get("runtime_seconds", ""),
                "best_objective_for_group": best_objective if best_objective is not None else "",
                "objective_gap_to_best": gap,
                "best_test_expected_burned_area_for_group": best_test_expected if best_test_expected is not None else "",
                "test_expected_burned_area_gap_to_best": test_expected_gap,
                "best_test_empirical_cvar_burned_area_for_group": best_test_cvar if best_test_cvar is not None else "",
                "test_cvar_burned_area_gap_to_best": test_cvar_gap,
                "runtime_ratio_to_best": ratio,
                "solved_to_target_quality_1pct": "true" if isinstance(gap, float) and gap <= 0.01 else "false",
                "solved_to_target_quality_5pct": "true" if isinstance(gap, float) and gap <= 0.05 else "false",
                "status": row.get("status", ""),
            })
    return out


def failure_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    failures: list[dict[str, str]] = []
    for row in rows:
        status = row.get("status", "").lower()
        objective_missing = as_float(row.get("objective")) is None
        if "missing" in status or "fail" in status or "error" in status or objective_missing:
            failures.append(row)
    return failures


def main() -> int:
    args = parse_args()
    output_dir = args.output_dir
    layout = cfg.ensure_output_layout(output_dir)
    manifest_path = args.manifest or (layout["commands"] / "full_command_manifest.csv")
    manifest_rows = load_manifest(manifest_path)
    if manifest_rows:
        rows: list[dict[str, str]] = []
        for manifest_row in manifest_rows:
            json_path = Path(manifest_row.get("json_path", ""))
            payload, error = load_json_payload(json_path)
            rows.append(row_from_manifest_and_json(manifest_row, payload, error))
    else:
        rows = discover_json_rows(output_dir)

    write_csv(layout["summaries"] / "fpp_risk_run_summary.csv", RUN_FIELDS, rows)
    method_fields = [
        "method_label", "method_key", "method_family", "runs", "optimal_count",
        "feasible_count", "restricted_heuristic_count", "failure_count",
        "time_limit_like_count", "time_limit_count", "mean_objective",
        "median_objective", "avg_objective", "mean_runtime_seconds",
        "median_runtime_seconds", "max_runtime_seconds", "avg_runtime_seconds",
        "mean_mip_gap", "mean_explored_nodes", "mean_test_expected_burned_area",
        "avg_test_expected_burned_area", "mean_test_empirical_cvar_burned_area",
        "avg_test_empirical_cvar_burned_area", "mean_final_active_candidate_fraction",
        "mean_candidate_rounds", "mean_deactivation_rounds",
        "mean_activated_candidate_count", "mean_deactivated_candidate_count",
        "mean_lazy_cuts_added", "mean_candidate_incumbents_checked",
        "mean_cut_pool_size", "statuses",
    ]
    write_csv(layout["summaries"] / "fpp_risk_method_summary.csv", method_fields, method_summary(rows))
    pairwise = pairwise_summary(rows)
    pairwise_fields = [
        "landscape", "alpha", "lambda", "case_id", "split_key", "left_method", "right_method",
        "objective_diff", "relative_objective_diff", "runtime_ratio",
        "test_expected_burned_area_diff", "test_cvar_burned_area_diff",
        "same_selected_firebreaks", "left_status", "right_status",
        "comparison_valid", "reason_if_invalid",
    ]
    write_csv(layout["summaries"] / "fpp_risk_pairwise_summary.csv", pairwise_fields, pairwise)
    profile_rows = performance_profile_rows(rows)
    profile_fields = [
        "run_id", "method_key", "method_label", "landscape", "alpha", "lambda", "case_id",
        "split_key", "objective", "runtime_seconds", "best_objective_for_group",
        "objective_gap_to_best", "best_test_expected_burned_area_for_group",
        "test_expected_burned_area_gap_to_best",
        "best_test_empirical_cvar_burned_area_for_group",
        "test_cvar_burned_area_gap_to_best", "runtime_ratio_to_best",
        "solved_to_target_quality_1pct", "solved_to_target_quality_5pct", "status",
    ]
    write_csv(layout["summaries"] / "fpp_risk_performance_profile_data.csv", profile_fields, profile_rows)
    write_csv(layout["summaries"] / "fpp_risk_failures.csv", RUN_FIELDS, failure_rows(rows))
    write_csv(
        layout["summaries"] / "fpp_risk_time_limits.csv",
        RUN_FIELDS,
        [row for row in rows if is_time_limit_like(row)])

    print(f"Run rows: {len(rows)}")
    print(f"Method summary rows: {len(method_summary(rows))}")
    print(f"Pairwise rows: {len(pairwise)}")
    print(f"Performance-profile rows: {len(profile_rows)}")
    print(f"Failure rows: {len(failure_rows(rows))}")
    print(f"Time-limit-like rows: {sum(1 for row in rows if is_time_limit_like(row))}")
    print(f"Wrote summaries under: {layout['summaries']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
