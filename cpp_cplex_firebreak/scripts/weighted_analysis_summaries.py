#!/usr/bin/env python3
"""Phase 9B method-level summary tables and pairwise win/tie/loss counts.

Operates entirely on the rows produced by
`weighted_analysis_core.run_core_analysis()`. Never averages across
incompatible weight maps / risk configurations unless a grouping explicitly
says so, and always reports the number of contributing observations.
"""

from __future__ import annotations

import math
import statistics

import weighted_analysis_core as core
import weighted_result_schema as schema

# ---------------------------------------------------------------------------
# Small statistics helpers (no SciPy dependency needed for these).
# ---------------------------------------------------------------------------

def _finite(values) -> list:
    return [v for v in values if v is not None and isinstance(v, (int, float)) and math.isfinite(v)]


def mean_or_none(values):
    finite = _finite(values)
    return statistics.fmean(finite) if finite else None


def median_or_none(values):
    finite = _finite(values)
    return statistics.median(finite) if finite else None


def geometric_mean_or_none(values):
    """Geometric mean over strictly positive values only (runtime is always
    > 0 for a completed row; non-positive/missing values are excluded and
    do not silently zero out the result)."""
    finite = [v for v in _finite(values) if v > 0.0]
    if not finite:
        return None
    return math.exp(statistics.fmean(math.log(v) for v in finite))


def max_or_none(values):
    finite = _finite(values)
    return max(finite) if finite else None


# ---------------------------------------------------------------------------
# Method-level summary (section 16)
# ---------------------------------------------------------------------------

_TERMINAL_OPTIMAL = {"optimal"}
_TERMINAL_FEASIBLE = {"feasible"}
_TERMINAL_TIME_LIMIT = {"timelimit", "time_limit", "timelimit_feasible"}


def _status_bucket(record: dict) -> str:
    status = str(record.get("solver_status") or "").strip().lower()
    if status in _TERMINAL_OPTIMAL:
        return "optimal"
    if status in _TERMINAL_FEASIBLE:
        return "feasible"
    if status in _TERMINAL_TIME_LIMIT:
        return "time_limit"
    if record.get("validation_classification") == schema.INCOMPLETE or record.get("execution_status") == "":
        return "failed"
    return "other"


def method_summary(rows: list) -> list:
    """One row per (method, method_family, weight_profile, risk_measure).
    Never pools different weight profiles or risk measures into one row."""
    groups: dict = {}
    for r in rows:
        key = (r.get("method"), r.get("method_family"), r.get("weight_profile"), r.get("risk_measure"))
        groups.setdefault(key, []).append(r)

    out = []
    for (method, family, profile, risk), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][2] or "", kv[0][3] or "")):
        scenario_counts = sorted({r.get("train_scenario_count") for r in group if r.get("train_scenario_count") is not None})
        statuses = [_status_bucket(r) for r in group]
        out.append({
            "method": method,
            "method_family": family,
            "weight_profile": profile,
            "risk_measure": risk,
            "scenario_count": ",".join(str(v) for v in scenario_counts),
            "instances_total": len(group),
            "instances_valid": sum(1 for r in group if r.get("validation_classification") in (schema.VALID, schema.VALID_LEGACY_MIGRATED)),
            "instances_optimal": statuses.count("optimal"),
            "instances_feasible": statuses.count("feasible"),
            "instances_time_limit": statuses.count("time_limit"),
            "instances_failed": statuses.count("failed"),
            "mean_running_time_sec": mean_or_none([r.get("running_time_sec") for r in group]),
            "median_running_time_sec": median_or_none([r.get("running_time_sec") for r in group]),
            "geometric_mean_running_time_sec": geometric_mean_or_none([r.get("running_time_sec") for r in group]),
            "mean_solver_gap": mean_or_none([r.get("solver_gap") for r in group]),
            "median_solver_gap": median_or_none([r.get("solver_gap") for r in group]),
            "mean_gap_to_best_known_feasible": mean_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "median_gap_to_best_known_feasible": median_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "maximum_gap_to_best_known_feasible": max_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "mean_out_of_sample_regret": mean_or_none([r.get("out_of_sample_regret_to_best_observed") for r in group]),
            "median_out_of_sample_regret": median_or_none([r.get("out_of_sample_regret_to_best_observed") for r in group]),
            "mean_paired_reburn_regret": mean_or_none([r.get("paired_reburn_regret_to_best_observed") for r in group]),
            "median_paired_reburn_regret": median_or_none([r.get("paired_reburn_regret_to_best_observed") for r in group]),
        })
    return out


