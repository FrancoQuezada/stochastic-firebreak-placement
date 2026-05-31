#!/usr/bin/env python3
"""Phase 1Q CVaR-tail score diagnostics for restricted FPP-CVaR."""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
from pathlib import Path
from typing import Any


PROJECT_DIR = Path(__file__).resolve().parents[2]
REPO_ROOT = PROJECT_DIR.parent
OUT_DIR = PROJECT_DIR / "results" / "new_method_phase1q_cvar_tail_score_diagnostics"
LANDSCAPE = "Sub20"
FOREST_PATH = REPO_ROOT / "sample_test" / "data" / "CanadianFBP" / LANDSCAPE
RESULTS_PATH = REPO_ROOT / "sample_test" / LANDSCAPE
TRAIN_COUNT = 100
TEST_COUNT = 100
SEED_BASE = 41000
CVAR_BETA = 0.9
MIP_GAP = 0.001
THREADS = 1

RUN_SUMMARY_CSV = OUT_DIR / "phase1q_tail_score_run_summary.csv"
ROUND_SUMMARY_CSV = OUT_DIR / "phase1q_tail_score_round_summary.csv"
CANDIDATE_EVENTS_CSV = OUT_DIR / "phase1q_tail_score_candidate_events.csv"
ALIGNMENT_SUMMARY_CSV = OUT_DIR / "phase1q_tail_score_alignment_summary.csv"
WARNINGS_CSV = OUT_DIR / "phase1q_tail_score_warnings.csv"

RUN_COLUMNS = [
    "run_id",
    "alpha",
    "train_count",
    "test_count",
    "case_id",
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
]

ROUND_COLUMNS = [
    "run_id",
    "round_index",
    "active_count_before_round",
    "active_count_after_activation",
    "active_count_after_deactivation",
    "tail_scenario_count",
    "top_k_overlap_generic_tail",
    "activated_tail_top_k_overlap",
    "deactivated_tail_bottom_k_overlap",
    "deactivated_tail_top_k_warning_count",
    "selected_tail_top_k_overlap",
    "spearman_generic_tail",
    "pearson_generic_tail",
]

CANDIDATE_EVENT_COLUMNS = [
    "run_id",
    "round_index",
    "candidate_id",
    "event_type",
    "generic_score",
    "tail_empirical_score",
    "tail_excess_score",
    "recent_tail_score",
    "was_active_before",
    "was_active_after",
    "was_selected",
    "was_protected",
    "was_deactivated",
    "was_activated",
    "rank_generic",
    "rank_tail_empirical",
    "rank_tail_excess",
    "warning",
]

ALIGNMENT_COLUMNS = [
    "run_id",
    "rounds_with_diagnostics",
    "avg_top_k_overlap_generic_tail",
    "avg_top_k_overlap_fraction",
    "avg_activated_tail_top_k_overlap",
    "total_deactivated_tail_top_k_warning_count",
    "avg_spearman_generic_tail",
    "avg_pearson_generic_tail",
    "interpretation",
]

WARNING_COLUMNS = [
    "run_id",
    "round_index",
    "warning_type",
    "candidate_id",
    "message",
]


def alpha_token(alpha: float) -> str:
    return f"{alpha:.6g}".replace(".", "p")


def run_id(alpha: float, case_id: int) -> str:
    return f"phase1q_restricted_bb_cvar_tail_diag_alpha_{alpha_token(alpha)}_case_{case_id}"


