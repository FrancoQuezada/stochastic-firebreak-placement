#!/usr/bin/env python3
"""Phase 9A merge integrity layer: validation classification, duplicate
policy, retry selection, and canonical output-artifact construction.

Operates purely on the normalized records produced by
weighted_result_normalize.normalize_row(). Never executes a solver, never
regenerates a weight map, never mutates a selected solution -- it only
classifies and groups data that already exists on disk.
"""

from __future__ import annotations

import hashlib
from collections import Counter, defaultdict

import weighted_result_schema as schema
from weighted_result_schema import (
    CANONICAL_FIELD_ORDER,
    CONFLICTING_DUPLICATE,
    DUP_CONFLICTING_DUPLICATE,
    DUP_EXACT_DUPLICATE,
    DUP_LEGACY_COLLISION,
    DUP_RETRY_ATTEMPT,
    DUP_UNIQUE,
    INCOMPLETE,
    INVALID_EVALUATION,
    INVALID_IDENTITY,
    INVALID_PAIRED_EVALUATION,
    INVALID_SCHEMA,
    INVALID_WEIGHT_PROVENANCE,
    VALID,
    VALID_LEGACY_MIGRATED,
)

_WEIGHT_HASH_FIELDS = (
    "manifest_weight_map_hash",
    "optimization_weight_map_hash",
    "in_sample_weight_map_hash",
    "out_of_sample_weight_map_hash",
)

_TERMINAL_STATUSES = {"optimal", "feasible", "timelimit_feasible", "notapplicable"}

_PAIRED_NOT_ATTEMPTED_STATUSES = ("", "n/a", None)


def _paired_evaluation_attempted(record: dict) -> bool:
    """Whether a paired-reburn evaluation was actually attempted for this
    row. NOT the same as `paired_evaluation_enabled` -- see the docstring on
    `weighted_result_schema.is_fully_paired_valid_result` for the confirmed,
    real discrepancy between that manifest-level flag and what the worker
    actually does. `paired_reburn_evaluation_status` is the real signal."""
    return record.get("paired_reburn_evaluation_status") not in _PAIRED_NOT_ATTEMPTED_STATUSES


def compute_content_hash(record: dict) -> str:
    """Deterministic hash over every canonical field's value. Two rows with
    an identical hash are byte-for-byte equivalent on everything this schema
    considers meaningful (execution timestamps are NOT canonical fields, so
    two writes of the same solve at different wall-clock times still hash
    equal -- this is intentional: it is what makes "exact duplicate"
    detection possible)."""
    parts = []
    for name in CANONICAL_FIELD_ORDER:
        value = record.get(name)
        if isinstance(value, list):
            value = tuple(value)
        parts.append(f"{name}={value!r}")
    digest = hashlib.sha256("|".join(parts).encode("utf-8")).hexdigest()
    return digest[:16]