# ---------------------------------------------------------------------------
# Exact-method table (section 17)
#
# Phase 10 section 13: stratified by weight_profile BY DEFAULT (a method
# solved under heterogeneous vs. clustered maps is not the same comparison
# group and must never be silently pooled). An aggregate-across-profile
# variant is available but must be called explicitly and is clearly labeled
# (weight_profile="ALL_PROFILES_AGGREGATED"), never the default.
# ---------------------------------------------------------------------------

_AGGREGATED_PROFILE_LABEL = "ALL_PROFILES_AGGREGATED"


def exact_method_summary(rows: list, *, stratify_by_profile: bool = True) -> list:
    exact_rows = [r for r in rows if r.get("objective_space") == core.OBJECTIVE_SPACE_EXACT_FPP]
    groups: dict = {}
    for r in exact_rows:
        key = (r.get("method"), r.get("weight_profile") if stratify_by_profile else _AGGREGATED_PROFILE_LABEL)
        groups.setdefault(key, []).append(r)

    out = []
    for (method, profile), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
        statuses = [_status_bucket(r) for r in group]
        profiles_pooled = len({r.get("weight_profile") for r in group})
        out.append({
            "method": method,
            "weight_profile": profile,
            "profiles_pooled": profiles_pooled,
            "instances": len(group),
            "optimal_count": statuses.count("optimal"),
            "feasible_count": statuses.count("feasible"),
            "time_limit_count": statuses.count("time_limit"),
            "failure_count": statuses.count("failed"),
            "mean_time": mean_or_none([r.get("running_time_sec") for r in group]),
            "median_time": median_or_none([r.get("running_time_sec") for r in group]),
            "geometric_mean_time": geometric_mean_or_none([r.get("running_time_sec") for r in group]),
            "mean_solver_gap": mean_or_none([r.get("solver_gap") for r in group]),
            "median_solver_gap": median_or_none([r.get("solver_gap") for r in group]),
            "mean_gap_to_best_feasible": mean_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "mean_gap_to_best_bound": mean_or_none([r.get("gap_to_best_known_lower_bound") for r in group]),
            # Method-specific cut/iteration diagnostics are method-shaped
            # (e.g. Benders cuts vs. combinatorial cuts are not the same
            # quantity) -- report the raw passthrough field name used per
            # row rather than forcing them into one undifferentiated count.
            "cuts_or_iterations_note": "method-specific; see raw passthrough columns (e.g. benders_lazy_cuts_added, combinatorial_cuts_added)",
        })
    return out


def exact_method_summary_aggregated(rows: list) -> list:
    """Explicit, clearly-labeled aggregate-across-profile variant of
    exact_method_summary. Never the default table."""
    return exact_method_summary(rows, stratify_by_profile=False)


# ---------------------------------------------------------------------------
# Heuristic / DPV table (section 18) -- same profile-stratification policy.
# ---------------------------------------------------------------------------

