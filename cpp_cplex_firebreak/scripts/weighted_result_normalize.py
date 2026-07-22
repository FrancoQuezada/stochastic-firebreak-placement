#!/usr/bin/env python3
"""Phase 9A normalization layer: raw worker/legacy CSV row (+ optional
per-task JSON) -> one internal canonical record.

This is the single place that reads the many raw shapes the pipeline
produces today (modern Phase 8B worker CSV rows, their sibling per-task
solver JSON and paired-reburn-evaluation JSON, and pre-8A/8B legacy CSV
rows) and turns each one into a dict keyed by the canonical field names
declared in weighted_result_schema.py.

Per Phase 9A section 18, JSON is preferred as the lossless source when both
a CSV row and a JSON payload exist for the same run: two canonical fields
(weighted_fpp_expected_paired_reburn, weighted_fpp_cvar_paired_reburn) have
NO source column in the worker CSV today at all -- they can only be
recovered from the paired-reburn-evaluation JSON (a real, documented gap;
see docs/WEIGHTED_LANDSCAPES_PHASE9A.md).
"""

from __future__ import annotations

import json
from pathlib import Path

from fpp_new_instances_scaling_compact_schema import bool_value, first_value, is_missing
import weighted_result_schema as schema
from weighted_result_schema import (
    CANONICAL_FIELDS,
    NumericParseError,
    parse_int_strict,
    parse_list_int,
    parse_numeric_strict,
)


def _read_json(path_str: str) -> dict | None:
    if not path_str:
        return None
    path = Path(path_str)
    if not path.exists():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def paired_reburn_json_path(raw_row: dict) -> str:
    """The worker writes `<task_id>_paired_reburn_eval.json` next to the
    solver's own `<task_id>.json` (same directory as `output_json`)."""
    output_json = raw_row.get("output_json") or ""
    if not output_json:
        return ""
    path = Path(output_json)
    return str(path.with_name(path.stem + "_paired_reburn_eval.json"))


def _resolve_raw(raw_row: dict, aliases: tuple) -> str:
    return first_value(raw_row, *aliases) if aliases else ""


def _mean_cvar(expected: float | None, cvar: float | None, risk_measure: str, lam: float | None) -> float | None:
    if "mean" not in (risk_measure or "").lower() or "cvar" not in (risk_measure or "").lower():
        return None
    if expected is None or cvar is None or lam is None:
        return None
    return lam * expected + (1.0 - lam) * cvar


def derive_solver_objective_fields(raw_row: dict) -> dict:
    """Section 4.1: the exact weighted-FPP solver objective must never be
    populated from a DPV surrogate solve. `objective_in_sample`/`best_bound`/
    `mip_gap` hold whatever the dispatched solver actually optimized -- for
    FPP-exact methods that IS the weighted FPP objective; for DPV-surrogate
    methods it is the surrogate's own value (already captured separately in
    dpv_surrogate_objective). We only alias it into solver_objective when
    this is NOT a DPV-surrogate row."""
    if schema.is_dpv_surrogate_row(raw_row):
        return {"solver_objective": "", "solver_best_bound": "", "solver_gap": ""}
    return {
        "solver_objective": first_value(raw_row, "objective_in_sample"),
        "solver_best_bound": first_value(raw_row, "best_bound"),
        "solver_gap": first_value(raw_row, "mip_gap", "solver_mip_gap"),
    }


