#!/usr/bin/env python3
"""Phase 9A canonical result schema for weighted-landscape experiment rows.

This module is the single source of truth for:
  - the canonical, versioned field list produced by the merge pipeline
    (`merge_weighted_experiment_results.py`);
  - the mapping from that canonical field list back to the raw column names
    emitted today by the C++ `StandardExperimentResult` writer, the Python
    manifest worker (`run_fpp_new_instances_scaling_manifest_worker.py`), and
    the per-task JSON files it writes alongside each worker CSV row;
  - the objective-space separation (solver objective vs. DPV surrogate
    objective vs. exact weighted-FPP evaluation vs. unweighted physical
    burned-cell counts) required so a merged dataset can never conflate them;
  - the validation-classification taxonomy and comparison-group-key /
    status-filter helpers shared by the merge pipeline and by Phase 9B.

Nothing in this module executes a solver, regenerates a weight map, or
mutates a selected solution. It only names and classifies data that other
parts of the pipeline already produce.

See docs/WEIGHTED_LANDSCAPES_PHASE9A.md for the full field-by-field audit
this schema is derived from.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

from fpp_new_instances_scaling_compact_schema import (
    any_bool,
    bool_value,
    is_missing,
    method_tokens,
    projected_family,
)

# ---------------------------------------------------------------------------
# Schema version
# ---------------------------------------------------------------------------

# The canonical schema version emitted by THIS pipeline. Every row this
# module normalizes is stamped with this value unless it was migrated from a
# legacy source, in which case it gets one of the LEGACY_SCHEMA_VERSIONS
# below plus an explicit migration flag (never silently relabeled as
# RESULT_SCHEMA_VERSION).
RESULT_SCHEMA_VERSION = "weighted-result-9a.1"

# Legacy versions this pipeline knows how to migrate. Detected from raw
# column presence ONLY when the row has no explicit `result_schema_version`
# field at all -- a modern row that happens to leave an optional field blank
# is never downgraded by this heuristic (Phase 9A section 3).
LEGACY_SCHEMA_UNWEIGHTED = "legacy-unweighted-pre8a"
LEGACY_SCHEMA_WEIGHTED_PRE8A = "legacy-weighted-pre8a"

# Schema versions this pipeline accepts as input (modern + known legacy).
# Any other explicit `result_schema_version` value fails clearly rather than
# being silently coerced.
SUPPORTED_SCHEMA_VERSIONS = frozenset({
    RESULT_SCHEMA_VERSION,
    LEGACY_SCHEMA_UNWEIGHTED,
    LEGACY_SCHEMA_WEIGHTED_PRE8A,
})

# Columns that, if present with a non-empty value, indicate a Phase 8A/8B
# canonical-registry row (as opposed to the Phase 5A "weighted but
# pre-registry" legacy shape, or the fully unweighted legacy shape).
_CANONICAL_REGISTRY_MARKER_FIELDS = (
    "canonical_landscape_id",
    "weight_replicate",
    "weight_generation_seed",
    "weight_generator_version",
    "weight_source_universe_hash",
    "weight_mapping_method",
)
_WEIGHTED_MARKER_FIELDS = ("weight_profile", "weight_map_hash")


def detect_legacy_schema_version(raw_row: dict) -> str:
    """Infer a legacy schema version from column presence.

    Only called when `raw_row` carries no explicit `result_schema_version`
    field. A row is "canonical registry" (i.e. produced by this pipeline,
    even if it forgot to stamp a version) when it carries the Phase 8A/8B
    provenance columns; "weighted pre-8A" when it has weight columns but not
    the registry ones (e.g. results/weighted_phase5a_smoke/*); otherwise
    fully unweighted legacy.
    """
    if any(not is_missing(raw_row.get(f)) for f in _CANONICAL_REGISTRY_MARKER_FIELDS):
        return RESULT_SCHEMA_VERSION
    if any(not is_missing(raw_row.get(f)) for f in _WEIGHTED_MARKER_FIELDS):
        return LEGACY_SCHEMA_WEIGHTED_PRE8A
    return LEGACY_SCHEMA_UNWEIGHTED


# ---------------------------------------------------------------------------
# Canonical field declarations
# ---------------------------------------------------------------------------

TYPE_STR = "str"
TYPE_FLOAT = "float"
TYPE_INT = "int"
TYPE_BOOL = "bool"
TYPE_LIST_INT = "list_int"


@dataclass(frozen=True)
class CanonicalField:
    name: str
    group: str
    dtype: str
    # Raw column names to try, in priority order. Empty for fields this
    # module derives rather than copies.
    raw_aliases: tuple = ()
    # True if this field can only be recovered from a per-task JSON payload
    # today (documented, real gap -- see docs/WEIGHTED_LANDSCAPES_PHASE9A.md).
    json_only: bool = False
    # JSON payload this field is read from when json_only or when JSON is
    # preferred: "solver" (worker_NNN_task_NNN.json) or "paired_reburn"
    # (worker_NNN_task_NNN_paired_reburn_eval.json).
    json_source: str = "solver"
    json_key: str = ""


# NOTE ON DERIVED FIELDS: fields with empty raw_aliases are computed by
# scripts/weighted_result_normalize.py, not copied verbatim. They are still
# declared here (with dtype and group) so column order, type validation, and
# documentation stay centralized.
CANONICAL_FIELDS: tuple = (
    # -- identity ------------------------------------------------------
    CanonicalField("result_schema_version", "identity", TYPE_STR),
    CanonicalField("run_id", "identity", TYPE_STR, ("run_id",)),
    CanonicalField("logical_run_key", "identity", TYPE_STR),
    CanonicalField("experiment_id", "identity", TYPE_STR, ("experiment_id",)),
    CanonicalField("case_id", "identity", TYPE_STR, ("case_id",)),
    CanonicalField("instance_id", "identity", TYPE_STR, ("instance_id", "landscape", "folder_name")),
    CanonicalField("instance_family", "identity", TYPE_STR, ("instance_type", "instance_group", "folder_name")),
    CanonicalField("landscape", "identity", TYPE_STR, ("landscape",)),
    CanonicalField("folder_name", "identity", TYPE_STR, ("folder_name",)),

    # -- execution identity (section 11) --------------------------------
    CanonicalField("attempt", "execution", TYPE_INT, ("attempt",)),
    CanonicalField("execution_status", "execution", TYPE_STR, ("execution_status",)),
    CanonicalField("solver_status", "execution", TYPE_STR, ("solver_status", "status")),
    CanonicalField("resume_action", "execution", TYPE_STR, ("resume_action",)),
    CanonicalField("failure_stage", "execution", TYPE_STR, ("failure_stage",)),
    CanonicalField("failure_type", "execution", TYPE_STR, ("failure_type",)),
    CanonicalField("failure_message", "execution", TYPE_STR, ("failure_message",)),
    CanonicalField("worker_exit_code", "execution", TYPE_STR, ("worker_exit_code", "worker_return_code")),
    CanonicalField("worker_return_code", "execution", TYPE_STR, ("worker_return_code",)),
    CanonicalField("worker_started_at_epoch", "execution", TYPE_FLOAT, ("worker_started_at_epoch",)),
    CanonicalField("worker_finished_at_epoch", "execution", TYPE_FLOAT, ("worker_finished_at_epoch",)),
    CanonicalField("running_time_sec", "execution", TYPE_FLOAT, ("worker_runtime_seconds", "runtime_seconds")),

    # -- method identity (section 10) ------------------------------------
    CanonicalField("method", "method_identity", TYPE_STR, ("method",)),
    CanonicalField("method_family", "method_identity", TYPE_STR, ("method_family",)),
    CanonicalField("method_variant", "method_identity", TYPE_STR, ("fpp_mode", "objective_family")),
    CanonicalField("solver_formulation", "method_identity", TYPE_STR, ("formulation",)),
    CanonicalField("llbi_type", "method_identity", TYPE_STR),  # derived
    CanonicalField("combinatorial_mode", "method_identity", TYPE_STR, ("combinatorial_mode", "combinatorial_benders_mode")),
    CanonicalField("lifting_mode", "method_identity", TYPE_STR, ("combinatorial_lifting_mode", "combinatorial_benders_lift_mode")),
    CanonicalField("scenario_order", "method_identity", TYPE_STR, ("combinatorial_scenario_order", "combinatorial_benders_scenario_order")),
    CanonicalField("sampling_ratio", "method_identity", TYPE_FLOAT, ("combinatorial_cut_sampling_ratio", "combinatorial_benders_cut_sampling_ratio")),
    CanonicalField("dpv_variant", "method_identity", TYPE_STR, ("dpv_variant",)),
    CanonicalField("dpv_ignition_policy", "method_identity", TYPE_STR, ("dpv_ignition_policy",)),
    CanonicalField("restricted_candidate_mode", "method_identity", TYPE_STR),  # derived

    # -- risk metadata (section 7) ---------------------------------------
    CanonicalField("risk_measure", "risk", TYPE_STR, ("risk_measure",)),
    CanonicalField("cvar_beta", "risk", TYPE_FLOAT, ("cvar_beta",)),
    CanonicalField("mean_cvar_lambda", "risk", TYPE_FLOAT, ("cvar_lambda",)),
    CanonicalField("alpha", "risk", TYPE_FLOAT, ("alpha",)),
    CanonicalField("budget", "risk", TYPE_INT, ("budget",)),

    # -- weight provenance (section 8) -----------------------------------
    CanonicalField("canonical_landscape_id", "weight_provenance", TYPE_STR, ("canonical_landscape_id",)),
    CanonicalField("paired_landscape_id", "weight_provenance", TYPE_STR, ("paired_landscape_id",)),
    CanonicalField("weight_profile", "weight_provenance", TYPE_STR, ("weight_profile",)),
    CanonicalField("weight_replicate", "weight_provenance", TYPE_INT, ("weight_replicate",)),
    CanonicalField("weight_generation_seed", "weight_provenance", TYPE_STR, ("weight_generation_seed",)),
    CanonicalField("weight_generator_version", "weight_provenance", TYPE_INT, ("weight_generator_version",)),
    CanonicalField("weight_map_path", "weight_provenance", TYPE_STR, ("weight_map_file",)),
    CanonicalField("weight_map_hash", "weight_provenance", TYPE_STR, ("weight_map_hash",)),
    CanonicalField("weight_source_universe_hash", "weight_provenance", TYPE_STR, ("weight_source_universe_hash",)),
    CanonicalField("weight_normalization_mode", "weight_provenance", TYPE_STR),  # derived from weight_normalized
    CanonicalField("weight_mapping_method", "weight_provenance", TYPE_STR, ("weight_mapping_method",)),
    CanonicalField("weight_mapping_hash", "weight_provenance", TYPE_STR, ("weight_mapping_hash",)),
    CanonicalField("manifest_weight_map_hash", "weight_provenance", TYPE_STR, ("weight_map_hash",)),
    CanonicalField("optimization_weight_map_hash", "weight_provenance", TYPE_STR, ("optimization_weight_map_hash",)),
    CanonicalField("in_sample_weight_map_hash", "weight_provenance", TYPE_STR, ("optimization_weight_map_hash",)),
    CanonicalField("out_of_sample_weight_map_hash", "weight_provenance", TYPE_STR, ("out_of_sample_weight_map_hash",)),
    CanonicalField("paired_reburn_weight_map_hash", "weight_provenance", TYPE_STR, ("paired_reburn_weight_map_hash",)),

    # -- scenario provenance (section 9) ---------------------------------
    CanonicalField("train_ids", "scenario_provenance", TYPE_LIST_INT, ("train_ids",)),
    CanonicalField("test_ids", "scenario_provenance", TYPE_LIST_INT, ("test_ids",)),
    CanonicalField("train_scenario_count", "scenario_provenance", TYPE_INT, ("train_scenario_count", "train_count")),
    CanonicalField("test_scenario_count", "scenario_provenance", TYPE_INT, ("test_scenario_count", "test_count")),
    # Paired reburn currently re-evaluates the SAME train scenario IDs
    # against the reburn instance -- there is no independent paired-reburn
    # scenario ID list today, so this is an explicit, documented alias, not
    # an independent raw column (Phase 9A section 9).
    CanonicalField("paired_reburn_scenario_ids", "scenario_provenance", TYPE_LIST_INT, ("train_ids",)),
    CanonicalField("paired_reburn_scenario_count", "scenario_provenance", TYPE_INT, ("paired_reburn_train_scenario_count",)),

    # -- solver objective: exact weighted FPP MIP (section 4.1) ----------
    # Derived, NEVER copied from objective_in_sample when the row is a DPV
    # surrogate / heuristic row (see normalize.derive_solver_objective).
    CanonicalField("solver_objective", "solver_objective", TYPE_FLOAT),
    CanonicalField("solver_best_bound", "solver_objective", TYPE_FLOAT),
    CanonicalField("solver_gap", "solver_objective", TYPE_FLOAT),

    # -- DPV surrogate objective (section 4.2) ---------------------------
    CanonicalField("dpv_surrogate_objective", "dpv_surrogate", TYPE_FLOAT, ("dpv_surrogate_objective",)),
    CanonicalField("dpv_surrogate_best_bound", "dpv_surrogate", TYPE_FLOAT, ("dpv_surrogate_best_bound",)),
    CanonicalField("dpv_surrogate_gap", "dpv_surrogate", TYPE_FLOAT, ("dpv_surrogate_gap",)),

    # -- evaluation namespace: in_sample (section 6) ----------------------
    CanonicalField("in_sample_evaluation_status", "evaluation_in_sample", TYPE_STR),  # derived
    CanonicalField("in_sample_scenario_count", "evaluation_in_sample", TYPE_INT, ("train_scenario_count", "train_count")),
    CanonicalField("in_sample_scenario_ids", "evaluation_in_sample", TYPE_LIST_INT, ("train_ids",)),
    CanonicalField("weighted_fpp_expected_in_sample", "evaluation_in_sample", TYPE_FLOAT, ("train_expected_weighted_burn_loss",)),
    CanonicalField("weighted_fpp_cvar_in_sample", "evaluation_in_sample", TYPE_FLOAT, ("train_weighted_cvar",)),
    CanonicalField("weighted_fpp_mean_cvar_in_sample", "evaluation_in_sample", TYPE_FLOAT),  # derived
    CanonicalField("mean_burned_cells_in_sample", "evaluation_in_sample", TYPE_FLOAT, ("train_expected_burned_area",)),
    CanonicalField("in_sample_evaluation_time_sec", "evaluation_in_sample", TYPE_FLOAT, ("train_evaluation_runtime_seconds", "train_eval_runtime_sec")),

    # -- evaluation namespace: out_of_sample (section 6) ------------------
    CanonicalField("out_of_sample_evaluation_status", "evaluation_out_of_sample", TYPE_STR),  # derived
    CanonicalField("out_of_sample_scenario_count", "evaluation_out_of_sample", TYPE_INT, ("test_scenario_count", "test_count")),
    CanonicalField("out_of_sample_scenario_ids", "evaluation_out_of_sample", TYPE_LIST_INT, ("test_ids",)),
    CanonicalField("weighted_fpp_expected_out_of_sample", "evaluation_out_of_sample", TYPE_FLOAT, ("test_expected_weighted_burn_loss",)),
    CanonicalField("weighted_fpp_cvar_out_of_sample", "evaluation_out_of_sample", TYPE_FLOAT, ("test_weighted_cvar",)),
    CanonicalField("weighted_fpp_mean_cvar_out_of_sample", "evaluation_out_of_sample", TYPE_FLOAT),  # derived
    CanonicalField("mean_burned_cells_out_of_sample", "evaluation_out_of_sample", TYPE_FLOAT, ("test_expected_burned_area",)),
    CanonicalField("out_of_sample_evaluation_time_sec", "evaluation_out_of_sample", TYPE_FLOAT, ("test_evaluation_runtime_seconds", "test_eval_runtime_sec")),

    # -- evaluation namespace: paired_reburn (section 6) ------------------
    CanonicalField("paired_reburn_evaluation_status", "evaluation_paired_reburn", TYPE_STR, ("paired_reburn_status",)),
    CanonicalField("paired_reburn_evaluation_time_sec", "evaluation_paired_reburn", TYPE_FLOAT,
                   ("paired_reburn_train_eval_runtime_sec", "paired_reburn_train_evaluation_runtime_seconds")),
    # These two are the confirmed, documented gap: the paired-reburn
    # evaluation JSON already computes the true weighted loss, but the
    # worker CSV never captures it -- only per-task JSON has it today.
    CanonicalField("weighted_fpp_expected_paired_reburn", "evaluation_paired_reburn", TYPE_FLOAT,
                   json_only=True, json_source="paired_reburn", json_key="expected_weighted_burn_loss"),
    CanonicalField("weighted_fpp_cvar_paired_reburn", "evaluation_paired_reburn", TYPE_FLOAT,
                   json_only=True, json_source="paired_reburn", json_key="weighted_cvar"),
    CanonicalField("weighted_fpp_mean_cvar_paired_reburn", "evaluation_paired_reburn", TYPE_FLOAT),  # derived
    CanonicalField("mean_burned_cells_paired_reburn", "evaluation_paired_reburn", TYPE_FLOAT, ("paired_reburn_train_expected_burned_area",)),

    # -- paired evaluation metadata (section 16) --------------------------
    CanonicalField("paired_evaluation_enabled", "paired_meta", TYPE_BOOL, ("paired_evaluation_enabled",)),
    CanonicalField("paired_reburn_instance_id", "paired_meta", TYPE_STR, ("paired_reburn_instance_id", "paired_reburn_instance_resolved")),
    CanonicalField("paired_reburn_instance_requested", "paired_meta", TYPE_STR, ("paired_reburn_instance_requested",)),
    CanonicalField("paired_reburn_instance_resolved", "paired_meta", TYPE_STR, ("paired_reburn_instance_resolved",)),
    CanonicalField("paired_reburn_resolution_status", "paired_meta", TYPE_STR, ("paired_reburn_resolution_status",)),
    CanonicalField("paired_selected_firebreak_count", "paired_meta", TYPE_INT, ("paired_selected_firebreak_count",)),
    CanonicalField("paired_selected_firebreaks_mapped", "paired_meta", TYPE_INT, ("paired_selected_firebreaks_mapped",)),
    CanonicalField("paired_selected_firebreaks_missing", "paired_meta", TYPE_INT, ("paired_selected_firebreaks_missing",)),
    CanonicalField("paired_selected_mapping_status", "paired_meta", TYPE_STR, ("paired_selected_mapping_status",)),

    # -- physical metric extras beyond mean_burned_cells_* (section 5) ----
    CanonicalField("burned_cells_var_in_sample", "physical_metrics", TYPE_FLOAT, ("train_empirical_var_burned_area",)),
    CanonicalField("burned_cells_cvar_in_sample", "physical_metrics", TYPE_FLOAT, ("train_empirical_cvar_burned_area",)),
    CanonicalField("burned_cells_worst10_in_sample", "physical_metrics", TYPE_FLOAT, ("train_worst_10pct_burned_area",)),
    CanonicalField("burned_cells_var_out_of_sample", "physical_metrics", TYPE_FLOAT, ("test_empirical_var_burned_area",)),
    CanonicalField("burned_cells_cvar_out_of_sample", "physical_metrics", TYPE_FLOAT, ("test_empirical_cvar_burned_area",)),
    CanonicalField("burned_cells_worst10_out_of_sample", "physical_metrics", TYPE_FLOAT, ("test_worst_10pct_burned_area",)),
    CanonicalField("percentage_landscape_value_burned_in_sample", "physical_metrics", TYPE_FLOAT, ("train_percentage_landscape_value_burned",)),
    CanonicalField("percentage_landscape_value_burned_out_of_sample", "physical_metrics", TYPE_FLOAT, ("test_percentage_landscape_value_burned",)),
    CanonicalField("percentage_high_value_weight_burned_in_sample", "physical_metrics", TYPE_FLOAT, ("train_percentage_high_value_weight_burned",)),
    CanonicalField("percentage_high_value_weight_burned_out_of_sample", "physical_metrics", TYPE_FLOAT, ("test_percentage_high_value_weight_burned",)),

    # -- objective cross-validation, surfaced not re-derived --------------
    # IMPORTANT (documented Phase 9A finding): objective_validation_passed
    # is only a meaningful correctness signal for FPP-exact-family rows. For
    # DPV-surrogate / heuristic rows it structurally reads False (the
    # surrogate is never expected to equal the true weighted loss) and MUST
    # NOT be treated as an error for those rows. See is_valid_dpv_result().
    CanonicalField("objective_validation_passed", "objective_validation", TYPE_BOOL, ("objective_validation_passed",)),
    CanonicalField("objective_validation_abs_difference", "objective_validation", TYPE_FLOAT, ("objective_validation_abs_difference",)),
    CanonicalField("objective_validation_rel_difference", "objective_validation", TYPE_FLOAT, ("objective_validation_rel_difference",)),
)

CANONICAL_FIELD_BY_NAME = {f.name: f for f in CANONICAL_FIELDS}
CANONICAL_FIELD_ORDER = tuple(f.name for f in CANONICAL_FIELDS)

# Fields added by the merge pipeline itself (never sourced from raw data).
# Always appended after CANONICAL_FIELD_ORDER, in this fixed order.
VALIDATION_META_FIELD_ORDER = (
    "validation_classification",
    "validation_reasons",
    "duplicate_category",
    "is_current_valid",
    "content_hash",
    "migration_status",
    "legacy_unmigrated_fields",
    "source_format",
    "source_file",
)

# Method-identity fields used to construct a legacy migration key (section
# 12) when a row has no usable run_id.
LEGACY_MIGRATION_KEY_FIELDS = (
    "canonical_landscape_id",
    "instance_id",
    "method",
    "method_family",
    "combinatorial_mode",
    "scenario_order",
    "risk_measure",
    "alpha",
    "cvar_beta",
    "mean_cvar_lambda",
    "train_scenario_count",
    "test_scenario_count",
    "in_sample_scenario_ids",
    "weight_profile",
    "weight_replicate",
    "weight_map_hash",
)

# ---------------------------------------------------------------------------
# Validation classification taxonomy (section 14)
# ---------------------------------------------------------------------------

VALID = "valid"
VALID_LEGACY_MIGRATED = "valid_legacy_migrated"
INVALID_SCHEMA = "invalid_schema"
INVALID_IDENTITY = "invalid_identity"
INVALID_WEIGHT_PROVENANCE = "invalid_weight_provenance"
INVALID_EVALUATION = "invalid_evaluation"
INVALID_PAIRED_EVALUATION = "invalid_paired_evaluation"
CONFLICTING_DUPLICATE = "conflicting_duplicate"
INCOMPLETE = "incomplete"

VALIDATION_CLASSIFICATIONS = (
    VALID,
    VALID_LEGACY_MIGRATED,
    INVALID_SCHEMA,
    INVALID_IDENTITY,
    INVALID_WEIGHT_PROVENANCE,
    INVALID_EVALUATION,
    INVALID_PAIRED_EVALUATION,
    CONFLICTING_DUPLICATE,
    INCOMPLETE,
)

# Duplicate categories (section 13).
DUP_UNIQUE = "unique"
DUP_EXACT_DUPLICATE = "exact_duplicate"
DUP_RETRY_ATTEMPT = "retry_attempt"
DUP_CONFLICTING_DUPLICATE = "conflicting_duplicate"
DUP_LEGACY_COLLISION = "legacy_collision"


# ---------------------------------------------------------------------------
# Strict numeric parsing (section 17 / test 27.11)
# ---------------------------------------------------------------------------
#
# fpp_new_instances_scaling_compact_schema.numeric_or_blank() is intentionally
# lenient: a malformed value passes through unchanged so a human can spot it
# in the compact CSV. That is wrong for a *validator* -- Phase 9A requires
# malformed numeric values to be rejected (row marked incomplete/invalid),
# never silently coerced to zero or blank. This is the strict counterpart
# used only by the validator, not by the normalizer's passthrough copy.

class NumericParseError(ValueError):
    pass


def parse_numeric_strict(value: object, *, field_name: str = "") -> float | None:
    """Return a finite float, or None for a genuinely missing value.

    Raises NumericParseError for anything else: non-numeric text, or a
    non-finite numeric value (inf/nan) that isn't an explicit missing marker.
    """
    if is_missing(value):
        return None
    text = str(value).strip()
    try:
        number = float(text)
    except ValueError as exc:
        raise NumericParseError(f"{field_name}: cannot parse {text!r} as a number") from exc
    if not math.isfinite(number):
        raise NumericParseError(f"{field_name}: non-finite value {text!r}")
    return number


def parse_int_strict(value: object, *, field_name: str = "") -> int | None:
    number = parse_numeric_strict(value, field_name=field_name)
    if number is None:
        return None
    if not float(number).is_integer():
        raise NumericParseError(f"{field_name}: {value!r} is not an integer count")
    return int(number)


def parse_list_int(value: object) -> list:
    if is_missing(value):
        return []
    text = str(value).strip().strip("[]")
    if not text:
        return []
    out = []
    for part in text.replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        out.append(int(float(part)))
    return out


# ---------------------------------------------------------------------------
# Method-family / derived-field helpers (shared, not duplicated per-caller)
# ---------------------------------------------------------------------------

def is_dpv_surrogate_row(raw_row: dict) -> bool:
    """True when this row's objective space is a DPV surrogate, not an exact
    weighted-FPP solve. Determined by the presence of a finite
    dpv_surrogate_objective -- the one explicit, unambiguous C++ field for
    this -- never by string-matching the method name alone."""
    try:
        return parse_numeric_strict(raw_row.get("dpv_surrogate_objective")) is not None
    except NumericParseError:
        return True


def derive_method_family(raw_row: dict) -> str:
    # The raw `method_family` column, as emitted today, is populated as a
    # verbatim copy of `method` (a confirmed, documented Phase 9A finding --
    # see docs/WEIGHTED_LANDSCAPES_PHASE9A.md) and is therefore not a useful
    # higher-level grouping. Only trust it when it actually differs from
    # `method`; otherwise derive a real family (FPP / DPV / ...) below.
    existing = raw_row.get("method_family")
    method = str(raw_row.get("method") or "").strip()
    if not is_missing(existing) and str(existing).strip() != method:
        return str(existing).strip()
    if is_dpv_surrogate_row(raw_row):
        return "DPV"
    if method.startswith("FPP"):
        return "FPP"
    return method or "unknown"


def derive_llbi_type(raw_row: dict) -> str:
    family = projected_family(raw_row)
    if family in {"coverage", "path"}:
        return f"projected_{family}"
    if any_bool(raw_row, "coverage_llbi_enabled"):
        return "coverage"
    if any_bool(raw_row, "path_llbi_enabled"):
        return "path"
    if any_bool(raw_row, "use_lifted_lower_bounds", "benders_use_lifted_lower_bounds"):
        return "standard"
    tokens = method_tokens(raw_row)
    if "LLBI" in tokens:
        return "standard"
    return "none"


def derive_restricted_candidate_mode(raw_row: dict) -> str:
    if not bool_value(raw_row.get("restricted_candidate_enabled")):
        return "none"
    if bool_value(raw_row.get("restricted_candidate_exact_mode")):
        return "exact"
    if bool_value(raw_row.get("restricted_candidate_heuristic_mode_enabled")):
        return "heuristic"
    return "enabled"


def derive_weight_normalization_mode(raw_row: dict) -> str:
    value = raw_row.get("weight_normalized")
    if is_missing(value):
        return ""
    return "normalized" if bool_value(value) else "raw"


# ---------------------------------------------------------------------------
# Comparison-group key (section 23) -- Phase 9B best-known-value prep only.
# Phase 9A defines and validates the key; it does NOT compute best-known
# values or performance profiles.
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class ComparisonGroupKey:
    instance_id: str
    train_scenario_ids: tuple
    risk_measure: str
    alpha: float
    cvar_beta: float
    mean_cvar_lambda: float
    weight_map_hash: str
    budget: int


def comparison_group_key(record: dict) -> ComparisonGroupKey:
    """Build the comparison-group key for a normalized canonical record.

    Two rows may only be compared (e.g. for a Phase 9B best-known-value
    table) when this key matches exactly. It deliberately excludes `method`
    (methods are what's being compared) and excludes solver vs. DPV
    objective space (comparisons must never mix them -- see
    objective_space_of()).
    """
    return ComparisonGroupKey(
        instance_id=str(record.get("instance_id", "")),
        train_scenario_ids=tuple(record.get("in_sample_scenario_ids") or ()),
        risk_measure=str(record.get("risk_measure", "")),
        alpha=float(record.get("alpha") or 0.0),
        cvar_beta=float(record.get("cvar_beta") or 0.0),
        mean_cvar_lambda=float(record.get("mean_cvar_lambda") or 0.0),
        weight_map_hash=str(record.get("weight_map_hash", "")),
        budget=int(record.get("budget") or 0),
    )


def objective_space_of(record: dict) -> str:
    """"fpp_exact" or "dpv_surrogate" -- comparisons must never mix these."""
    return "dpv_surrogate" if record.get("dpv_surrogate_objective") not in (None, "") else "fpp_exact"


def comparison_group_is_valid(records: list) -> bool:
    """True only if every record shares one comparison-group key AND one
    objective space. Never aggregate across map hashes, train splits, risk
    objectives, or DPV-vs-FPP objective spaces (Phase 9A section 23/31)."""
    if not records:
        return True
    keys = {comparison_group_key(r) for r in records}
    spaces = {objective_space_of(r) for r in records}
    return len(keys) == 1 and len(spaces) == 1


# ---------------------------------------------------------------------------
# Canonical status filters (section 24) -- reusable by Phase 9B.
# ---------------------------------------------------------------------------

_TERMINAL_FEASIBLE_STATUSES = {"optimal", "feasible"}


def is_valid_exact_result(record: dict) -> bool:
    """Valid exact result: FPP-exact method, terminal feasible/optimal
    solver status, evaluator validation passed."""
    if record.get("dpv_surrogate_objective") not in (None, ""):
        return False
    if str(record.get("solver_status", "")).lower() not in _TERMINAL_FEASIBLE_STATUSES:
        return False
    return bool(record.get("objective_validation_passed"))


def is_valid_heuristic_result(record: dict) -> bool:
    """Valid heuristic result: heuristic execution completed, a selected
    solution is present, and the exact evaluator ran (finite in-sample
    weighted FPP value) -- solver/evaluator agreement is NOT required since
    a heuristic never solves the exact weighted objective."""
    if record.get("execution_status") != "heuristic_completed":
        return False
    return record.get("weighted_fpp_expected_in_sample") not in (None, "")


def is_valid_dpv_result(record: dict) -> bool:
    """Valid DPV result: DPV surrogate solve completed, a selected solution
    is present, and the independent exact-FPP evaluator passed -- again,
    solver-vs-evaluator agreement is NOT the criterion (see the
    objective_validation_passed caveat on the field declaration above)."""
    if record.get("dpv_surrogate_objective") in (None, ""):
        return False
    status = str(record.get("solver_status", "")).lower()
    if status not in _TERMINAL_FEASIBLE_STATUSES and record.get("execution_status") != "heuristic_completed":
        return False
    return record.get("weighted_fpp_expected_in_sample") not in (None, "")


def is_fully_paired_valid_result(record: dict) -> bool:
    """Fully paired-valid result: base validity (exact, heuristic, or DPV)
    PLUS a successful, complete paired-reburn evaluation. Never satisfied
    merely because optimization succeeded (section 16)."""
    base_valid = (
        is_valid_exact_result(record)
        or is_valid_heuristic_result(record)
        or is_valid_dpv_result(record)
    )
    if not base_valid:
        return False
    if not bool_value(record.get("paired_evaluation_enabled")):
        return False
    if record.get("paired_reburn_evaluation_status") != "ok":
        return False
    if (record.get("paired_selected_firebreaks_missing") or 0) not in (0, "0"):
        return False
    return record.get("weighted_fpp_expected_paired_reburn") not in (None, "")