def build_command(alpha: float, case_id: int, time_limit: float) -> tuple[str, Path, Path, list[str]]:
    rid = run_id(alpha, case_id)
    json_path = OUT_DIR / "json" / f"{rid}.json"
    log_path = OUT_DIR / "logs" / f"{rid}.log"
    solution_json = OUT_DIR / "solutions" / f"{rid}_solution.json"
    solution_csv = OUT_DIR / "solutions" / f"{rid}_solution.csv"
    raw_csv = OUT_DIR / "phase1q_raw_runner_results.csv"
    seed = SEED_BASE + case_id
    cmd = [
        str(PROJECT_DIR / "build_gpp" / "firebreak_cpp"),
        "run-fpp-restricted-branch-benders-oos",
        "--landscape", LANDSCAPE,
        "--forest-path", str(FOREST_PATH),
        "--results-path", str(RESULTS_PATH),
        "--seed", str(seed),
        "--train-count", str(TRAIN_COUNT),
        "--test-count", str(TEST_COUNT),
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
        "--initial-candidate-size", "50",
        "--candidate-activation-policy", "benders-coefficients",
        "--candidate-activation-batch-size", "20",
        "--candidate-maintenance-policy", "benders-coefficients",
        "--candidate-deactivation-batch-size", "20",
        "--candidate-min-active-size", "50",
        "--candidate-max-active-size", "70",
        "--candidate-deactivation-min-age", "1",
        "--candidate-reactivation-cooldown-rounds", "1",
        "--max-candidate-rounds", "2",
        "--restricted-heuristic-mode",
        "--stop-after-candidate-rounds", "2",
        "--export-tail-score-diagnostics",
    ]
    return rid, json_path, log_path, cmd


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


def write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({column: format_cell(row.get(column, "")) for column in columns})


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


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


def run_summary_row(data: dict[str, Any], alpha: float, case_id: int) -> dict[str, Any]:
    restricted = restricted_block(data)
    return {
        "run_id": data.get("run_id", run_id(alpha, case_id)),
        "alpha": alpha,
        "train_count": data.get("train_scenario_count", TRAIN_COUNT),
        "test_count": data.get("test_scenario_count", TEST_COUNT),
        "case_id": case_id,
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
    }


def round_summary_rows(data: dict[str, Any]) -> list[dict[str, Any]]:
    rid = str(data.get("run_id", ""))
    log_by_index = round_log_by_index(data)
    rows: list[dict[str, Any]] = []
    for diag in diagnostics(data):
        idx = diag.get("round_index", "")
        stage = log_by_index.get(int(idx), {}) if isinstance(idx, int) else {}
        after_activation = stage.get("active_count_after_activation", "")
        after_deactivation = stage.get("active_count_after_deactivation", "")
        if after_activation in ("", None, 0):
            after_activation = diag.get("active_count_after_round", "")
        if after_deactivation in ("", None, 0):
            after_deactivation = diag.get("active_count_after_round", "")
        rows.append({
            "run_id": rid,
            "round_index": idx,
            "active_count_before_round": diag.get("active_count_before_round", ""),
            "active_count_after_activation": after_activation,
            "active_count_after_deactivation": after_deactivation,
            "tail_scenario_count": diag.get("tail_scenario_count", ""),
            "top_k_overlap_generic_tail": diag.get("top_k_overlap_generic_tail", ""),
            "activated_tail_top_k_overlap": diag.get("activated_tail_top_k_overlap", ""),
            "deactivated_tail_bottom_k_overlap": diag.get("deactivated_tail_bottom_k_overlap", ""),
            "deactivated_tail_top_k_warning_count": diag.get("deactivated_tail_top_k_warning_count", ""),
            "selected_tail_top_k_overlap": diag.get("selected_tail_top_k_overlap", ""),
            "spearman_generic_tail": diag.get("spearman_generic_tail", ""),
            "pearson_generic_tail": diag.get("pearson_generic_tail", ""),
        })
    return rows


def candidate_event_rows(data: dict[str, Any]) -> list[dict[str, Any]]:
    rid = str(data.get("run_id", ""))
    rows: list[dict[str, Any]] = []
    for diag in diagnostics(data):
        round_index = diag.get("round_index", "")
        for event in diag.get("candidate_events") or []:
            if not isinstance(event, dict):
                continue
            row = {column: event.get(column, "") for column in CANDIDATE_EVENT_COLUMNS}
            row["run_id"] = rid
            row["round_index"] = round_index
            rows.append(row)
    return rows