def normalize_row(
    raw_row: dict,
    *,
    source_file: str = "",
    source_format: str = "worker_csv",
    prefer_json: bool = True,
) -> dict:
    """Turn one raw row into a canonical record (dict of canonical field
    name -> parsed value). Numeric fields that fail strict parsing are left
    as None and reported in `_numeric_errors` (never silently coerced to
    zero -- section 17 / test 27.11)."""
    record: dict = {}
    numeric_errors: list = []

    explicit_version = raw_row.get("result_schema_version")
    if not is_missing(explicit_version):
        version = str(explicit_version).strip()
        migration_status = "modern" if version == schema.RESULT_SCHEMA_VERSION else "legacy_declared"
    else:
        version = schema.detect_legacy_schema_version(raw_row)
        migration_status = "modern" if version == schema.RESULT_SCHEMA_VERSION else "legacy_migrated"
    record["result_schema_version"] = version

    solver_json = _read_json(raw_row.get("output_json", "")) if prefer_json else None
    paired_json = _read_json(paired_reburn_json_path(raw_row)) if prefer_json else None

    for f in CANONICAL_FIELDS:
        if f.name == "result_schema_version":
            continue
        raw_value = ""
        if f.json_only:
            payload = paired_json if f.json_source == "paired_reburn" else solver_json
            if payload is not None and not is_missing(payload.get(f.json_key)):
                raw_value = str(payload.get(f.json_key))
            # No CSV fallback exists for these fields today; leave blank
            # (reported by the validator, never fabricated).
        elif f.raw_aliases:
            raw_value = _resolve_raw(raw_row, f.raw_aliases)
            if not raw_value and solver_json is not None and prefer_json:
                for alias in f.raw_aliases:
                    if not is_missing(solver_json.get(alias)):
                        raw_value = str(solver_json.get(alias))
                        break

        if f.dtype == schema.TYPE_FLOAT:
            try:
                record[f.name] = parse_numeric_strict(raw_value, field_name=f.name)
            except NumericParseError as exc:
                numeric_errors.append(str(exc))
                record[f.name] = None
        elif f.dtype == schema.TYPE_INT:
            try:
                record[f.name] = parse_int_strict(raw_value, field_name=f.name)
            except NumericParseError as exc:
                numeric_errors.append(str(exc))
                record[f.name] = None
        elif f.dtype == schema.TYPE_BOOL:
            record[f.name] = None if is_missing(raw_value) else bool_value(raw_value)
        elif f.dtype == schema.TYPE_LIST_INT:
            record[f.name] = parse_list_int(raw_value)
        else:
            record[f.name] = "" if is_missing(raw_value) else str(raw_value).strip()

    # -- derived fields (no direct raw alias) ---------------------------
    # derive_method_family() itself decides whether the raw passthrough
    # value is trustworthy (see its docstring): always call it rather than
    # only falling back to it when the raw column is empty.
    record["method_family"] = schema.derive_method_family(raw_row)
    record["llbi_type"] = schema.derive_llbi_type(raw_row)
    record["restricted_candidate_mode"] = schema.derive_restricted_candidate_mode(raw_row)
    record["weight_normalization_mode"] = schema.derive_weight_normalization_mode(raw_row)

    solver_derived = derive_solver_objective_fields(raw_row)
    for key, value in solver_derived.items():
        if value == "":
            record[key] = None
        else:
            try:
                record[key] = parse_numeric_strict(value, field_name=key)
            except NumericParseError as exc:
                numeric_errors.append(str(exc))
                record[key] = None

    record["in_sample_evaluation_status"] = "ok" if record.get("weighted_fpp_expected_in_sample") is not None else "missing"
    record["out_of_sample_evaluation_status"] = "ok" if record.get("weighted_fpp_expected_out_of_sample") is not None else "missing"
    if not record.get("paired_reburn_evaluation_status"):
        record["paired_reburn_evaluation_status"] = ""

    record["weighted_fpp_mean_cvar_in_sample"] = _mean_cvar(
        record.get("weighted_fpp_expected_in_sample"),
        record.get("weighted_fpp_cvar_in_sample"),
        record.get("risk_measure"),
        record.get("mean_cvar_lambda"),
    )
    record["weighted_fpp_mean_cvar_out_of_sample"] = _mean_cvar(
        record.get("weighted_fpp_expected_out_of_sample"),
        record.get("weighted_fpp_cvar_out_of_sample"),
        record.get("risk_measure"),
        record.get("mean_cvar_lambda"),
    )
    record["weighted_fpp_mean_cvar_paired_reburn"] = _mean_cvar(
        record.get("weighted_fpp_expected_paired_reburn"),
        record.get("weighted_fpp_cvar_paired_reburn"),
        record.get("risk_measure"),
        record.get("mean_cvar_lambda"),
    )

    # -- logical run key (section 12) -----------------------------------
    if record.get("run_id"):
        record["logical_run_key"] = record["run_id"]
    else:
        key_parts = []
        for field_name in schema.LEGACY_MIGRATION_KEY_FIELDS:
            value = record.get(field_name)
            key_parts.append(f"{field_name}={value!r}")
        record["logical_run_key"] = "legacy::" + "|".join(key_parts)

    # -- bookkeeping (not part of CANONICAL_FIELD_ORDER's raw-sourced set)
    record["migration_status"] = migration_status
    record["legacy_unmigrated_fields"] = _legacy_unmigrated_fields(record, migration_status)
    record["source_format"] = source_format
    record["source_file"] = source_file
    record["_numeric_errors"] = numeric_errors
    record["_raw_row"] = raw_row
    return record


def _legacy_unmigrated_fields(record: dict, migration_status: str) -> list:
    """Section 25: report which canonical fields a legacy row could not
    populate, rather than silently leaving them blank with no trace."""
    if migration_status == "modern":
        return []
    always_expected_for_legacy = (
        "canonical_landscape_id",
        "weight_replicate",
        "weight_generation_seed",
        "weight_generator_version",
        "weight_source_universe_hash",
        "weight_mapping_method",
        "paired_reburn_evaluation_status",
    )
    missing = [name for name in always_expected_for_legacy if not record.get(name)]
    return missing
