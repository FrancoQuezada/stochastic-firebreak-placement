#!/usr/bin/env python3
"""Post-hoc diagnostics for Phase 1M FPP-CVaR restricted-candidate runs."""

from __future__ import annotations

import csv
import json
import math
import statistics
from pathlib import Path
from typing import Any


PROJECT_DIR = Path(__file__).resolve().parents[2]
PHASE1M_DIR = PROJECT_DIR / "results" / "new_method_phase1m_fpp_cvar_restricted_comparison"
OUT_DIR = PROJECT_DIR / "results" / "new_method_phase1n_fpp_cvar_triage"

STAGE_CSV = OUT_DIR / "phase1m_posthoc_stage_diagnostics.csv"
TIME_LIMIT_CSV = OUT_DIR / "phase1m_posthoc_time_limit_diagnostics.csv"
HEURISTIC_CSV = OUT_DIR / "phase1m_posthoc_heuristic_quality.csv"
EXACT_PAIRWISE_CSV = OUT_DIR / "phase1m_posthoc_exact_pairwise.csv"
HEURISTIC_PARAM_CSV = OUT_DIR / "phase1n_heuristic_parameter_diagnostics.csv"

NOMINAL_TIME_LIMIT = 600.0
MIP_GAP = 0.001


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as src:
        return list(csv.DictReader(src))


def write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({column: format_cell(row.get(column, "")) for column in columns})


def format_cell(value: Any) -> Any:
    if value is None:
        return ""
    if isinstance(value, float):
        if math.isnan(value):
            return ""
        return f"{value:.10g}"
    if isinstance(value, list):
        return ";".join(str(v) for v in value)
    return value


