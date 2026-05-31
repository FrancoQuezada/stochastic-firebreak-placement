#!/usr/bin/env python3
"""Phase 1R broadened CVaR-tail score diagnostics for restricted FPP-CVaR."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


PROJECT_DIR = Path(__file__).resolve().parents[2]
REPO_ROOT = PROJECT_DIR.parent
OUT_DIR = PROJECT_DIR / "results" / "new_method_phase1r_broaden_cvar_tail_score_diagnostics"
LANDSCAPE = "Sub20"
FOREST_PATH = REPO_ROOT / "sample_test" / "data" / "CanadianFBP" / LANDSCAPE
RESULTS_PATH = REPO_ROOT / "sample_test" / LANDSCAPE
TRAIN_COUNT = 100
TEST_COUNT = 100
SEED_BASE = 41000
CVAR_BETA = 0.9
MIP_GAP = 0.001
THREADS = 1

RUN_SUMMARY_CSV = OUT_DIR / "phase1r_run_summary.csv"
ROUND_SUMMARY_CSV = OUT_DIR / "phase1r_round_summary.csv"
CANDIDATE_EVENTS_CSV = OUT_DIR / "phase1r_candidate_events.csv"
WARNING_SUMMARY_CSV = OUT_DIR / "phase1r_warning_summary.csv"
ALIGNMENT_BY_CONFIG_CSV = OUT_DIR / "phase1r_alignment_by_config.csv"
ALIGNMENT_BY_ALPHA_CSV = OUT_DIR / "phase1r_alignment_by_alpha.csv"
DECISION_SUMMARY_CSV = OUT_DIR / "phase1r_decision_summary.csv"
COMMAND_MANIFEST_CSV = OUT_DIR / "phase1r_command_manifest.csv"


@dataclass(frozen=True)
class DiagnosticConfig:
    name: str
    initial_candidate_size: int
    activation_batch_size: int
    min_active_size: int
    max_active_size: int
    max_candidate_rounds: int


CONFIGS = [
    DiagnosticConfig("compact_pool", 50, 20, 50, 70, 2),
    DiagnosticConfig("compact_pool_deeper", 50, 20, 50, 70, 3),
    DiagnosticConfig("wider_pool", 100, 20, 100, 120, 2),
]
ALPHAS = [0.01, 0.02]
CASE_IDS = [0, 1, 2]

RUN_COLUMNS = [
    "run_id",
    "config_name",
    "alpha",
    "case_id",
    "seed",
    "train_count",
    "test_count",
    "status",
    "objective",
    "runtime_seconds",
    "risk_measure",
    "cvar_beta",
    "expected_loss_component",
    "cvar_loss_component",
    "final_active_candidate_count",
    "final_active_candidate_fraction",
    "candidate_rounds",
    "deactivation_rounds",
    "cut_pool_size",
    "global_optimality_certified",
    "initial_candidate_size",
    "candidate_activation_batch_size",
    "candidate_min_active_size",
    "candidate_max_active_size",
    "max_candidate_rounds",
    "tail_diagnostic_rounds",
    "total_warnings",
    "activated_poor_tail_rank_warnings",
    "deactivated_tail_top_k_warnings",
    "low_generic_tail_overlap_warnings",
    "nonpositive_correlation_warnings",
]

ROUND_COLUMNS = [
    "run_id",
    "config_name",
    "alpha",
    "case_id",
    "round_index",
    "active_count_before_round",
    "active_count_after_activation",
    "active_count_after_deactivation",
    "tail_scenario_count",
    "top_k",
    "top_k_overlap_generic_tail",
    "top_k_overlap_fraction",
    "activated_tail_top_k_overlap",
    "activated_tail_top_k_fraction",
    "deactivated_tail_bottom_k_overlap",
    "deactivated_tail_top_k_warning_count",
    "selected_tail_top_k_overlap",
    "spearman_generic_tail",
    "pearson_generic_tail",
    "activated_count",
    "deactivated_count",
    "selected_count",
    "protected_selected_count",
]

CANDIDATE_EVENT_COLUMNS = [
    "run_id",
    "config_name",
    "alpha",
    "case_id",
    "round_index",
    "candidate_id",
    "event_type",
    "generic_score",
    "tail_empirical_score",
    "tail_excess_score",
    "recent_tail_score",
    "rank_generic",
    "rank_tail_empirical",
    "rank_tail_excess",
    "rank_recent_tail",
    "was_active_before",
    "was_active_after",
    "was_selected",
    "was_protected",
    "was_activated",
    "was_deactivated",
    "warning",
]

WARNING_COLUMNS = [
    "config_name",
    "alpha",
    "case_id",
    "warning_type",
    "count",
    "run_id",
    "round_indices",
    "candidate_ids",
]

ALIGNMENT_COLUMNS = [
    "group_name",
    "run_count",
    "round_count",
    "mean_top_k_overlap_fraction",
    "min_top_k_overlap_fraction",
    "mean_spearman",
    "min_spearman",
    "mean_pearson",
    "min_pearson",
    "total_activated_poor_tail_rank_warnings",
    "total_deactivated_tail_top_k_warnings",
    "fraction_rounds_low_overlap",
    "fraction_rounds_nonpositive_correlation",
    "mean_objective",
    "mean_runtime",
    "mean_final_active_fraction",
    "classification",
]

DECISION_COLUMNS = [
    "group_type",
    "group_name",
    *ALIGNMENT_COLUMNS[1:],
    "recommended_decision",
]

MANIFEST_COLUMNS = [
    "run_id",
    "config_name",
    "alpha",
    "case_id",
    "seed",
    "json_path",
    "log_path",
    "status",
    "return_code",
    "command",
]


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


def write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({column: format_cell(row.get(column, "")) for column in columns})


def parse_float(value: Any) -> float | None:
    if value in ("", None):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def mean(values: Iterable[float | None]) -> float | None:
    finite = [value for value in values if value is not None and math.isfinite(value)]
    if not finite:
        return None
    return sum(finite) / len(finite)


def min_finite(values: Iterable[float | None]) -> float | None:
    finite = [value for value in values if value is not None and math.isfinite(value)]
    if not finite:
        return None
    return min(finite)


def alpha_token(alpha: float) -> str:
    return f"{alpha:.6g}".replace(".", "p")


def run_id(config: DiagnosticConfig, alpha: float, case_id: int) -> str:
    return f"phase1r_{config.name}_alpha_{alpha_token(alpha)}_case_{case_id}"


def available_scenario_ids() -> list[int]:
    messages = RESULTS_PATH / "Messages"
    ids: list[int] = []
    for path in messages.glob("MessagesFile*.csv"):
        digits = "".join(ch for ch in path.stem if ch.isdigit())
        if digits:
            ids.append(int(digits))
    ids = sorted(set(ids))
    if len(ids) < TRAIN_COUNT + TEST_COUNT:
        raise RuntimeError("Not enough Sub20 scenarios for the Phase 1R 100/100 split.")
    return ids


def split_for_case(case_id: int) -> tuple[list[int], list[int]]:
    ids = available_scenario_ids()
    rng = random.Random(SEED_BASE + case_id)
    shuffled = ids[:]
    rng.shuffle(shuffled)
    train = sorted(shuffled[:TRAIN_COUNT])
    test = sorted(shuffled[TRAIN_COUNT:TRAIN_COUNT + TEST_COUNT])
    return train, test


def save_split(alpha: float, case_id: int, train: list[int], test: list[int]) -> Path:
    path = OUT_DIR / "splits" / f"phase1r_alpha_{alpha_token(alpha)}_case_{case_id}_split.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "landscape": LANDSCAPE,
                "alpha": alpha,
                "case_id": case_id,
                "seed": SEED_BASE + case_id,
                "train_ids": train,
                "test_ids": test,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    return path


def config_by_name(name: str) -> DiagnosticConfig:
    for config in CONFIGS:
        if config.name == name:
            return config
    raise ValueError(f"Unknown config: {name}")


def selected_alphas(value: str | None) -> list[float]:
    if not value:
        return ALPHAS
    requested = [float(item) for item in value.split(",") if item]
    return requested


def selected_case_ids(value: str | None) -> list[int]:
    if not value:
        return CASE_IDS
    return [int(item) for item in value.split(",") if item]


def selected_configs(value: str | None) -> list[DiagnosticConfig]:
    if not value:
        return CONFIGS
    return [config_by_name(item) for item in value.split(",") if item]


def build_command(
    config: DiagnosticConfig,
    alpha: float,
    case_id: int,
    train: list[int],
    test: list[int],
    time_limit: float,
) -> tuple[str, Path, Path, list[str]]:
    rid = run_id(config, alpha, case_id)
    json_path = OUT_DIR / "json" / f"{rid}.json"
    log_path = OUT_DIR / "logs" / f"{rid}.log"
    solution_json = OUT_DIR / "solutions" / f"{rid}_solution.json"
    solution_csv = OUT_DIR / "solutions" / f"{rid}_solution.csv"
    raw_csv = OUT_DIR / "phase1r_raw_runner_results.csv"
    cmd = [
        str(PROJECT_DIR / "build_gpp" / "firebreak_cpp"),
        "run-fpp-restricted-branch-benders-oos",
        "--landscape", LANDSCAPE,
        "--forest-path", str(FOREST_PATH),
        "--results-path", str(RESULTS_PATH),
        "--train-ids", ",".join(str(v) for v in train),
        "--test-ids", ",".join(str(v) for v in test),
        "--alpha", f"{alpha:.6g}",
        "--risk-measure", "cvar",
        "--cvar-beta", f"{CVAR_BETA:.6g}",
        "--time-limit", f"{time_limit:.6g}",
        "--mip-gap", f"{MIP_GAP:.6g}",
        "--threads", str(THREADS),
        "--run-id", rid,
        "--output-json", str(json_path),
        "--output-csv", str(raw_csv),
        "--solution-json", str(solution_json),
        "--solution-csv", str(solution_csv),
        "--initial-candidate-policy", "burn-frequency",
        "--initial-candidate-size", str(config.initial_candidate_size),
        "--candidate-activation-policy", "benders-coefficients",
        "--candidate-activation-batch-size", str(config.activation_batch_size),
        "--candidate-maintenance-policy", "benders-coefficients",
        "--candidate-deactivation-batch-size", str(config.activation_batch_size),
        "--candidate-min-active-size", str(config.min_active_size),
        "--candidate-max-active-size", str(config.max_active_size),
        "--candidate-deactivation-min-age", "1",
        "--candidate-reactivation-cooldown-rounds", "1",
        "--max-candidate-rounds", str(config.max_candidate_rounds),
        "--restricted-heuristic-mode",
        "--stop-after-candidate-rounds", str(config.max_candidate_rounds),
        "--export-tail-score-diagnostics",
    ]
    return rid, json_path, log_path, cmd


def restricted_block(data: dict[str, Any]) -> dict[str, Any]:
    block = data.get("restricted_candidate") or {}
    return block if isinstance(block, dict) else {}


def diagnostics(data: dict[str, Any]) -> list[dict[str, Any]]:
    values = restricted_block(data).get("tail_score_diagnostics") or []
    return [value for value in values if isinstance(value, dict)]


def round_log_by_index(data: dict[str, Any]) -> dict[int, dict[str, Any]]:
    out: dict[int, dict[str, Any]] = {}
    for round_data in restricted_block(data).get("round_log") or []:
        if not isinstance(round_data, dict):
            continue
        try:
            idx = int(round_data.get("round_index"))
        except (TypeError, ValueError):
            continue
        out[idx] = round_data
    return out


def warning_records(
    data: dict[str, Any],
    config: DiagnosticConfig,
    alpha: float,
    case_id: int,
) -> list[dict[str, Any]]:
    rid = str(data.get("run_id", run_id(config, alpha, case_id)))
    records: list[dict[str, Any]] = []
    diag_values = diagnostics(data)
    if not diag_values:
        return [{
            "run_id": rid,
            "config_name": config.name,
            "alpha": alpha,
            "case_id": case_id,
            "round_index": "",
            "candidate_id": "",
            "warning_type": "missing_tail_diagnostics",
        }]

    for diag in diag_values:
        idx = diag.get("round_index", "")
        top_k = len(diag.get("top_generic_candidates") or [])
        overlap = parse_float(diag.get("top_k_overlap_generic_tail"))
        if int(diag.get("tail_scenario_count") or 0) == 0:
            records.append({
                "run_id": rid,
                "config_name": config.name,
                "alpha": alpha,
                "case_id": case_id,
                "round_index": idx,
                "candidate_id": "",
                "warning_type": "missing_tail_scenarios",
            })
        if top_k > 0 and overlap is not None and overlap / top_k < 0.25:
            records.append({
                "run_id": rid,
                "config_name": config.name,
                "alpha": alpha,
                "case_id": case_id,
                "round_index": idx,
                "candidate_id": "",
                "warning_type": "low_generic_tail_overlap",
            })
        spearman = parse_float(diag.get("spearman_generic_tail"))
        pearson = parse_float(diag.get("pearson_generic_tail"))
        if (spearman is not None and spearman <= 0.0) or (pearson is not None and pearson <= 0.0):
            records.append({
                "run_id": rid,
                "config_name": config.name,
                "alpha": alpha,
                "case_id": case_id,
                "round_index": idx,
                "candidate_id": "",
                "warning_type": "nonpositive_correlation",
            })
        if int(diag.get("deactivated_tail_top_k_warning_count") or 0) > 0:
            records.append({
                "run_id": rid,
                "config_name": config.name,
                "alpha": alpha,
                "case_id": case_id,
                "round_index": idx,
                "candidate_id": "",
                "warning_type": "deactivated_tail_top_k",
            })
        for note in diag.get("notes") or []:
            note_text = str(note).lower()
            if "tail scenarios" in note_text and "cuts" in note_text:
                records.append({
                    "run_id": rid,
                    "config_name": config.name,
                    "alpha": alpha,
                    "case_id": case_id,
                    "round_index": idx,
                    "candidate_id": "",
                    "warning_type": "tail_scenarios_without_cuts",
                })
        for event in diag.get("candidate_events") or []:
            if not isinstance(event, dict):
                continue
            for warning in str(event.get("warning") or "").split(";"):
                if warning:
                    records.append({
                        "run_id": rid,
                        "config_name": config.name,
                        "alpha": alpha,
                        "case_id": case_id,
                        "round_index": idx,
                        "candidate_id": event.get("candidate_id", ""),
                        "warning_type": warning,
                    })
    return records


def warning_counts(records: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for record in records:
        key = str(record.get("warning_type", ""))
        counts[key] = counts.get(key, 0) + 1
    return counts


def run_summary_row(
    data: dict[str, Any],
    config: DiagnosticConfig,
    alpha: float,
    case_id: int,
    warning_records_for_run: list[dict[str, Any]],
) -> dict[str, Any]:
    restricted = restricted_block(data)
    counts = warning_counts(warning_records_for_run)
    return {
        "run_id": data.get("run_id", run_id(config, alpha, case_id)),
        "config_name": config.name,
        "alpha": alpha,
        "case_id": case_id,
        "seed": SEED_BASE + case_id,
        "train_count": data.get("train_scenario_count", TRAIN_COUNT),
        "test_count": data.get("test_scenario_count", TEST_COUNT),
        "status": data.get("solver_status", ""),
        "objective": data.get("objective_in_sample", ""),
        "runtime_seconds": data.get("runtime_seconds", ""),
        "risk_measure": data.get("risk_measure", ""),
        "cvar_beta": data.get("cvar_beta", ""),
        "expected_loss_component": data.get("expected_loss_component", ""),
        "cvar_loss_component": data.get("cvar_loss_component", ""),
        "final_active_candidate_count": restricted.get("final_active_count", ""),
        "final_active_candidate_fraction": restricted.get("final_active_fraction", ""),
        "candidate_rounds": restricted.get("candidate_rounds", ""),
        "deactivation_rounds": restricted.get("deactivation_rounds", ""),
        "cut_pool_size": restricted.get("cut_pool_size", ""),
        "global_optimality_certified": restricted.get("global_optimality_certified", ""),
        "initial_candidate_size": config.initial_candidate_size,
        "candidate_activation_batch_size": config.activation_batch_size,
        "candidate_min_active_size": config.min_active_size,
        "candidate_max_active_size": config.max_active_size,
        "max_candidate_rounds": config.max_candidate_rounds,
        "tail_diagnostic_rounds": len(diagnostics(data)),
        "total_warnings": len(warning_records_for_run),
        "activated_poor_tail_rank_warnings": counts.get("activated_poor_tail_rank", 0),
        "deactivated_tail_top_k_warnings": counts.get("deactivated_tail_top_k", 0),
        "low_generic_tail_overlap_warnings": counts.get("low_generic_tail_overlap", 0),
        "nonpositive_correlation_warnings": counts.get("nonpositive_correlation", 0),
    }


def round_summary_rows(
    data: dict[str, Any],
    config: DiagnosticConfig,
    alpha: float,
    case_id: int,
) -> list[dict[str, Any]]:
    rid = str(data.get("run_id", run_id(config, alpha, case_id)))
    log_by_index = round_log_by_index(data)
    rows: list[dict[str, Any]] = []
    for diag in diagnostics(data):
        idx = int(diag.get("round_index", -1))
        stage = log_by_index.get(idx, {})
        top_k = len(diag.get("top_generic_candidates") or [])
        activated_count = len(diag.get("activated_candidates") or [])
        after_activation = stage.get("active_count_after_activation", "")
        after_deactivation = stage.get("active_count_after_deactivation", "")
        if after_activation in ("", None, 0):
            after_activation = diag.get("active_count_after_round", "")
        if after_deactivation in ("", None, 0):
            after_deactivation = diag.get("active_count_after_round", "")
        overlap = parse_float(diag.get("top_k_overlap_generic_tail"))
        activated_overlap = parse_float(diag.get("activated_tail_top_k_overlap"))
        rows.append({
            "run_id": rid,
            "config_name": config.name,
            "alpha": alpha,
            "case_id": case_id,
            "round_index": idx,
            "active_count_before_round": diag.get("active_count_before_round", ""),
            "active_count_after_activation": after_activation,
            "active_count_after_deactivation": after_deactivation,
            "tail_scenario_count": diag.get("tail_scenario_count", ""),
            "top_k": top_k,
            "top_k_overlap_generic_tail": diag.get("top_k_overlap_generic_tail", ""),
            "top_k_overlap_fraction": (overlap / top_k if top_k > 0 and overlap is not None else None),
            "activated_tail_top_k_overlap": diag.get("activated_tail_top_k_overlap", ""),
            "activated_tail_top_k_fraction": (
                activated_overlap / activated_count
                if activated_count > 0 and activated_overlap is not None
                else None
            ),
            "deactivated_tail_bottom_k_overlap": diag.get("deactivated_tail_bottom_k_overlap", ""),
            "deactivated_tail_top_k_warning_count": diag.get("deactivated_tail_top_k_warning_count", ""),
            "selected_tail_top_k_overlap": diag.get("selected_tail_top_k_overlap", ""),
            "spearman_generic_tail": diag.get("spearman_generic_tail", ""),
            "pearson_generic_tail": diag.get("pearson_generic_tail", ""),
            "activated_count": activated_count,
            "deactivated_count": len(diag.get("deactivated_candidates") or []),
            "selected_count": len(diag.get("selected_candidates") or []),
            "protected_selected_count": len(diag.get("protected_selected_candidates") or []),
        })
    return rows


def recent_tail_rank(diag: dict[str, Any], candidate_id: Any) -> Any:
    try:
        candidate = int(candidate_id)
    except (TypeError, ValueError):
        return ""
    for item in diag.get("top_recent_tail_candidates") or []:
        if isinstance(item, dict) and item.get("candidate_id") == candidate:
            return item.get("rank", "")
    return ""


def candidate_event_rows(
    data: dict[str, Any],
    config: DiagnosticConfig,
    alpha: float,
    case_id: int,
) -> list[dict[str, Any]]:
    rid = str(data.get("run_id", run_id(config, alpha, case_id)))
    rows: list[dict[str, Any]] = []
    for diag in diagnostics(data):
        round_index = diag.get("round_index", "")
        for event in diag.get("candidate_events") or []:
            if not isinstance(event, dict):
                continue
            rows.append({
                "run_id": rid,
                "config_name": config.name,
                "alpha": alpha,
                "case_id": case_id,
                "round_index": round_index,
                "candidate_id": event.get("candidate_id", ""),
                "event_type": event.get("event_type", ""),
                "generic_score": event.get("generic_score", ""),
                "tail_empirical_score": event.get("tail_empirical_score", ""),
                "tail_excess_score": event.get("tail_excess_score", ""),
                "recent_tail_score": event.get("recent_tail_score", ""),
                "rank_generic": event.get("rank_generic", ""),
                "rank_tail_empirical": event.get("rank_tail_empirical", ""),
                "rank_tail_excess": event.get("rank_tail_excess", ""),
                "rank_recent_tail": recent_tail_rank(diag, event.get("candidate_id")),
                "was_active_before": event.get("was_active_before", ""),
                "was_active_after": event.get("was_active_after", ""),
                "was_selected": event.get("was_selected", ""),
                "was_protected": event.get("was_protected", ""),
                "was_activated": event.get("was_activated", ""),
                "was_deactivated": event.get("was_deactivated", ""),
                "warning": event.get("warning", ""),
            })
    return rows


def aggregate_warning_summary(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, float, int, str, str], dict[str, Any]] = {}
    for record in records:
        key = (
            str(record["config_name"]),
            float(record["alpha"]),
            int(record["case_id"]),
            str(record["warning_type"]),
            str(record["run_id"]),
        )
        row = grouped.setdefault(
            key,
            {
                "config_name": key[0],
                "alpha": key[1],
                "case_id": key[2],
                "warning_type": key[3],
                "count": 0,
                "run_id": key[4],
                "round_indices": set(),
                "candidate_ids": set(),
            },
        )
        row["count"] += 1
        if record.get("round_index") not in ("", None):
            row["round_indices"].add(record.get("round_index"))
        if record.get("candidate_id") not in ("", None):
            row["candidate_ids"].add(record.get("candidate_id"))
    out = []
    for row in grouped.values():
        out.append({
            **row,
            "round_indices": sorted(row["round_indices"]),
            "candidate_ids": sorted(row["candidate_ids"]),
        })
    return sorted(
        out,
        key=lambda r: (r["config_name"], r["alpha"], r["case_id"], r["warning_type"], r["run_id"]),
    )


def classify_alignment(metrics: dict[str, Any]) -> str:
    mean_overlap = parse_float(metrics.get("mean_top_k_overlap_fraction"))
    mean_spearman = parse_float(metrics.get("mean_spearman"))
    deactivated = int(metrics.get("total_deactivated_tail_top_k_warnings") or 0)
    if mean_overlap is None or mean_spearman is None:
        return "inconclusive"
    if mean_overlap >= 0.60 and mean_spearman >= 0.70 and deactivated == 0:
        return "strong_alignment"
    if 0.35 <= mean_overlap < 0.60 and mean_spearman > 0.50 and deactivated == 0:
        return "mixed_alignment"
    if mean_overlap < 0.35 or mean_spearman <= 0.50 or deactivated > 0:
        return "poor_alignment"
    return "inconclusive"


def recommended_decision(metrics: dict[str, Any]) -> str:
    classification = classify_alignment(metrics)
    activated_poor = int(metrics.get("total_activated_poor_tail_rank_warnings") or 0)
    deactivated = int(metrics.get("total_deactivated_tail_top_k_warnings") or 0)
    if classification == "strong_alignment" and activated_poor == 0:
        return "A. Generic scoring appears sufficient for now; focus on parameter tuning."
    if deactivated > 0:
        return "C. Tail-aware deactivation protection appears necessary."
    if activated_poor > 0 and classification in {"mixed_alignment", "poor_alignment"}:
        return "B. Tail-aware activation appears justified, but deactivation is safe."
    if classification == "strong_alignment":
        return "A. Generic scoring appears sufficient for now; focus on parameter tuning."
    return "D. Evidence remains inconclusive; broaden diagnostics slightly or inspect manually."


def aggregate_alignment(
    group_name: str,
    run_rows: list[dict[str, Any]],
    round_rows: list[dict[str, Any]],
    warning_records_for_group: list[dict[str, Any]],
) -> dict[str, Any]:
    low_overlap_rounds = [
        row for row in round_rows
        if (parse_float(row.get("top_k_overlap_fraction")) is not None and
            parse_float(row.get("top_k_overlap_fraction")) < 0.25)
    ]
    nonpositive_rounds = [
        row for row in round_rows
        if ((parse_float(row.get("spearman_generic_tail")) is not None and
             parse_float(row.get("spearman_generic_tail")) <= 0.0) or
            (parse_float(row.get("pearson_generic_tail")) is not None and
             parse_float(row.get("pearson_generic_tail")) <= 0.0))
    ]
    warning_count = warning_counts(warning_records_for_group)
    round_count = len(round_rows)
    metrics = {
        "group_name": group_name,
        "run_count": len(run_rows),
        "round_count": round_count,
        "mean_top_k_overlap_fraction": mean(parse_float(row.get("top_k_overlap_fraction")) for row in round_rows),
        "min_top_k_overlap_fraction": min_finite(parse_float(row.get("top_k_overlap_fraction")) for row in round_rows),
        "mean_spearman": mean(parse_float(row.get("spearman_generic_tail")) for row in round_rows),
        "min_spearman": min_finite(parse_float(row.get("spearman_generic_tail")) for row in round_rows),
        "mean_pearson": mean(parse_float(row.get("pearson_generic_tail")) for row in round_rows),
        "min_pearson": min_finite(parse_float(row.get("pearson_generic_tail")) for row in round_rows),
        "total_activated_poor_tail_rank_warnings": warning_count.get("activated_poor_tail_rank", 0),
        "total_deactivated_tail_top_k_warnings": warning_count.get("deactivated_tail_top_k", 0),
        "fraction_rounds_low_overlap": len(low_overlap_rounds) / round_count if round_count else None,
        "fraction_rounds_nonpositive_correlation": len(nonpositive_rounds) / round_count if round_count else None,
        "mean_objective": mean(parse_float(row.get("objective")) for row in run_rows),
        "mean_runtime": mean(parse_float(row.get("runtime_seconds")) for row in run_rows),
        "mean_final_active_fraction": mean(parse_float(row.get("final_active_candidate_fraction")) for row in run_rows),
    }
    metrics["classification"] = classify_alignment(metrics)
    return metrics


def aggregate_outputs(
    planned: list[tuple[DiagnosticConfig, float, int]],
) -> tuple[
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
]:
    run_rows: list[dict[str, Any]] = []
    round_rows: list[dict[str, Any]] = []
    event_rows: list[dict[str, Any]] = []
    warning_records_all: list[dict[str, Any]] = []

    for config, alpha, case_id in planned:
        rid = run_id(config, alpha, case_id)
        json_path = OUT_DIR / "json" / f"{rid}.json"
        if not json_path.exists():
            warning_records_all.append({
                "run_id": rid,
                "config_name": config.name,
                "alpha": alpha,
                "case_id": case_id,
                "round_index": "",
                "candidate_id": "",
                "warning_type": "missing_tail_diagnostics",
            })
            continue
        data = json.loads(json_path.read_text(encoding="utf-8"))
        warnings_for_run = warning_records(data, config, alpha, case_id)
        warning_records_all.extend(warnings_for_run)
        run_rows.append(run_summary_row(data, config, alpha, case_id, warnings_for_run))
        round_rows.extend(round_summary_rows(data, config, alpha, case_id))
        event_rows.extend(candidate_event_rows(data, config, alpha, case_id))

    warning_summary_rows = aggregate_warning_summary(warning_records_all)

    config_rows: list[dict[str, Any]] = []
    for config in selected_configs(None):
        runs = [row for row in run_rows if row["config_name"] == config.name]
        rounds = [row for row in round_rows if row["config_name"] == config.name]
        warnings = [row for row in warning_records_all if row["config_name"] == config.name]
        if runs or rounds or warnings:
            config_rows.append(aggregate_alignment(config.name, runs, rounds, warnings))

    alpha_rows: list[dict[str, Any]] = []
    for alpha in sorted({float(row["alpha"]) for row in run_rows}):
        runs = [row for row in run_rows if float(row["alpha"]) == alpha]
        rounds = [row for row in round_rows if float(row["alpha"]) == alpha]
        warnings = [row for row in warning_records_all if float(row["alpha"]) == alpha]
        alpha_rows.append(aggregate_alignment(str(alpha), runs, rounds, warnings))

    decision_rows: list[dict[str, Any]] = []
    overall = aggregate_alignment("overall", run_rows, round_rows, warning_records_all)
    decision_rows.append({
        "group_type": "overall",
        **overall,
        "recommended_decision": recommended_decision(overall),
    })
    for row in config_rows:
        decision_rows.append({
            "group_type": "config",
            **row,
            "recommended_decision": recommended_decision(row),
        })
    for row in alpha_rows:
        decision_rows.append({
            "group_type": "alpha",
            **row,
            "recommended_decision": recommended_decision(row),
        })

    return (
        run_rows,
        round_rows,
        event_rows,
        warning_summary_rows,
        config_rows,
        alpha_rows,
        decision_rows,
    )


def run_grid(
    planned: list[tuple[DiagnosticConfig, float, int]],
    time_limit: float,
    skip_existing: bool,
    dry_run: bool,
    stop_after_failures: int,
) -> list[dict[str, Any]]:
    manifest: list[dict[str, Any]] = []
    failures = 0
    split_cache: dict[tuple[float, int], tuple[list[int], list[int]]] = {}
    for config, alpha, case_id in planned:
        train, test = split_cache.setdefault((alpha, case_id), split_for_case(case_id))
        save_split(alpha, case_id, train, test)
        rid, json_path, log_path, cmd = build_command(config, alpha, case_id, train, test, time_limit)
        status = "planned"
        return_code: int | str = ""
        if skip_existing and json_path.exists():
            status = "skipped-existing"
        elif dry_run:
            status = "dry-run"
        else:
            json_path.parent.mkdir(parents=True, exist_ok=True)
            log_path.parent.mkdir(parents=True, exist_ok=True)
            print(f"Running {rid}")
            with log_path.open("w", encoding="utf-8") as log:
                proc = subprocess.run(
                    cmd,
                    cwd=PROJECT_DIR,
                    text=True,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
            return_code = proc.returncode
            if proc.returncode == 0:
                status = "completed"
            else:
                status = "failed"
                failures += 1
                if failures >= stop_after_failures:
                    manifest.append({
                        "run_id": rid,
                        "config_name": config.name,
                        "alpha": alpha,
                        "case_id": case_id,
                        "seed": SEED_BASE + case_id,
                        "json_path": str(json_path),
                        "log_path": str(log_path),
                        "status": status,
                        "return_code": return_code,
                        "command": " ".join(cmd),
                    })
                    break
        manifest.append({
            "run_id": rid,
            "config_name": config.name,
            "alpha": alpha,
            "case_id": case_id,
            "seed": SEED_BASE + case_id,
            "json_path": str(json_path),
            "log_path": str(log_path),
            "status": status,
            "return_code": return_code,
            "command": " ".join(cmd),
        })
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-existing", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--time-limit", type=float, default=600.0)
    parser.add_argument("--max-runs", type=int, default=0)
    parser.add_argument("--stop-after-failures", type=int, default=1)
    parser.add_argument("--alpha", default="")
    parser.add_argument("--case-id", default="")
    parser.add_argument("--config", default="")
    args = parser.parse_args()

    if args.time_limit < 0.0:
        raise RuntimeError("--time-limit must be nonnegative.")
    if args.max_runs < 0:
        raise RuntimeError("--max-runs must be nonnegative.")
    if args.stop_after_failures <= 0:
        raise RuntimeError("--stop-after-failures must be positive.")

    planned = [
        (config, alpha, case_id)
        for alpha in selected_alphas(args.alpha)
        for case_id in selected_case_ids(args.case_id)
        for config in selected_configs(args.config)
    ]
    if args.max_runs > 0:
        planned = planned[:args.max_runs]

    manifest = run_grid(
        planned,
        args.time_limit,
        args.skip_existing,
        args.dry_run,
        args.stop_after_failures,
    )
    write_csv(COMMAND_MANIFEST_CSV, manifest, MANIFEST_COLUMNS)
    if args.dry_run:
        print(f"Wrote {COMMAND_MANIFEST_CSV}")
        return 0

    (
        run_rows,
        round_rows,
        event_rows,
        warning_summary_rows,
        config_rows,
        alpha_rows,
        decision_rows,
    ) = aggregate_outputs(planned)

    write_csv(RUN_SUMMARY_CSV, run_rows, RUN_COLUMNS)
    write_csv(ROUND_SUMMARY_CSV, round_rows, ROUND_COLUMNS)
    write_csv(CANDIDATE_EVENTS_CSV, event_rows, CANDIDATE_EVENT_COLUMNS)
    write_csv(WARNING_SUMMARY_CSV, warning_summary_rows, WARNING_COLUMNS)
    write_csv(ALIGNMENT_BY_CONFIG_CSV, config_rows, ALIGNMENT_COLUMNS)
    write_csv(ALIGNMENT_BY_ALPHA_CSV, alpha_rows, ALIGNMENT_COLUMNS)
    write_csv(DECISION_SUMMARY_CSV, decision_rows, DECISION_COLUMNS)

    print(f"Wrote {RUN_SUMMARY_CSV}")
    print(f"Wrote {ROUND_SUMMARY_CSV}")
    print(f"Wrote {CANDIDATE_EVENTS_CSV}")
    print(f"Wrote {WARNING_SUMMARY_CSV}")
    print(f"Wrote {ALIGNMENT_BY_CONFIG_CSV}")
    print(f"Wrote {ALIGNMENT_BY_ALPHA_CSV}")
    print(f"Wrote {DECISION_SUMMARY_CSV}")
    print(f"Wrote {COMMAND_MANIFEST_CSV}")
    print(f"Planned runs: {len(planned)}")
    print(f"Produced JSON runs: {len(run_rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