def heuristic_dpv_summary(rows: list, *, stratify_by_profile: bool = True) -> list:
    candidate_rows = [
        r for r in rows
        if r.get("objective_space") in (core.OBJECTIVE_SPACE_DPV_OPTIMIZATION, core.OBJECTIVE_SPACE_HEURISTIC, core.OBJECTIVE_SPACE_APPROXIMATE_FPP)
    ]
    groups: dict = {}
    for r in candidate_rows:
        key = (r.get("method"), r.get("weight_profile") if stratify_by_profile else _AGGREGATED_PROFILE_LABEL)
        groups.setdefault(key, []).append(r)

    out = []
    for (method, profile), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
        profiles_pooled = len({r.get("weight_profile") for r in group})
        out.append({
            "method": method,
            "weight_profile": profile,
            "profiles_pooled": profiles_pooled,
            "instances": len(group),
            "mean_runtime": mean_or_none([r.get("running_time_sec") for r in group]),
            "median_runtime": median_or_none([r.get("running_time_sec") for r in group]),
            "mean_gap_to_best_feasible": mean_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "median_gap_to_best_feasible": median_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "maximum_gap_to_best_feasible": max_or_none([r.get("gap_to_best_known_feasible") for r in group]),
            "mean_oos_regret": mean_or_none([r.get("out_of_sample_regret_to_best_observed") for r in group]),
            "median_oos_regret": median_or_none([r.get("out_of_sample_regret_to_best_observed") for r in group]),
            "mean_paired_regret": mean_or_none([r.get("paired_reburn_regret_to_best_observed") for r in group]),
            "median_paired_regret": median_or_none([r.get("paired_reburn_regret_to_best_observed") for r in group]),
            "mean_burned_cells_oos": mean_or_none([r.get("mean_burned_cells_out_of_sample") for r in group]),
            "mean_weighted_loss_oos": mean_or_none([r.get("weighted_fpp_expected_out_of_sample") for r in group]),
            # Reported separately, never as the FPP comparison value (section 18).
            "dpv_surrogate_objective_mean": mean_or_none([r.get("dpv_surrogate_objective") for r in group]),
        })
    return out


def heuristic_dpv_summary_aggregated(rows: list) -> list:
    """Explicit, clearly-labeled aggregate-across-profile variant of
    heuristic_dpv_summary. Never the default table."""
    return heuristic_dpv_summary(rows, stratify_by_profile=False)


# ---------------------------------------------------------------------------
# Out-of-sample / paired-reburn / physical-metric summaries (sections 13-15)
# ---------------------------------------------------------------------------

def out_of_sample_summary(rows: list) -> list:
    groups: dict = {}
    for r in rows:
        if r.get("out_of_sample_regret_to_best_observed") is None:
            continue
        groups.setdefault((r.get("method"), r.get("weight_profile"), r.get("risk_measure")), []).append(r)
    out = []
    for (method, profile, risk), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
        out.append({
            "method": method,
            "weight_profile": profile,
            "risk_measure": risk,
            "observations": len(group),
            "mean_out_of_sample_value": mean_or_none([r.get(core.oos_metric_field(r)) for r in group]),
            "mean_out_of_sample_regret": mean_or_none([r.get("out_of_sample_regret_to_best_observed") for r in group]),
            "median_out_of_sample_regret": median_or_none([r.get("out_of_sample_regret_to_best_observed") for r in group]),
            "mean_burned_cells_out_of_sample": mean_or_none([r.get("mean_burned_cells_out_of_sample") for r in group]),
        })
    return out


def paired_reburn_summary(rows: list) -> list:
    groups: dict = {}
    for r in rows:
        if r.get("paired_reburn_regret_to_best_observed") is None:
            continue
        groups.setdefault((r.get("method"), r.get("weight_profile"), r.get("risk_measure")), []).append(r)
    out = []
    for (method, profile, risk), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
        out.append({
            "method": method,
            "weight_profile": profile,
            "risk_measure": risk,
            "observations": len(group),
            "mean_paired_reburn_value": mean_or_none([r.get(core.paired_metric_field(r)) for r in group]),
            "mean_paired_reburn_regret": mean_or_none([r.get("paired_reburn_regret_to_best_observed") for r in group]),
            "median_paired_reburn_regret": median_or_none([r.get("paired_reburn_regret_to_best_observed") for r in group]),
            "mean_burned_cells_paired_reburn": mean_or_none([r.get("mean_burned_cells_paired_reburn") for r in group]),
        })
    return out