def mean(values: list[float]) -> float | None:
    finite = [value for value in values if math.isfinite(value)]
    if not finite:
        return None
    return sum(finite) / len(finite)


def alignment_summary_row(data: dict[str, Any]) -> dict[str, Any]:
    rid = str(data.get("run_id", ""))
    diag_values = diagnostics(data)
    overlaps = [parse_float(diag.get("top_k_overlap_generic_tail")) for diag in diag_values]
    activated = [parse_float(diag.get("activated_tail_top_k_overlap")) for diag in diag_values]
    spearman = [parse_float(diag.get("spearman_generic_tail")) for diag in diag_values]
    pearson = [parse_float(diag.get("pearson_generic_tail")) for diag in diag_values]
    top_lengths = [
        len(diag.get("top_generic_candidates") or [])
        for diag in diag_values
        if isinstance(diag.get("top_generic_candidates"), list)
    ]
    avg_overlap = mean([value for value in overlaps if value is not None])
    avg_top_k = mean([float(value) for value in top_lengths if value > 0])
    avg_overlap_fraction = (
        avg_overlap / avg_top_k
        if avg_overlap is not None and avg_top_k not in (None, 0.0)
        else None
    )
    total_deactivated_warnings = sum(
        int(diag.get("deactivated_tail_top_k_warning_count") or 0)
        for diag in diag_values
    )
    avg_spearman = mean([value for value in spearman if value is not None])
    if not diag_values:
        interpretation = "missing diagnostics"
    elif total_deactivated_warnings > 0:
        interpretation = "tail-important deactivation warning observed"
    elif avg_overlap_fraction is not None and avg_overlap_fraction >= 0.5 and (
        avg_spearman is None or avg_spearman >= 0.25
    ):
        interpretation = "generic scoring appears moderately aligned with empirical tail scores"
    elif avg_overlap_fraction is not None and avg_overlap_fraction < 0.25:
        interpretation = "generic scoring appears weakly aligned with empirical tail scores"
    else:
        interpretation = "alignment evidence is inconclusive"
    return {
        "run_id": rid,
        "rounds_with_diagnostics": len(diag_values),
        "avg_top_k_overlap_generic_tail": avg_overlap,
        "avg_top_k_overlap_fraction": avg_overlap_fraction,
        "avg_activated_tail_top_k_overlap": mean([value for value in activated if value is not None]),
        "total_deactivated_tail_top_k_warning_count": total_deactivated_warnings,
        "avg_spearman_generic_tail": avg_spearman,
        "avg_pearson_generic_tail": mean([value for value in pearson if value is not None]),
        "interpretation": interpretation,
    }