def validate_record(
    record: dict,
    *,
    schema_version_filter: str | None = None,
    allow_legacy: bool = True,
    validate_paired: bool = True,
) -> tuple:
    """Return (classification, reasons). See VALIDATION_CLASSIFICATIONS in
    weighted_result_schema for the full taxonomy.

    schema_version_filter: reject any row whose detected version is not
    exactly this value (in addition to the always-enforced
    SUPPORTED_SCHEMA_VERSIONS check).
    allow_legacy: when False, any row whose migration_status is not
    "modern" is rejected as invalid_schema instead of migrated.
    validate_paired: when False, a broken paired-reburn evaluation is
    recorded as a warning reason but no longer downgrades the
    classification (an explicit escape hatch, off by default).
    """
    reasons: list = []

    if record["result_schema_version"] not in schema.SUPPORTED_SCHEMA_VERSIONS:
        return INVALID_SCHEMA, [f"unsupported result_schema_version={record['result_schema_version']!r}"]
    if schema_version_filter is not None and record["result_schema_version"] != schema_version_filter:
        return INVALID_SCHEMA, [
            f"result_schema_version={record['result_schema_version']!r} != required {schema_version_filter!r}"
        ]
    if not allow_legacy and record.get("migration_status") != "modern":
        return INVALID_SCHEMA, [
            f"legacy row (migration_status={record.get('migration_status')!r}) rejected: pass --allow-legacy to migrate it"
        ]

    numeric_errors = record.get("_numeric_errors") or []
    if numeric_errors:
        return INVALID_EVALUATION, list(numeric_errors)

    if not record.get("method") or not record.get("instance_id"):
        return INVALID_IDENTITY, ["missing required identity field(s): method and/or instance_id"]
    if not record.get("run_id") and record.get("migration_status") == "modern":
        reasons.append("modern row has no run_id; relying on derived legacy-style migration key")

    return_code = record.get("worker_return_code")
    solver_status = str(record.get("solver_status") or "").strip()
    if return_code not in (None, "", "0") and not solver_status:
        return INCOMPLETE, [f"worker_return_code={return_code!r} and no solver_status recorded"]
    if not solver_status and record.get("execution_status") != "heuristic_completed":
        return INCOMPLETE, ["no solver_status and execution_status is not heuristic_completed"]

    if record.get("weight_profile"):
        hash_value = record.get("weight_map_hash")
        for field_name in _WEIGHT_HASH_FIELDS:
            other = record.get(field_name)
            if other and hash_value and other != hash_value:
                return INVALID_WEIGHT_PROVENANCE, [
                    f"{field_name}={other!r} does not match weight_map_hash={hash_value!r}"
                ]
        paired_hash = record.get("paired_reburn_weight_map_hash")
        if _paired_evaluation_attempted(record) and paired_hash and hash_value and paired_hash != hash_value:
            return INVALID_WEIGHT_PROVENANCE, [
                f"paired_reburn_weight_map_hash={paired_hash!r} does not match weight_map_hash={hash_value!r}"
            ]

    # objective_validation_passed is only a meaningful correctness signal
    # for FPP-exact rows (see weighted_result_schema.py docstring on the
    # objective_validation_passed field): a DPV surrogate or heuristic row
    # is EXPECTED to fail solver-vs-evaluator agreement by construction.
    is_exact_fpp = record.get("dpv_surrogate_objective") is None
    if is_exact_fpp and record.get("objective_validation_passed") is False:
        return INVALID_EVALUATION, [
            f"objective_validation_passed=False for an FPP-exact row "
            f"(abs_diff={record.get('objective_validation_abs_difference')!r})"
        ]

    if _paired_evaluation_attempted(record):
        paired_reasons = []
        if record.get("paired_reburn_evaluation_status") != "ok":
            paired_reasons.append(
                f"paired_reburn_evaluation_status={record.get('paired_reburn_evaluation_status')!r} != 'ok'"
            )
        missing = record.get("paired_selected_firebreaks_missing")
        if missing not in (0, None, ""):
            paired_reasons.append(f"paired_selected_firebreaks_missing={missing!r} != 0")
        if not paired_reasons and record.get("weighted_fpp_expected_paired_reburn") is None:
            paired_reasons.append(
                "paired evaluation status is 'ok' but no weighted_fpp_expected_paired_reburn value was recovered"
            )
        if paired_reasons:
            if validate_paired:
                return INVALID_PAIRED_EVALUATION, paired_reasons
            reasons.extend("(paired validation disabled) " + r for r in paired_reasons)

    if record.get("migration_status") == "modern":
        return VALID, reasons
    return VALID_LEGACY_MIGRATED, reasons + [f"migrated from {record['result_schema_version']}"]


def _primary_key(record: dict):
    return record.get("run_id") or None


def group_logical_runs(records: list) -> dict:
    """Group by run_id when present, else by the derived legacy migration
    key (section 12). Returns dict key -> list[record]."""
    groups: dict = defaultdict(list)
    for record in records:
        key = _primary_key(record) or record["logical_run_key"]
        groups[key].append(record)
    return groups


_COLLISION_IDENTITY_FIELDS = ("method", "instance_id", "alpha", "train_scenario_count", "weight_map_hash")


def _looks_like_same_config(a: dict, b: dict) -> bool:
    return all(a.get(f) == b.get(f) for f in _COLLISION_IDENTITY_FIELDS)