def physical_metric_summary(rows: list) -> list:
    """Separate physical (unweighted burned-cell) summary, never combined
    into one ranking with the weighted-loss summaries above (section 15)."""
    groups: dict = {}
    for r in rows:
        groups.setdefault((r.get("method"), r.get("weight_profile")), []).append(r)
    out = []
    for (method, profile), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
        out.append({
            "method": method,
            "weight_profile": profile,
            "observations": len(group),
            "mean_burned_cells_in_sample": mean_or_none([r.get("mean_burned_cells_in_sample") for r in group]),
            "mean_burned_cells_out_of_sample": mean_or_none([r.get("mean_burned_cells_out_of_sample") for r in group]),
            "mean_burned_cells_paired_reburn": mean_or_none([r.get("mean_burned_cells_paired_reburn") for r in group]),
        })
    return out


# ---------------------------------------------------------------------------
# Win / tie / loss (section 19)
# ---------------------------------------------------------------------------

def pairwise_group_values(rows: list, group_key_fn, metric_field_fn, filter_fn=None) -> dict:
    """group_key -> {method: value}. Groups with more than one row for the
    same method are skipped (ambiguous pairing), not silently resolved."""
    groups: dict = {}
    for r in rows:
        if filter_fn is not None and not filter_fn(r):
            continue
        value = metric_field_fn(r)
        if value is None:
            continue
        key = group_key_fn(r)
        bucket = groups.setdefault(key, {})
        if r.get("method") in bucket:
            bucket[r.get("method")] = None  # ambiguous: mark and drop below
        else:
            bucket[r.get("method")] = value
    return {k: {m: v for m, v in bucket.items() if v is not None} for k, bucket in groups.items()}


def win_tie_loss(rows: list, *, metric: str, group_key_fn, metric_field_fn, filter_fn=None,
                  tolerance: float = core.DEFAULT_GAP_TOLERANCE, minimize: bool = True) -> list:
    by_group = pairwise_group_values(rows, group_key_fn, metric_field_fn, filter_fn)
    methods = sorted({m for bucket in by_group.values() for m in bucket})
    results = []
    for i, method_a in enumerate(methods):
        for method_b in methods[i + 1:]:
            wins = ties = losses = 0
            paired = 0
            for bucket in by_group.values():
                if method_a not in bucket or method_b not in bucket:
                    continue
                paired += 1
                va, vb = bucket[method_a], bucket[method_b]
                diff = va - vb
                if not minimize:
                    diff = -diff
                if abs(diff) <= tolerance:
                    ties += 1
                elif diff < 0:
                    wins += 1
                else:
                    losses += 1
            if paired == 0:
                continue
            results.append({
                "metric": metric,
                "method_a": method_a,
                "method_b": method_b,
                "method_a_wins": wins,
                "ties": ties,
                "method_b_wins": losses,
                "paired_observations": paired,
            })
    return results


def all_win_tie_loss(rows: list, *, tolerance: float = core.DEFAULT_GAP_TOLERANCE) -> list:
    out = []
    out.extend(win_tie_loss(
        rows, metric="in_sample_weighted_fpp_objective",
        group_key_fn=lambda r: schema.comparison_group_key(r),
        metric_field_fn=lambda r: r.get("fpp_comparison_objective_in_sample"),
        tolerance=tolerance,
    ))
    out.extend(win_tie_loss(
        rows, metric="out_of_sample_weighted_objective",
        group_key_fn=core.out_of_sample_comparison_key,
        metric_field_fn=lambda r: r.get(core.oos_metric_field(r)),
        tolerance=tolerance,
    ))
    out.extend(win_tie_loss(
        rows, metric="paired_reburn_weighted_objective",
        group_key_fn=core.paired_reburn_comparison_key,
        metric_field_fn=lambda r: r.get(core.paired_metric_field(r)),
        filter_fn=core.paired_eligible,
        tolerance=tolerance,
    ))
    out.extend(win_tie_loss(
        rows, metric="runtime_among_mutually_successful_exact_runs",
        group_key_fn=lambda r: schema.comparison_group_key(r),
        metric_field_fn=lambda r: r.get("running_time_sec"),
        filter_fn=lambda r: r.get("objective_space") == core.OBJECTIVE_SPACE_EXACT_FPP and schema.is_valid_exact_result(r),
        tolerance=tolerance,
    ))
    return out