def warning_rows(data: dict[str, Any]) -> list[dict[str, Any]]:
    rid = str(data.get("run_id", ""))
    rows: list[dict[str, Any]] = []
    diag_values = diagnostics(data)
    if not diag_values:
        return [{
            "run_id": rid,
            "round_index": "",
            "warning_type": "missing_tail_score_diagnostics",
            "candidate_id": "",
            "message": "tail_score_diagnostics missing from JSON",
        }]

    for diag in diag_values:
        idx = diag.get("round_index", "")
        top_k = len(diag.get("top_generic_candidates") or [])
        overlap = parse_float(diag.get("top_k_overlap_generic_tail"))
        if int(diag.get("tail_scenario_count") or 0) == 0:
            rows.append({
                "run_id": rid,
                "round_index": idx,
                "warning_type": "missing_tail_scenarios",
                "candidate_id": "",
                "message": "tail scenario IDs could not be inferred",
            })
        if top_k > 0 and overlap is not None and overlap / top_k < 0.25:
            rows.append({
                "run_id": rid,
                "round_index": idx,
                "warning_type": "low_generic_tail_overlap",
                "candidate_id": "",
                "message": f"generic/tail top-K overlap {overlap:g}/{top_k}",
            })
        spearman = parse_float(diag.get("spearman_generic_tail"))
        if spearman is not None and spearman <= 0.0:
            rows.append({
                "run_id": rid,
                "round_index": idx,
                "warning_type": "nonpositive_rank_correlation",
                "candidate_id": "",
                "message": f"Spearman generic/tail correlation {spearman:g}",
            })
        if int(diag.get("deactivated_tail_top_k_warning_count") or 0) > 0:
            rows.append({
                "run_id": rid,
                "round_index": idx,
                "warning_type": "deactivated_tail_top_k",
                "candidate_id": "",
                "message": "one or more deactivated candidates were in the empirical-tail top-K set",
            })
        if not diag.get("top_recent_tail_candidates"):
            rows.append({
                "run_id": rid,
                "round_index": idx,
                "warning_type": "no_recent_tail_scores",
                "candidate_id": "",
                "message": "recent tail-score list is empty",
            })
        for note in diag.get("notes") or []:
            rows.append({
                "run_id": rid,
                "round_index": idx,
                "warning_type": "diagnostic_note",
                "candidate_id": "",
                "message": note,
            })
        for event in diag.get("candidate_events") or []:
            if not isinstance(event, dict):
                continue
            warning = str(event.get("warning") or "")
            if warning:
                rows.append({
                    "run_id": rid,
                    "round_index": idx,
                    "warning_type": warning,
                    "candidate_id": event.get("candidate_id", ""),
                    "message": f"candidate event warning: {warning}",
                })
    return rows


def run_case(alpha: float, case_id: int, time_limit: float, skip_existing: bool, dry_run: bool) -> Path:
    rid, json_path, log_path, cmd = build_command(alpha, case_id, time_limit)
    if skip_existing and json_path.exists():
        print(f"Skipping existing Phase 1Q run: {json_path}")
        return json_path
    if dry_run:
        print(" ".join(cmd))
        return json_path
    log_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.parent.mkdir(parents=True, exist_ok=True)
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
    if proc.returncode != 0:
        raise RuntimeError(f"Phase 1Q diagnostic run failed; see {log_path}")
    return json_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-existing", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--time-limit", type=float, default=600.0)
    parser.add_argument("--case-id", type=int, default=0)
    parser.add_argument("--alpha", type=float, default=0.02)
    args = parser.parse_args()

    json_path = run_case(
        args.alpha,
        args.case_id,
        args.time_limit,
        args.skip_existing,
        args.dry_run,
    )
    if args.dry_run:
        return 0
    if not json_path.exists():
        raise RuntimeError(f"Expected JSON output was not created: {json_path}")

    data = read_json(json_path)
    run_rows = [run_summary_row(data, args.alpha, args.case_id)]
    round_rows = round_summary_rows(data)
    event_rows = candidate_event_rows(data)
    alignment_rows = [alignment_summary_row(data)]
    warning_rows_out = warning_rows(data)

    write_csv(RUN_SUMMARY_CSV, run_rows, RUN_COLUMNS)
    write_csv(ROUND_SUMMARY_CSV, round_rows, ROUND_COLUMNS)
    write_csv(CANDIDATE_EVENTS_CSV, event_rows, CANDIDATE_EVENT_COLUMNS)
    write_csv(ALIGNMENT_SUMMARY_CSV, alignment_rows, ALIGNMENT_COLUMNS)
    write_csv(WARNINGS_CSV, warning_rows_out, WARNING_COLUMNS)

    print(f"Wrote {RUN_SUMMARY_CSV}")
    print(f"Wrote {ROUND_SUMMARY_CSV}")
    print(f"Wrote {CANDIDATE_EVENTS_CSV}")
    print(f"Wrote {ALIGNMENT_SUMMARY_CSV}")
    print(f"Wrote {WARNINGS_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