def classify_duplicates(records: list) -> dict:
    """Annotate every record's `duplicate_category` and `is_current_valid`
    in place. Returns diagnostics counters for this logical group."""
    counters = Counter()
    for record in records:
        record["content_hash"] = compute_content_hash(record)

    groups = group_logical_runs(records)
    for key, group in groups.items():
        run_id_based = _primary_key(group[0]) is not None

        # Legacy collision: same run_id (or same derived key) but genuinely
        # different logical configurations sharing it (section 13).
        if run_id_based and len(group) > 1:
            distinct_configs = []
            for record in group:
                if not any(_looks_like_same_config(record, existing[0]) for existing in distinct_configs):
                    distinct_configs.append([record])
                else:
                    for existing in distinct_configs:
                        if _looks_like_same_config(record, existing[0]):
                            existing.append(record)
                            break
            if len(distinct_configs) > 1:
                for idx, subgroup in enumerate(distinct_configs):
                    for record in subgroup:
                        record["duplicate_category"] = DUP_LEGACY_COLLISION
                        record["validation_classification"] = CONFLICTING_DUPLICATE
                        record["logical_run_key"] = f"{key}::collision_{idx}"
                        record["is_current_valid"] = False
                    counters["legacy_collision_rows"] += len(subgroup)
                continue

        # Within one real logical run: split by (attempt) after deduping
        # byte-identical content.
        seen_hashes: dict = {}
        for record in group:
            h = record["content_hash"]
            seen_hashes.setdefault(h, []).append(record)

        if len(seen_hashes) == 1 and len(group) > 1:
            # Every row in this logical run is byte-identical.
            ordered = group
            for record in ordered[1:]:
                record["duplicate_category"] = DUP_EXACT_DUPLICATE
                record["is_current_valid"] = False
            ordered[0]["duplicate_category"] = DUP_UNIQUE
            counters["exact_duplicates"] += len(ordered) - 1
            candidates = [ordered[0]]
        else:
            candidates = []
            by_attempt: dict = defaultdict(list)
            for record in group:
                by_attempt[record.get("attempt")].append(record)
            for attempt, attempt_group in by_attempt.items():
                distinct = {r["content_hash"]: r for r in attempt_group}
                if len(distinct) > 1:
                    for record in attempt_group:
                        record["duplicate_category"] = DUP_CONFLICTING_DUPLICATE
                        record["validation_classification"] = CONFLICTING_DUPLICATE
                        record["is_current_valid"] = False
                    counters["conflicting_duplicates"] += len(attempt_group)
                else:
                    for record in attempt_group[1:]:
                        record["duplicate_category"] = DUP_EXACT_DUPLICATE
                        record["is_current_valid"] = False
                        counters["exact_duplicates"] += 1
                    attempt_group[0]["duplicate_category"] = (
                        DUP_RETRY_ATTEMPT if len(by_attempt) > 1 else DUP_UNIQUE
                    )
                    candidates.append(attempt_group[0])
            if len(by_attempt) > 1:
                counters["retry_groups"] += 1
                counters["retry_attempts"] += sum(len(v) for v in by_attempt.values())

        # Retry selection: among candidates classified valid/valid_legacy_migrated,
        # pick the latest attempt as the current valid row for this logical run.
        eligible = [
            r for r in candidates
            if r.get("validation_classification") in (VALID, VALID_LEGACY_MIGRATED)
        ]
        for record in candidates:
            record["is_current_valid"] = False
        if eligible:

            def sort_key(r):
                return (
                    r.get("attempt") if r.get("attempt") is not None else -1,
                    r.get("worker_finished_at_epoch") or 0.0,
                )

            eligible.sort(key=sort_key)
            eligible[-1]["is_current_valid"] = True

    return counters


def build_merge_outputs(
    records: list,
    *,
    schema_version_filter: str | None = None,
    allow_legacy: bool = True,
    validate_paired: bool = True,
) -> dict:
    """Validate every record, classify duplicates, and split into the
    canonical output artifacts (section 19). Returns a dict with
    all_attempts, current_valid, invalid, and diagnostics."""
    for record in records:
        classification, reasons = validate_record(
            record,
            schema_version_filter=schema_version_filter,
            allow_legacy=allow_legacy,
            validate_paired=validate_paired,
        )
        record["validation_classification"] = classification
        record["validation_reasons"] = "; ".join(reasons)
        record.setdefault("duplicate_category", DUP_UNIQUE)
        record.setdefault("is_current_valid", False)

    dup_counters = classify_duplicates(records)

    all_attempts = list(records)
    current_valid = [r for r in records if r.get("is_current_valid")]
    invalid = [
        r for r in records
        if r["validation_classification"] not in (VALID, VALID_LEGACY_MIGRATED)
    ]

    logical_runs = group_logical_runs(records)
    classification_counts = Counter(r["validation_classification"] for r in records)
    weight_hash_mismatches = sum(
        1 for r in records if r["validation_classification"] == INVALID_WEIGHT_PROVENANCE
    )
    paired_validation_failures = sum(
        1 for r in records if r["validation_classification"] == INVALID_PAIRED_EVALUATION
    )
    schema_versions = Counter(r["result_schema_version"] for r in records)
    method_counts = Counter(r.get("method", "") for r in records)
    weight_profile_counts = Counter(r.get("weight_profile", "") for r in records)
    failed_runs = sum(1 for r in records if r["validation_classification"] == INCOMPLETE)

    diagnostics = {
        "input_rows": len(records),
        "parsed_rows": len(records),
        "valid_rows": classification_counts.get(VALID, 0),
        "legacy_migrated_rows": classification_counts.get(VALID_LEGACY_MIGRATED, 0),
        "invalid_rows": len(invalid),
        "incomplete_rows": classification_counts.get(INCOMPLETE, 0),
        "exact_duplicates": dup_counters.get("exact_duplicates", 0),
        "retry_attempts": dup_counters.get("retry_attempts", 0),
        "conflicting_duplicates": dup_counters.get("conflicting_duplicates", 0),
        "legacy_collision_rows": dup_counters.get("legacy_collision_rows", 0),
        "logical_runs": len(logical_runs),
        "current_valid_runs": len(current_valid),
        "failed_runs": failed_runs,
        "weight_hash_mismatches": weight_hash_mismatches,
        "paired_validation_failures": paired_validation_failures,
        "schema_versions": dict(schema_versions),
        "method_counts": dict(method_counts),
        "weight_profile_counts": dict(weight_profile_counts),
        "classification_counts": dict(classification_counts),
    }

    return {
        "all_attempts": all_attempts,
        "current_valid": current_valid,
        "invalid": invalid,
        "diagnostics": diagnostics,
    }