def parse_float(value: Any) -> float | None:
    if value in ("", None):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def load_json_by_run_id() -> dict[str, dict[str, Any]]:
    out: dict[str, dict[str, Any]] = {}
    for path in sorted((PHASE1M_DIR / "json").glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        run_id = str(data.get("run_id") or path.stem)
        out[run_id] = data
    return out


def run_id_from_main_row(row: dict[str, str]) -> str:
    method = row["method"]
    if method == "FPP-SAA-CVaR":
        token = "saa_cvar"
    elif method == "FPP-Branch-Benders-CVaR":
        token = "branch_benders_cvar"
    elif method == "FPP-Restricted-Branch-Benders-CVaR-Exact":
        token = "restricted_bb_cvar_exact"
    elif method == "FPP-Restricted-Branch-Benders-CVaR-Heuristic":
        token = "restricted_bb_cvar_heuristic"
    else:
        token = method.lower().replace("-", "_")
    alpha = str(row["alpha"]).replace(".", "p")
    return f"phase1m_{token}_alpha_{alpha}_case_{row['case_id']}"


def is_time_limited(row: dict[str, str]) -> bool:
    status = row.get("status", "")
    if "time" in status.lower() and "limit" in status.lower():
        return True
    runtime = parse_float(row.get("runtime_seconds"))
    gap = parse_float(row.get("mip_gap"))
    if status == "Feasible" and runtime is not None and runtime >= 0.99 * NOMINAL_TIME_LIMIT:
        return gap is None or gap > MIP_GAP
    return False


def missing_stage_fields(round_data: dict[str, Any]) -> list[str]:
    required = [
        "stage_type",
        "risk_measure",
        "best_bound",
        "mip_gap",
        "runtime_seconds",
        "time_limit_seconds",
        "remaining_global_time_before_stage",
        "remaining_global_time_after_stage",
        "candidate_incumbents_checked",
        "subproblem_solves",
        "callback_time_seconds",
        "subproblem_time_seconds",
        "cut_pool_size_before_stage",
        "new_cuts_added_to_pool",
        "duplicate_cuts_skipped",
        "selected_firebreaks",
    ]
    return [field for field in required if field not in round_data]


def stage_rows(main_rows: list[dict[str, str]], json_by_run: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for main in main_rows:
        if "Restricted" not in main["method"]:
            continue
        run_id = run_id_from_main_row(main)
        data = json_by_run.get(run_id, {})
        restricted = data.get("restricted_candidate") or {}
        round_log = restricted.get("round_log") or []
        if not round_log:
            rows.append({
                "run_id": run_id,
                "method": main["method"],
                "alpha": main["alpha"],
                "case_id": main["case_id"],
                "stage_index": "",
                "stage_type": "",
                "status": main["status"],
                "missing_fields": "round_log",
            })
            continue
        for stage in round_log:
            missing = missing_stage_fields(stage)
            rows.append({
                "run_id": run_id,
                "method": main["method"],
                "alpha": main["alpha"],
                "case_id": main["case_id"],
                "stage_index": stage.get("stage_index", stage.get("round_index", "")),
                "stage_type": stage.get("stage_type", ""),
                "risk_measure": stage.get("risk_measure", data.get("risk_measure", "")),
                "activation_policy": stage.get("activation_policy", ""),
                "active_candidate_count": stage.get("active_candidate_count", ""),
                "active_candidate_fraction": stage.get("active_candidate_fraction", ""),
                "status": stage.get("status", stage.get("solve_status", "")),
                "objective": stage.get("objective", ""),
                "best_bound": stage.get("best_bound", ""),
                "mip_gap": stage.get("mip_gap", ""),
                "runtime_seconds": stage.get("runtime_seconds", ""),
                "time_limit_seconds": stage.get("time_limit_seconds", ""),
                "remaining_global_time_before_stage": stage.get("remaining_global_time_before_stage", ""),
                "remaining_global_time_after_stage": stage.get("remaining_global_time_after_stage", ""),
                "lazy_cuts_added": stage.get("lazy_cuts_added", stage.get("cuts_added", "")),
                "candidate_incumbents_checked": stage.get("candidate_incumbents_checked", ""),
                "subproblem_solves": stage.get("subproblem_solves", ""),
                "callback_time_seconds": stage.get("callback_time_seconds", ""),
                "subproblem_time_seconds": stage.get("subproblem_time_seconds", ""),
                "cut_pool_size_before_stage": stage.get("cut_pool_size_before_stage", ""),
                "cuts_inserted_from_pool": stage.get("cuts_inserted_from_pool", stage.get("cuts_reused_at_stage", "")),
                "new_cuts_added_to_pool": stage.get("new_cuts_added_to_pool", ""),
                "duplicate_cuts_skipped": stage.get("duplicate_cuts_skipped", ""),
                "max_cut_violation": stage.get("max_cut_violation", ""),
                "selected_firebreak_count": len(stage.get("selected_firebreaks", []) or []),
                "missing_fields": ";".join(missing),
            })
    return rows


def time_limit_rows(main_rows: list[dict[str, str]], json_by_run: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for main in main_rows:
        run_id = run_id_from_main_row(main)
        data = json_by_run.get(run_id, {})
        restricted = data.get("restricted_candidate") or {}
        runtime = parse_float(main.get("runtime_seconds"))
        nominal_exceeded = runtime is not None and runtime > NOMINAL_TIME_LIMIT + 1.0
        rows.append({
            "run_id": run_id,
            "method": main["method"],
            "alpha": main["alpha"],
            "case_id": main["case_id"],
            "status": main["status"],
            "runtime_seconds": main.get("runtime_seconds", ""),
            "objective": main.get("objective", ""),
            "best_bound": main.get("best_bound", ""),
            "mip_gap": main.get("mip_gap", ""),
            "time_limited_or_warning": is_time_limited(main),
            "nominal_wall_time_exceeded": nominal_exceeded,
            "restricted_time_budget_exhausted": restricted.get("time_budget_exhausted", ""),
            "restricted_global_time_limit": restricted.get("global_time_limit_seconds", ""),
            "restricted_elapsed_time_total": restricted.get("elapsed_time_total_seconds", ""),
            "restricted_final_stage_time_limit": restricted.get("final_stage_time_limit_seconds", ""),
            "eventually_activated_all": restricted.get("eventually_activated_all", ""),
            "global_optimality_certified": restricted.get("global_optimality_certified", ""),
            "diagnosis": diagnose_time_limit(main, restricted, nominal_exceeded),
        })
    return rows


def diagnose_time_limit(row: dict[str, str], restricted: dict[str, Any], nominal_exceeded: bool) -> str:
    if nominal_exceeded and restricted:
        return "restricted staged solve exceeded nominal Phase 1M wall time; global budget fields were missing in Phase 1M outputs"
    if is_time_limited(row):
        return "solver returned feasible incumbent at nominal time limit"
    if row.get("status") == "RestrictedHeuristic":
        return "heuristic stopped before full activation by design"
    return "completed without time-limit warning"


def effective_objective(row: dict[str, str], json_by_run: dict[str, dict[str, Any]]) -> tuple[float | None, str]:
    run_id = run_id_from_main_row(row)
    data = json_by_run.get(run_id, {})
    restricted = data.get("restricted_candidate") or {}
    if row.get("method") == "FPP-Restricted-Branch-Benders-CVaR-Heuristic":
        restricted_objective = parse_float(restricted.get("restricted_objective"))
        if restricted_objective is not None:
            return restricted_objective, "restricted_candidate.restricted_objective"
    return parse_float(row.get("objective")), "objective"


def heuristic_quality_rows(
    main_rows: list[dict[str, str]],
    pairwise_rows: list[dict[str, str]],
    json_by_run: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    by_case_method: dict[tuple[str, str, str], dict[str, str]] = {}
    for row in main_rows:
        by_case_method[(row["alpha"], row["case_id"], row["method"])] = row

    rows: list[dict[str, Any]] = []
    for row in pairwise_rows:
        if row.get("comparison_type") != "heuristic":
            continue
        left = by_case_method.get((row.get("alpha", ""), row.get("case_id", ""), row.get("left_method", "")))
        right = by_case_method.get((row.get("alpha", ""), row.get("case_id", ""), row.get("right_method", "")))
        left_obj, left_source = effective_objective(left, json_by_run) if left else (None, "")
        right_obj, right_source = effective_objective(right, json_by_run) if right else (None, "")
        obj_diff = right_obj - left_obj if left_obj is not None and right_obj is not None else None
        test_cvar_diff = parse_float(row.get("test_cvar_diff_right_minus_left"))
        rows.append({
            "alpha": row.get("alpha", ""),
            "case_id": row.get("case_id", ""),
            "reference_method": row.get("left_method", ""),
            "heuristic_method": row.get("right_method", ""),
            "reference_status": row.get("left_status", ""),
            "heuristic_status": row.get("right_status", ""),
            "objective_diff_heuristic_minus_reference": obj_diff if obj_diff is not None else "",
            "objective_source": right_source,
            "reference_objective_source": left_source,
            "test_cvar_diff_heuristic_minus_reference": row.get("test_cvar_diff_right_minus_left", ""),
            "runtime_ratio_heuristic_over_reference": row.get("runtime_ratio_right_over_left", ""),
            "heuristic_active_fraction": row.get("right_final_active_fraction", ""),
            "heuristic_objective_not_worse": obj_diff is not None and obj_diff <= 1.0e-6,
            "heuristic_test_cvar_lower": test_cvar_diff is not None and test_cvar_diff < 0.0,
            "global_optimality_certified": row.get("right_global_optimality_certified", ""),
        })
    return rows


def exact_pairwise_rows(pairwise_rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for row in pairwise_rows:
        if row.get("comparison_type") != "exact":
            continue
        diff = parse_float(row.get("abs_objective_diff"))
        both_optimal = row.get("left_status") == "Optimal" and row.get("right_status") == "Optimal"
        rows.append({
            "alpha": row.get("alpha", ""),
            "case_id": row.get("case_id", ""),
            "left_method": row.get("left_method", ""),
            "right_method": row.get("right_method", ""),
            "left_status": row.get("left_status", ""),
            "right_status": row.get("right_status", ""),
            "both_optimal": both_optimal,
            "abs_objective_diff": row.get("abs_objective_diff", ""),
            "objective_mismatch_when_both_optimal": both_optimal and diff is not None and diff > 1.0e-6,
            "selected_firebreaks_equal": row.get("selected_firebreaks_equal", ""),
            "runtime_ratio_right_over_left": row.get("runtime_ratio_right_over_left", ""),
            "notes": row.get("notes", ""),
        })
    return rows


def heuristic_parameter_rows(
    main_rows: list[dict[str, str]],
    json_by_run: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    heuristic = [row for row in main_rows if row["method"] == "FPP-Restricted-Branch-Benders-CVaR-Heuristic"]
    objective_values = [effective_objective(row, json_by_run)[0] for row in heuristic]
    runtimes = [parse_float(row.get("runtime_seconds")) for row in heuristic]
    active_fractions = [parse_float(row.get("final_active_candidate_fraction")) for row in heuristic]
    finite_objectives = [v for v in objective_values if v is not None]
    finite_runtimes = [v for v in runtimes if v is not None]
    finite_fractions = [v for v in active_fractions if v is not None]
    return [{
        "diagnostic_scope": "phase1m-current-heuristic-parameters",
        "initial_candidate_size": 50,
        "activation_policy": "benders-coefficients",
        "activation_batch_size": 20,
        "max_candidate_rounds": 2,
        "rows": len(heuristic),
        "mean_objective": statistics.mean(finite_objectives) if finite_objectives else "",
        "mean_runtime_seconds": statistics.mean(finite_runtimes) if finite_runtimes else "",
        "mean_final_active_fraction": statistics.mean(finite_fractions) if finite_fractions else "",
        "diagnosis": "current heuristic uses 25% final active fraction and shows a large in-sample objective gap; larger pools or CVaR-tail-aware scoring should be evaluated before paper-scale runs",
    }]


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    main_rows = read_csv(PHASE1M_DIR / "fpp_cvar_restricted_comparison.csv")
    pairwise = read_csv(PHASE1M_DIR / "fpp_cvar_restricted_pairwise.csv")
    json_by_run = load_json_by_run_id()

    write_csv(
        STAGE_CSV,
        stage_rows(main_rows, json_by_run),
        [
            "run_id",
            "method",
            "alpha",
            "case_id",
            "stage_index",
            "stage_type",
            "risk_measure",
            "activation_policy",
            "active_candidate_count",
            "active_candidate_fraction",
            "status",
            "objective",
            "best_bound",
            "mip_gap",
            "runtime_seconds",
            "time_limit_seconds",
            "remaining_global_time_before_stage",
            "remaining_global_time_after_stage",
            "lazy_cuts_added",
            "candidate_incumbents_checked",
            "subproblem_solves",
            "callback_time_seconds",
            "subproblem_time_seconds",
            "cut_pool_size_before_stage",
            "cuts_inserted_from_pool",
            "new_cuts_added_to_pool",
            "duplicate_cuts_skipped",
            "max_cut_violation",
            "selected_firebreak_count",
            "missing_fields",
        ],
    )
    write_csv(
        TIME_LIMIT_CSV,
        time_limit_rows(main_rows, json_by_run),
        [
            "run_id",
            "method",
            "alpha",
            "case_id",
            "status",
            "runtime_seconds",
            "objective",
            "best_bound",
            "mip_gap",
            "time_limited_or_warning",
            "nominal_wall_time_exceeded",
            "restricted_time_budget_exhausted",
            "restricted_global_time_limit",
            "restricted_elapsed_time_total",
            "restricted_final_stage_time_limit",
            "eventually_activated_all",
            "global_optimality_certified",
            "diagnosis",
        ],
    )
    write_csv(
        HEURISTIC_CSV,
        heuristic_quality_rows(main_rows, pairwise, json_by_run),
        [
            "alpha",
            "case_id",
            "reference_method",
            "heuristic_method",
            "reference_status",
            "heuristic_status",
            "objective_diff_heuristic_minus_reference",
            "objective_source",
            "reference_objective_source",
            "test_cvar_diff_heuristic_minus_reference",
            "runtime_ratio_heuristic_over_reference",
            "heuristic_active_fraction",
            "heuristic_objective_not_worse",
            "heuristic_test_cvar_lower",
            "global_optimality_certified",
        ],
    )
    write_csv(
        EXACT_PAIRWISE_CSV,
        exact_pairwise_rows(pairwise),
        [
            "alpha",
            "case_id",
            "left_method",
            "right_method",
            "left_status",
            "right_status",
            "both_optimal",
            "abs_objective_diff",
            "objective_mismatch_when_both_optimal",
            "selected_firebreaks_equal",
            "runtime_ratio_right_over_left",
            "notes",
        ],
    )
    write_csv(
        HEURISTIC_PARAM_CSV,
        heuristic_parameter_rows(main_rows, json_by_run),
        [
            "diagnostic_scope",
            "initial_candidate_size",
            "activation_policy",
            "activation_batch_size",
            "max_candidate_rounds",
            "rows",
            "mean_objective",
            "mean_runtime_seconds",
            "mean_final_active_fraction",
            "diagnosis",
        ],
    )

    print(f"Wrote {STAGE_CSV}")
    print(f"Wrote {TIME_LIMIT_CSV}")
    print(f"Wrote {HEURISTIC_CSV}")
    print(f"Wrote {EXACT_PAIRWISE_CSV}")
    print(f"Wrote {HEURISTIC_PARAM_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
