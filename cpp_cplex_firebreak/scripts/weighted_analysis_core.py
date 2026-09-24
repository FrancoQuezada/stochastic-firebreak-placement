#!/usr/bin/env python3
"""Phase 9B core analysis primitives: method classification, comparison
objective selection, comparison groups, best-known feasible values, best-
known lower bounds, certification gaps, per-row gaps, and out-of-sample /
paired-reburn observed regret.

This module deliberately contains NO result parsing, NO schema definition,
and NO comparison-group-key reimplementation: it imports and reuses
`weighted_result_schema` (Phase 9A) for all of that, and only adds the
Phase-9B-specific analysis layer on top (per Phase 9B section 3).

Nothing here executes a solver, regenerates a weight map, or mutates a
selected solution -- it only classifies and compares data already produced
by Phase 9A's merge pipeline.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import weighted_result_schema as schema

# ---------------------------------------------------------------------------
# Tolerances (documented defaults, all overridable via the CLI).
# ---------------------------------------------------------------------------

DEFAULT_GAP_TOLERANCE = 1.0e-6
"""Absolute-noise tolerance: a gap whose magnitude is within this many
absolute units of zero (after the max(|z|, eps) normalization) is reported
as exactly 0.0 rather than a small negative/positive artifact of floating
point noise (Phase 9B section 10)."""

DEFAULT_OPTIMALITY_TOLERANCE = 1.0e-4
"""Relative tolerance on the best-known certification gap
(Gap^BK = (z_bk - LB_bk) / max(|z_bk|, eps)) below which a best-known
feasible value is reported as certified optimal (Phase 9B section 9)."""

_EPS = 1.0e-9
"""Generic denominator floor, matching the max(|x|, epsilon) in every
formula in the Phase 9B spec."""

# ---------------------------------------------------------------------------
# Objective-space classification (section 5)
# ---------------------------------------------------------------------------

OBJECTIVE_SPACE_EXACT_FPP = "exact_fpp"
OBJECTIVE_SPACE_APPROXIMATE_FPP = "approximate_fpp"
OBJECTIVE_SPACE_DPV_OPTIMIZATION = "dpv_optimization"
OBJECTIVE_SPACE_HEURISTIC = "heuristic"
OBJECTIVE_SPACE_UNCLASSIFIED = "unclassified_method"

OBJECTIVE_SPACES = (
    OBJECTIVE_SPACE_EXACT_FPP,
    OBJECTIVE_SPACE_APPROXIMATE_FPP,
    OBJECTIVE_SPACE_DPV_OPTIMIZATION,
    OBJECTIVE_SPACE_HEURISTIC,
    OBJECTIVE_SPACE_UNCLASSIFIED,
)


def classify_method(record: dict) -> str:
    """Classify a normalized canonical record into one of the four required
    objective-space buckets (plus an explicit "unclassified_method" escape
    hatch that is never silently treated as exact).

    Precedence, using explicit Phase 9A metadata rather than substring
    matching wherever it exists:
      1. execution_status == "heuristic_completed" -> heuristic. This is
         checked FIRST because a construction method (e.g. Static-DPV) also
         populates dpv_surrogate_objective -- the heuristic-execution signal
         takes priority over the surrogate-objective signal (section 5:
         "Construction methods such as Static-DPV ... are heuristic").
      2. dpv_surrogate_objective is populated (and not heuristic) ->
         dpv_optimization -- an actual MIP/Benders solve of the DPV
         surrogate (DPV-SAA, DPV-Benders, DPV-Branch-Benders).
      3. restricted_candidate_mode == "heuristic" (the restricted-candidate
         search stopped without a certified global bound) -> approximate_fpp,
         even though the method label starts with "FPP".
      4. method starts with "FPP" and restricted_candidate_mode is one of
         "none"/"exact" (i.e. no restriction, or a certified-exact
         restriction) -> exact_fpp.
      5. method starts with "FPP" but restricted_candidate_mode == "enabled"
         (restricted-candidate active with neither exact nor heuristic
         explicitly confirmed) -> approximate_fpp (never assume exactness
         without positive confirmation).
      6. anything else -> unclassified_method, reported in diagnostics,
         never silently compared as if it were exact.
    """
    if record.get("execution_status") == "heuristic_completed":
        return OBJECTIVE_SPACE_HEURISTIC
    if record.get("dpv_surrogate_objective") is not None:
        return OBJECTIVE_SPACE_DPV_OPTIMIZATION

    mode = record.get("restricted_candidate_mode") or "none"
    method = str(record.get("method") or "")
    if mode == "heuristic":
        return OBJECTIVE_SPACE_APPROXIMATE_FPP
    if method.startswith("FPP"):
        if mode in ("none", "exact"):
            return OBJECTIVE_SPACE_EXACT_FPP
        return OBJECTIVE_SPACE_APPROXIMATE_FPP
    return OBJECTIVE_SPACE_UNCLASSIFIED


# ---------------------------------------------------------------------------
# Canonical FPP comparison objective (section 4)
# ---------------------------------------------------------------------------

_RISK_METRIC_FIELDS = {
    "mean_cvar": "weighted_fpp_mean_cvar_in_sample",
    "cvar": "weighted_fpp_cvar_in_sample",
    "expected": "weighted_fpp_expected_in_sample",
}


def _risk_bucket(risk_measure: str) -> str:
    risk = (risk_measure or "").lower().replace("-", "_").replace(" ", "_")
    if "mean" in risk and "cvar" in risk:
        return "mean_cvar"
    if "cvar" in risk:
        return "cvar"
    return "expected"


def select_comparison_objective(record: dict) -> tuple:
    """Return (value, source_field, rejection_reason). Exactly one exact
    evaluator metric is selected per the row's own risk_measure -- never a
    DPV surrogate objective, never a physical burned-cell count, never an
    out-of-sample metric used as an in-sample objective (section 4)."""
    bucket = _risk_bucket(record.get("risk_measure"))
    source_field = _RISK_METRIC_FIELDS[bucket]
    value = record.get(source_field)
    if value is None:
        return None, source_field, f"missing required in-sample metric {source_field} for risk_measure={record.get('risk_measure')!r}"
    return value, source_field, None


def enrich_with_comparison_objective(record: dict) -> dict:
    """Return a shallow copy of `record` with `fpp_comparison_objective_in_sample`,
    `fpp_comparison_objective_in_sample_source`, and `objective_space` added."""
    out = dict(record)
    value, source_field, reason = select_comparison_objective(record)
    out["fpp_comparison_objective_in_sample"] = value
    out["fpp_comparison_objective_in_sample_source"] = source_field
    out["fpp_comparison_objective_in_sample_rejection_reason"] = reason
    out["objective_space"] = classify_method(record)
    return out


# ---------------------------------------------------------------------------
# Comparison groups (section 6) -- reuses schema.comparison_group_key(),
# does not reimplement it.
# ---------------------------------------------------------------------------

def build_comparison_groups(records: list) -> dict:
    """Group enriched records by the (extended) Phase 9A comparison-group
    key. Returns dict[ComparisonGroupKey] -> list[record]."""
    groups: dict = {}
    for record in records:
        key = schema.comparison_group_key(record)
        groups.setdefault(key, []).append(record)
    return groups


def comparison_group_row(key) -> dict:
    """Flatten a ComparisonGroupKey into a CSV-friendly row."""
    return {
        "canonical_landscape_id": key.canonical_landscape_id,
        "instance_family": key.instance_family,
        "instance_id": key.instance_id,
        "train_scenario_ids": ",".join(str(v) for v in key.train_scenario_ids),
        "train_scenario_count": key.train_scenario_count,
        "weight_profile": key.weight_profile,
        "weight_replicate": key.weight_replicate,
        "weight_map_hash": key.weight_map_hash,
        "risk_measure": key.risk_measure,
        "alpha": key.alpha,
        "cvar_beta": key.cvar_beta,
        "mean_cvar_lambda": key.mean_cvar_lambda,
        "budget": key.budget,
    }


@dataclass(frozen=True)
class NamespaceComparisonKey:
    """A namespace-scoped compatibility key for OOS / paired-reburn
    comparisons (section 13/14). Distinct from `schema.ComparisonGroupKey`
    (which is specifically the in-sample best-known-FPP group key) because
    OOS and paired-reburn compatibility requires different scenario-identity
    fields (test IDs / paired scenario IDs instead of train IDs) -- this is
    the namespace generalization Phase 9A explicitly deferred to Phase 9B
    (Phase 9A section 9), not a second implementation of the same concept.
    """
    namespace: str
    canonical_landscape_id: str
    instance_id: str
    scenario_ids: tuple
    weight_map_hash: str
    risk_measure: str
    alpha: float
    cvar_beta: float
    mean_cvar_lambda: float
    budget: int


def out_of_sample_comparison_key(record: dict) -> NamespaceComparisonKey:
    return NamespaceComparisonKey(
        namespace="out_of_sample",
        canonical_landscape_id=str(record.get("canonical_landscape_id", "")),
        instance_id=str(record.get("instance_id", "")),
        scenario_ids=tuple(record.get("out_of_sample_scenario_ids") or ()),
        weight_map_hash=str(record.get("out_of_sample_weight_map_hash") or record.get("weight_map_hash", "")),
        risk_measure=str(record.get("risk_measure", "")),
        alpha=float(record.get("alpha") or 0.0),
        cvar_beta=float(record.get("cvar_beta") or 0.0),
        mean_cvar_lambda=float(record.get("mean_cvar_lambda") or 0.0),
        budget=int(record.get("budget") or 0),
    )


def paired_reburn_comparison_key(record: dict) -> NamespaceComparisonKey:
    return NamespaceComparisonKey(
        namespace="paired_reburn",
        canonical_landscape_id=str(record.get("canonical_landscape_id", "")),
        instance_id=str(record.get("paired_reburn_instance_id", "")),
        scenario_ids=tuple(record.get("paired_reburn_scenario_ids") or ()),
        weight_map_hash=str(record.get("paired_reburn_weight_map_hash") or record.get("weight_map_hash", "")),
        risk_measure=str(record.get("risk_measure", "")),
        alpha=float(record.get("alpha") or 0.0),
        cvar_beta=float(record.get("cvar_beta") or 0.0),
        mean_cvar_lambda=float(record.get("mean_cvar_lambda") or 0.0),
        budget=int(record.get("budget") or 0),
    )


# ---------------------------------------------------------------------------
# Best-known feasible FPP value (section 7)
# ---------------------------------------------------------------------------

def _is_finite(value) -> bool:
    return value is not None and isinstance(value, (int, float)) and math.isfinite(value)


def best_known_feasible(group_records: list, *, tie_tolerance: float = 1.0e-9) -> dict:
    """Compute the best-known feasible FPP value for one comparison group.
    Only rows with objective_space == exact_fpp, valid terminal execution,
    a feasible/optimal solver status, evaluator validation passed, and a
    finite fpp_comparison_objective_in_sample are eligible (section 7)."""
    eligible = [
        r for r in group_records
        if r.get("objective_space") == OBJECTIVE_SPACE_EXACT_FPP
        and schema.is_valid_exact_result(r)
        and _is_finite(r.get("fpp_comparison_objective_in_sample"))
    ]
    if not eligible:
        return {
            "best_known_feasible_value": None,
            "best_known_feasible_method": None,
            "best_known_feasible_run_id": None,
            "best_known_feasible_solver_status": None,
            "best_known_feasible_tied_run_ids": [],
            "best_known_feasible_source_count": 0,
        }
    best_value = min(r["fpp_comparison_objective_in_sample"] for r in eligible)
    tied = sorted(
        (r for r in eligible if abs(r["fpp_comparison_objective_in_sample"] - best_value) <= tie_tolerance),
        key=lambda r: (r.get("method", ""), r.get("run_id", "")),
    )
    winner = tied[0]
    return {
        "best_known_feasible_value": best_value,
        "best_known_feasible_method": winner.get("method"),
        "best_known_feasible_run_id": winner.get("run_id"),
        "best_known_feasible_solver_status": winner.get("solver_status"),
        "best_known_feasible_tied_run_ids": [r.get("run_id") for r in tied],
        "best_known_feasible_source_count": len(eligible),
    }


# ---------------------------------------------------------------------------
# Best-known lower bound (section 8)
# ---------------------------------------------------------------------------

def best_known_lower_bound(group_records: list, *, tie_tolerance: float = 1.0e-9) -> dict:
    """Compute the best (maximum, for minimization) known valid lower bound
    for one comparison group. Only original-FPP solver bounds are eligible
    -- DPV surrogate bounds are excluded by construction (objective_space
    filter); non-finite bounds are already excluded by strict numeric
    parsing at normalization time (section 8)."""
    eligible = [
        r for r in group_records
        if r.get("objective_space") == OBJECTIVE_SPACE_EXACT_FPP
        and _is_finite(r.get("solver_best_bound"))
    ]
    if not eligible:
        return {
            "best_known_lower_bound": None,
            "best_known_lower_bound_method": None,
            "best_known_lower_bound_run_id": None,
            "best_known_lower_bound_source_count": 0,
        }
    best_bound = max(r["solver_best_bound"] for r in eligible)
    tied = sorted(
        (r for r in eligible if abs(r["solver_best_bound"] - best_bound) <= tie_tolerance),
        key=lambda r: (r.get("method", ""), r.get("run_id", "")),
    )
    winner = tied[0]
    return {
        "best_known_lower_bound": best_bound,
        "best_known_lower_bound_method": winner.get("method"),
        "best_known_lower_bound_run_id": winner.get("run_id"),
        "best_known_lower_bound_source_count": len(eligible),
    }


def validate_bound_consistency(best_known_feasible_value, best_known_lower_bound_value, *, tolerance: float = DEFAULT_GAP_TOLERANCE) -> tuple:
    """LB_bk <= z_bk + epsilon must hold. Returns (ok, reason)."""
    if best_known_feasible_value is None or best_known_lower_bound_value is None:
        return True, None
    if best_known_lower_bound_value <= best_known_feasible_value + tolerance:
        return True, None
    return False, (
        f"best_known_lower_bound={best_known_lower_bound_value!r} exceeds "
        f"best_known_feasible_value={best_known_feasible_value!r} by more than tolerance={tolerance!r}"
    )


# ---------------------------------------------------------------------------
# Best-known certification gap (section 9)
# ---------------------------------------------------------------------------

def certification_gap(best_known_feasible_value, best_known_lower_bound_value, *,
                       optimality_tolerance: float = DEFAULT_OPTIMALITY_TOLERANCE,
                       proven_optimal: bool = False) -> dict:
    """Gap^BK = (z_bk - LB_bk) / max(|z_bk|, eps). certified_optimal is true
    when this gap is within `optimality_tolerance`, OR when at least one
    exact solver directly reported solver_status == 'Optimal' at the
    best-known value (`proven_optimal`, passed in by the caller)."""
    if best_known_feasible_value is None or best_known_lower_bound_value is None:
        return {"best_known_certification_gap": None, "best_known_certified_optimal": bool(proven_optimal)}
    gap = (best_known_feasible_value - best_known_lower_bound_value) / max(abs(best_known_feasible_value), _EPS)
    certified = proven_optimal or gap <= optimality_tolerance
    return {"best_known_certification_gap": gap, "best_known_certified_optimal": certified}


# ---------------------------------------------------------------------------
# Per-row gaps (sections 10-11)
# ---------------------------------------------------------------------------

def gap_to_best_known_feasible(value, best_known_feasible_value, *, noise_tolerance: float = DEFAULT_GAP_TOLERANCE) -> tuple:
    """Return (gap, flagged_negative). Minimization sign convention: positive
    means worse than best-known. A raw value within `noise_tolerance` below
    zero is reported as exactly 0.0. A materially negative gap is preserved
    (never hidden) and flagged -- see the module-level note in
    docs/WEIGHTED_LANDSCAPES_PHASE9B.md for why this can legitimately occur
    for DPV/heuristic rows but should never occur for exact_fpp rows."""
    if value is None or best_known_feasible_value is None:
        return None, False
    raw = (value - best_known_feasible_value) / max(abs(best_known_feasible_value), _EPS)
    if -noise_tolerance <= raw < 0.0:
        return 0.0, False
    if raw < -noise_tolerance:
        return raw, True
    return raw, False


def gap_to_best_known_lower_bound(value, best_known_lower_bound_value, *, noise_tolerance: float = DEFAULT_GAP_TOLERANCE) -> float | None:
    """Gap^LB = (z - LB_bk) / max(|z|, eps). Not a solver optimality gap for
    heuristic/DPV methods -- purely a distance-to-reference-bound metric."""
    if value is None or best_known_lower_bound_value is None:
        return None
    raw = (value - best_known_lower_bound_value) / max(abs(value), _EPS)
    if -noise_tolerance <= raw < 0.0:
        return 0.0
    return raw


# ---------------------------------------------------------------------------
# Out-of-sample observed regret (section 13)
# ---------------------------------------------------------------------------

_OOS_METRIC_FIELDS = {
    "mean_cvar": "weighted_fpp_mean_cvar_out_of_sample",
    "cvar": "weighted_fpp_cvar_out_of_sample",
    "expected": "weighted_fpp_expected_out_of_sample",
}
_PAIRED_METRIC_FIELDS = {
    "mean_cvar": "weighted_fpp_mean_cvar_paired_reburn",
    "cvar": "weighted_fpp_cvar_paired_reburn",
    "expected": "weighted_fpp_expected_paired_reburn",
}


def oos_metric_field(record: dict) -> str:
    return _OOS_METRIC_FIELDS[_risk_bucket(record.get("risk_measure"))]


def paired_metric_field(record: dict) -> str:
    return _PAIRED_METRIC_FIELDS[_risk_bucket(record.get("risk_measure"))]


def best_observed(records: list, metric_field_fn) -> dict:
    """Generic 'best observed' (never called optimal) reference: the
    minimum risk-consistent metric value among a compatible group of rows,
    regardless of method/objective-space (sections 13-14)."""
    eligible = [
        (r, r.get(metric_field_fn(r)))
        for r in records
    ]
    eligible = [(r, v) for r, v in eligible if _is_finite(v)]
    if not eligible:
        return {"best_observed_value": None, "best_observed_method": None, "best_observed_run_id": None, "best_observed_source_count": 0}
    best_value = min(v for _, v in eligible)
    tied = sorted(
        ((r, v) for r, v in eligible if abs(v - best_value) <= 1.0e-9),
        key=lambda rv: (rv[0].get("method", ""), rv[0].get("run_id", "")),
    )
    winner, _ = tied[0]
    return {
        "best_observed_value": best_value,
        "best_observed_method": winner.get("method"),
        "best_observed_run_id": winner.get("run_id"),
        "best_observed_source_count": len(eligible),
    }


def regret_to_best_observed(value, best_observed_value, *, noise_tolerance: float = DEFAULT_GAP_TOLERANCE) -> float | None:
    if value is None or best_observed_value is None:
        return None
    raw = (value - best_observed_value) / max(abs(best_observed_value), _EPS)
    if -noise_tolerance <= raw < 0.0:
        return 0.0
    return raw


def paired_eligible(record: dict) -> bool:
    """A row may contribute to / receive a paired-reburn regret only when
    its paired-reburn evaluation actually succeeded with zero missing
    transferred firebreaks (section 14) -- reuses the real gating signal
    fixed in weighted_result_schema (paired_reburn_evaluation_status, not
    the unreliable paired_evaluation_enabled flag)."""
    if record.get("paired_reburn_evaluation_status") != "ok":
        return False
    missing = record.get("paired_selected_firebreaks_missing")
    return missing in (0, None, "")


# ---------------------------------------------------------------------------
# Full row-level orchestration
# ---------------------------------------------------------------------------

def run_core_analysis(records: list, *, gap_tolerance: float = DEFAULT_GAP_TOLERANCE,
                       optimality_tolerance: float = DEFAULT_OPTIMALITY_TOLERANCE) -> dict:
    """Enrich every record, build all three comparison-group families
    (in-sample best-known, out-of-sample, paired-reburn), and attach every
    gap/regret field the rest of Phase 9B's tables/profiles consume.

    Returns {"rows": [...], "comparison_groups": [...], "diagnostics": {...}}.
    """
    enriched = [enrich_with_comparison_objective(r) for r in records]

    in_sample_groups = build_comparison_groups(enriched)
    group_summaries = []
    bound_violations = []
    for key, group_records in in_sample_groups.items():
        bk = best_known_feasible(group_records)
        lb = best_known_lower_bound(group_records)
        ok, reason = validate_bound_consistency(
            bk["best_known_feasible_value"], lb["best_known_lower_bound"], tolerance=gap_tolerance)
        proven_optimal = bk["best_known_feasible_solver_status"] == "Optimal"
        cert = certification_gap(
            bk["best_known_feasible_value"], lb["best_known_lower_bound"],
            optimality_tolerance=optimality_tolerance, proven_optimal=proven_optimal)
        if not ok:
            bound_violations.append({"group": comparison_group_row(key), "reason": reason})
        row = dict(comparison_group_row(key))
        row.update(bk)
        row.update(lb)
        row.update(cert)
        row["bound_consistency_ok"] = ok
        row["bound_consistency_reason"] = reason or ""
        row["group_size"] = len(group_records)
        group_summaries.append(row)

        for record in group_records:
            record["best_known_feasible_value"] = bk["best_known_feasible_value"]
            record["best_known_feasible_method"] = bk["best_known_feasible_method"]
            record["best_known_feasible_run_id"] = bk["best_known_feasible_run_id"]
            record["best_known_lower_bound"] = lb["best_known_lower_bound"]
            record["best_known_lower_bound_method"] = lb["best_known_lower_bound_method"]
            record["best_known_certification_gap"] = cert["best_known_certification_gap"]
            record["best_known_certified_optimal"] = cert["best_known_certified_optimal"]

            gap_feasible, flagged = gap_to_best_known_feasible(
                record.get("fpp_comparison_objective_in_sample"), bk["best_known_feasible_value"],
                noise_tolerance=gap_tolerance)
            record["gap_to_best_known_feasible"] = gap_feasible
            record["gap_to_best_known_feasible_percent"] = None if gap_feasible is None else gap_feasible * 100.0
            record["gap_to_best_known_feasible_flagged_negative"] = flagged

            gap_lb = gap_to_best_known_lower_bound(
                record.get("fpp_comparison_objective_in_sample"), lb["best_known_lower_bound"],
                noise_tolerance=gap_tolerance)
            record["gap_to_best_known_lower_bound"] = gap_lb
            record["gap_to_best_known_lower_bound_percent"] = None if gap_lb is None else gap_lb * 100.0

    oos_groups: dict = {}
    for record in enriched:
        if record.get(oos_metric_field(record)) is None:
            continue
        oos_groups.setdefault(out_of_sample_comparison_key(record), []).append(record)
    for key, group_records in oos_groups.items():
        best = best_observed(group_records, oos_metric_field)
        for record in group_records:
            record["best_observed_out_of_sample_value"] = best["best_observed_value"]
            record["best_observed_out_of_sample_method"] = best["best_observed_method"]
            regret = regret_to_best_observed(
                record.get(oos_metric_field(record)), best["best_observed_value"], noise_tolerance=gap_tolerance)
            record["out_of_sample_regret_to_best_observed"] = regret
            record["out_of_sample_regret_to_best_observed_percent"] = None if regret is None else regret * 100.0

    paired_groups: dict = {}
    for record in enriched:
        if not paired_eligible(record) or record.get(paired_metric_field(record)) is None:
            continue
        paired_groups.setdefault(paired_reburn_comparison_key(record), []).append(record)
    for key, group_records in paired_groups.items():
        best = best_observed(group_records, paired_metric_field)
        for record in group_records:
            record["best_observed_paired_reburn_value"] = best["best_observed_value"]
            record["best_observed_paired_reburn_method"] = best["best_observed_method"]
            regret = regret_to_best_observed(
                record.get(paired_metric_field(record)), best["best_observed_value"], noise_tolerance=gap_tolerance)
            record["paired_reburn_regret_to_best_observed"] = regret
            record["paired_reburn_regret_to_best_observed_percent"] = None if regret is None else regret * 100.0

    diagnostics = {
        "comparison_groups": len(in_sample_groups),
        "groups_with_exact_reference": sum(1 for g in group_summaries if g["best_known_feasible_value"] is not None),
        "groups_without_exact_reference": sum(1 for g in group_summaries if g["best_known_feasible_value"] is None),
        "groups_with_certified_optimum": sum(1 for g in group_summaries if g["best_known_certified_optimal"]),
        "groups_with_uncertified_best_known": sum(
            1 for g in group_summaries if g["best_known_feasible_value"] is not None and not g["best_known_certified_optimal"]),
        "rows_with_gap": sum(1 for r in enriched if r.get("gap_to_best_known_feasible") is not None),
        "rows_without_gap": sum(1 for r in enriched if r.get("gap_to_best_known_feasible") is None),
        "oos_groups": len(oos_groups),
        "paired_groups": len(paired_groups),
        "bound_consistency_violations": bound_violations,
        "rows_flagged_negative_gap": sum(1 for r in enriched if r.get("gap_to_best_known_feasible_flagged_negative")),
    }

    return {"rows": enriched, "comparison_groups": group_summaries, "diagnostics": diagnostics}
