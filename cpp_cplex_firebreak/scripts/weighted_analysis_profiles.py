#!/usr/bin/env python3
"""Phase 9B performance profiles (Dolan-More, runtime, exact-FPP methods
only) and quality profiles (gap-to-best-known-feasible, for approximate/DPV/
heuristic methods), plus optional cactus/coverage threshold data.

Runtime profiles and quality profiles are two distinct artifacts (section
21 vs. 22) and are never generated from the same underlying ratio -- a
runtime profile is never mislabeled as a quality profile or vice versa.
"""

from __future__ import annotations

import math

import weighted_analysis_core as core
import weighted_result_schema as schema

DEFAULT_CACTUS_THRESHOLDS = (0.0, 0.001, 0.01, 0.05, 0.10)


def _group_stratum(record: dict) -> tuple:
    return (record.get("weight_profile"), record.get("risk_measure"))


def _group_identity(record: dict) -> tuple:
    return schema.comparison_group_key(record)


# ---------------------------------------------------------------------------
# Runtime performance profile (section 21)
# ---------------------------------------------------------------------------

def _is_solved_runtime(record: dict, success_criterion: str, feasible_tolerance: float) -> bool:
    status = str(record.get("solver_status") or "").lower()
    if success_criterion == "optimal":
        return status == "optimal"
    # feasible_tolerance: ANY row with an evaluated incumbent (Optimal,
    # Feasible, or a time-limited-but-feasible terminal status) counts as
    # solved as long as its gap to best-known feasible is within tolerance
    # -- the status label itself does not gate this criterion, only whether
    # a real evaluated objective (hence a gap) exists at all (section 21:
    # "distinguish optimal-only and feasible-within-tolerance profiles").
    gap = record.get("gap_to_best_known_feasible")
    return gap is not None and gap <= feasible_tolerance


def build_runtime_performance_profiles(
    current_valid_rows: list, all_attempts_rows: list, *,
    success_criterion: str = "optimal", feasible_tolerance: float = 0.01,
) -> dict:
    """Returns dict[(weight_profile, risk_measure)] -> {"rows": [...],
    "ratios_by_method": {...}, "instance_count": int, "success_criterion": str}.

    Profiles are built SEPARATELY per (weight_profile, risk_measure) stratum
    -- never pooled across weight maps or risk measures (section 21) -- and
    `rows` is the CSV-ready diagnostic table for that stratum alone."""
    exact_valid = [r for r in current_valid_rows if r.get("objective_space") == core.OBJECTIVE_SPACE_EXACT_FPP]
    exact_attempts = [
        r for r in all_attempts_rows
        if str(r.get("method") or "").startswith("FPP") and r.get("dpv_surrogate_objective") is None
    ]

    strata: dict = {}
    for r in exact_valid + exact_attempts:
        strata.setdefault(_group_stratum(r), {"methods": set(), "groups": {}})
        strata[_group_stratum(r)]["methods"].add(r.get("method"))
    for r in exact_valid:
        bucket = strata[_group_stratum(r)]["groups"].setdefault(_group_identity(r), {})
        bucket.setdefault(r.get("method"), []).append(r)
    attempted_groups: dict = {}
    for r in exact_attempts:
        attempted_groups.setdefault((_group_stratum(r), _group_identity(r)), set()).add(r.get("method"))

    profiles: dict = {}
    for stratum, info in strata.items():
        methods = sorted(info["methods"])
        groups = info["groups"]
        diagnostic_rows = []
        ratios_by_method: dict = {method: [] for method in methods}

        for group_key, method_rows in groups.items():
            runtimes: dict = {}
            statuses: dict = {}
            for method, rows in method_rows.items():
                row = rows[0]
                statuses[method] = row.get("solver_status")
                if _is_solved_runtime(row, success_criterion, feasible_tolerance):
                    runtimes[method] = row.get("running_time_sec")
            best = min(runtimes.values()) if runtimes else math.inf
            for method in methods:
                value = runtimes.get(method)
                solved = method in runtimes and math.isfinite(best)
                ratio = (value / best) if solved and value is not None else math.inf
                ratios_by_method[method].append(ratio)
                diagnostic_rows.append({
                    "weight_profile": stratum[0],
                    "risk_measure": stratum[1],
                    "instance_id": group_key.instance_id,
                    "weight_map_hash": group_key.weight_map_hash,
                    "method": method,
                    "runtime_sec": "" if value is None else f"{value:.10g}",
                    "best_runtime_sec": "" if not math.isfinite(best) else f"{best:.10g}",
                    "runtime_ratio": "inf" if not math.isfinite(ratio) else f"{ratio:.10g}",
                    "solver_status": statuses.get(method, ""),
                    "solved": solved,
                    "attempted": method in method_rows or method in attempted_groups.get((stratum, group_key), set()),
                })

        profiles[stratum] = {
            "rows": diagnostic_rows,
            "ratios_by_method": ratios_by_method,
            "instance_count": len(groups),
            "success_criterion": success_criterion,
        }
    return profiles