# ---------------------------------------------------------------------------
# Replicate-level and replicate-aggregated summaries (Phase 10 section 12).
#
# By default every table in this module (method_summary, etc.) already
# retains weight_replicate as a distinct grouping dimension -- nothing here
# ever averages across replicates unless the caller explicitly asks for the
# *_aggregated variant, and raw weight_map_hash values are always preserved
# in the replicate-level table (maps themselves are never averaged, only
# the resulting metric values are).
# ---------------------------------------------------------------------------

def replicate_level_method_summary(rows: list, *, metric_field: str, metric_name: str) -> list:
    """One row per (method, weight_profile, risk_measure, canonical_landscape_id,
    weight_replicate, weight_map_hash) -- full granularity, raw map hash
    preserved, nothing pooled."""
    groups: dict = {}
    for r in rows:
        key = (r.get("method"), r.get("weight_profile"), r.get("risk_measure"),
               r.get("canonical_landscape_id"), r.get("weight_replicate"), r.get("weight_map_hash"))
        groups.setdefault(key, []).append(r)

    out = []
    for (method, profile, risk, landscape, replicate, map_hash), group in sorted(
        groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "", kv[0][3] or "", kv[0][4] if kv[0][4] is not None else -1)
    ):
        values = [r.get(metric_field) for r in group]
        out.append({
            "method": method,
            "weight_profile": profile,
            "risk_measure": risk,
            "canonical_landscape_id": landscape,
            "weight_replicate": replicate,
            "weight_map_hash": map_hash,
            "observations": len(group),
            "metric": metric_name,
            "mean_metric": mean_or_none(values),
            "median_metric": median_or_none(values),
        })
    return out


def replicate_aggregated_method_summary(rows: list, *, metric_field: str, metric_name: str) -> list:
    """One row per (method, weight_profile, risk_measure), aggregated ACROSS
    weight replicates and physical landscapes -- explicitly labeled as such.
    `between_replicate_std` is the standard deviation of each replicate's
    OWN mean (variability BETWEEN replicates, not the pooled raw std across
    every observation) -- left as None (never fabricated as 0.0) when fewer
    than two replicates contributed, since a single replicate cannot
    evidence any between-replicate variability at all."""
    groups: dict = {}
    for r in rows:
        key = (r.get("method"), r.get("weight_profile"), r.get("risk_measure"))
        groups.setdefault(key, []).append(r)

    out = []
    for (method, profile, risk), group in sorted(groups.items(), key=lambda kv: (kv[0][0] or "", kv[0][1] or "")):
        landscapes = {r.get("canonical_landscape_id") for r in group if r.get("canonical_landscape_id")}
        replicates = {r.get("weight_replicate") for r in group if r.get("weight_replicate") is not None}
        values = [r.get(metric_field) for r in group]

        per_replicate_means = []
        for replicate in sorted(replicates, key=lambda v: (v is None, v)):
            replicate_values = [r.get(metric_field) for r in group if r.get("weight_replicate") == replicate]
            replicate_mean = mean_or_none(replicate_values)
            if replicate_mean is not None:
                per_replicate_means.append(replicate_mean)
        between_replicate_std = (
            statistics.stdev(per_replicate_means) if len(per_replicate_means) >= 2 else None
        )

        out.append({
            "weight_profile": profile,
            "method": method,
            "risk_measure": risk,
            "physical_landscapes": len(landscapes),
            "weight_replicates": len(replicates),
            "observations": len(group),
            "metric": metric_name,
            "mean_metric": mean_or_none(values),
            "median_metric": median_or_none(values),
            "between_replicate_std": between_replicate_std,
        })
    return out