def profile_points(ratios: list, x_max: float) -> tuple:
    """Dolan-More step-function points: rho_m(tau) = fraction of ALL
    problem instances (including unsolved, ratio=inf) with ratio <= tau."""
    n = len(ratios)
    if n == 0:
        return [1.0], [0.0]
    finite = sorted(v for v in ratios if math.isfinite(v))
    unique = sorted({round(v, 12) for v in finite if v >= 1.0})
    x_values = [1.0]
    y_values = [sum(v <= 1.0 + 1.0e-9 for v in finite) / n]
    for value in unique:
        if value <= 1.0 + 1.0e-9:
            continue
        x_values.append(value)
        y_values.append(sum(v <= value + 1.0e-9 for v in finite) / n)
    if x_values[-1] < x_max:
        x_values.append(x_max)
        y_values.append(y_values[-1])
    return x_values, y_values


# ---------------------------------------------------------------------------
# Quality profiles (section 22)
# ---------------------------------------------------------------------------

_QUALITY_NAMESPACES = {
    "insample": "gap_to_best_known_feasible",
    "oos": "out_of_sample_regret_to_best_observed",
    "paired": "paired_reburn_regret_to_best_observed",
}


def build_quality_profiles(rows: list, *, namespace: str) -> dict:
    """Returns dict[(weight_profile, risk_measure)] -> {"rows": [...],
    "ratios_by_method": {...}, "namespace": str, "gap_field": str}.

    q_{p,m} = 1 + max(0, gap_{p,m}) where gap is the namespace-appropriate
    quantity (in-sample gap-to-best-known-feasible, OOS best-observed
    regret, or paired-reburn best-observed regret). Approximate/DPV/
    heuristic methods only. Built separately per (weight_profile,
    risk_measure) stratum -- never pooled (section 22). Requires a valid
    exact reference to exist for the in-sample namespace; OOS/paired
    namespaces use best-observed instead and so never require one."""
    gap_field = _QUALITY_NAMESPACES[namespace]
    candidate_rows = [
        r for r in rows
        if r.get("objective_space") != core.OBJECTIVE_SPACE_EXACT_FPP and r.get(gap_field) is not None
    ]
    strata: dict = {}
    for r in candidate_rows:
        strata.setdefault(_group_stratum(r), []).append(r)

    profiles: dict = {}
    for stratum, group_rows in strata.items():
        ratios_by_method: dict = {}
        diagnostic_rows = []
        for r in group_rows:
            gap = r[gap_field]
            q = 1.0 + max(0.0, gap)
            ratios_by_method.setdefault(r.get("method"), []).append(q)
            diagnostic_rows.append({
                "weight_profile": stratum[0],
                "risk_measure": stratum[1],
                "namespace": namespace,
                "method": method_of(r),
                "run_id": r.get("run_id"),
                "gap": gap,
                "quality_ratio": q,
            })
        profiles[stratum] = {
            "rows": diagnostic_rows, "ratios_by_method": ratios_by_method,
            "namespace": namespace, "gap_field": gap_field,
        }
    return profiles


def method_of(record: dict) -> str:
    return record.get("method")


# ---------------------------------------------------------------------------
# Cactus / coverage data (section 23, optional but mandatory CSV if plotted)
# ---------------------------------------------------------------------------

def build_cactus_coverage(rows: list, *, thresholds: tuple = DEFAULT_CACTUS_THRESHOLDS) -> list:
    """Fraction of rows with gap_to_best_known_feasible <= threshold, per
    method, for each configurable threshold."""
    by_method: dict = {}
    for r in rows:
        gap = r.get("gap_to_best_known_feasible")
        if gap is None:
            continue
        by_method.setdefault(r.get("method"), []).append(gap)

    out = []
    for method, gaps in sorted(by_method.items()):
        n = len(gaps)
        for threshold in thresholds:
            fraction = sum(1 for g in gaps if g <= threshold) / n
            out.append({
                "method": method,
                "threshold": threshold,
                "fraction_within_threshold": fraction,
                "observations": n,
            })
    return out
