#!/usr/bin/env python3
"""Small Phase 1N rerun for restricted FPP-CVaR global time-budget diagnostics."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PROJECT_DIR = Path(__file__).resolve().parents[2]
REPO_ROOT = PROJECT_DIR.parent
DEFAULT_OUTPUT_DIR = PROJECT_DIR / "results" / "new_method_phase1n_fpp_cvar_triage" / "rerun"
LANDSCAPE = "Sub20"
FOREST_PATH = REPO_ROOT / "sample_test" / "data" / "CanadianFBP" / LANDSCAPE
RESULTS_PATH = REPO_ROOT / "sample_test" / LANDSCAPE
TRAIN_COUNT = 100
TEST_COUNT = 100
SEED_BASE = 41000
RISK_MEASURE = "cvar"
CVAR_BETA = 0.9
MIP_GAP = 0.001
THREADS = 1


@dataclass(frozen=True)
class Method:
    label: str
    command: str
    restricted: bool = False
    heuristic: bool = False


METHODS = [
    Method("FPP-Branch-Benders-CVaR", "run-fpp-branch-benders-oos"),
    Method("FPP-Restricted-Branch-Benders-CVaR-Exact", "run-fpp-restricted-branch-benders-oos", True, False),
    Method("FPP-Restricted-Branch-Benders-CVaR-Heuristic", "run-fpp-restricted-branch-benders-oos", True, True),
]

SUMMARY_COLUMNS = [
    "method",
    "alpha",
    "case_id",
    "seed",
    "status",
    "objective",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "lazy_cuts_added",
    "candidate_incumbents_checked",
    "final_max_cut_violation",
    "restricted_candidate_enabled",
    "global_optimality_certified",
    "eventually_activated_all",
    "stopped_before_full_activation",
    "final_active_candidate_fraction",
    "candidate_rounds",
    "cut_pool_size",
    "cuts_reused_in_full_stage",
    "restricted_stage_count",
    "restricted_initial_stage_runtime",
    "restricted_activation_stage_runtime_total",
    "restricted_final_full_stage_runtime",
    "restricted_final_stage_time_limit",
    "restricted_global_time_limit",
    "restricted_elapsed_time_total",
    "restricted_time_budget_exhausted",
    "initial_candidate_policy",
    "activation_policy",
    "notes",
]


def available_scenario_ids() -> list[int]:
    messages = RESULTS_PATH / "Messages"
    ids = []
    for path in messages.glob("MessagesFile*.csv"):
        digits = "".join(ch for ch in path.stem if ch.isdigit())
        if digits:
            ids.append(int(digits))
    if len(ids) < TRAIN_COUNT + TEST_COUNT:
        raise RuntimeError("Not enough Sub20 scenarios for the Phase 1N 100/100 split.")
    return sorted(set(ids))


def generate_split(seed: int) -> tuple[list[int], list[int]]:
    ids = available_scenario_ids()
    rng = random.Random(seed)
    shuffled = ids[:]
    rng.shuffle(shuffled)
    train_ids = sorted(shuffled[:TRAIN_COUNT])
    test_ids = sorted(shuffled[TRAIN_COUNT:TRAIN_COUNT + TEST_COUNT])
    return train_ids, test_ids


def alpha_token(alpha: float) -> str:
    return f"{alpha:.6g}".replace(".", "p")


def run_id(method: Method, alpha: float, case_id: int) -> str:
    if method.label == "FPP-Branch-Benders-CVaR":
        token = "branch_benders_cvar"
    elif method.heuristic:
        token = "restricted_bb_cvar_heuristic"
    else:
        token = "restricted_bb_cvar_exact"
    return f"phase1n_{token}_alpha_{alpha_token(alpha)}_case_{case_id}"


def build_command(
    method: Method,
    alpha: float,
    case_id: int,
    train_ids: list[int],
    test_ids: list[int],
    output_dir: Path,
    time_limit: float,
) -> tuple[str, Path, Path, list[str]]:
    rid = run_id(method, alpha, case_id)
    json_path = output_dir / "json" / f"{rid}.json"
    log_path = output_dir / "logs" / f"{rid}.log"
    solution_json = output_dir / "solutions" / f"{rid}_solution.json"
    solution_csv = output_dir / "solutions" / f"{rid}_solution.csv"
    raw_csv = output_dir / "raw_runner_results.csv"
    cmd = [
        str(PROJECT_DIR / "build_gpp" / "firebreak_cpp"),
        method.command,
        "--landscape", LANDSCAPE,
        "--forest-path", str(FOREST_PATH),
        "--results-path", str(RESULTS_PATH),
        "--train-ids", ",".join(str(v) for v in train_ids),
        "--test-ids", ",".join(str(v) for v in test_ids),
        "--alpha", f"{alpha:.6g}",
        "--risk-measure", RISK_MEASURE,
        "--cvar-beta", f"{CVAR_BETA:.6g}",
        "--time-limit", f"{time_limit:.6g}",
        "--mip-gap", f"{MIP_GAP:.6g}",
        "--threads", str(THREADS),
        "--run-id", rid,
        "--output-json", str(json_path),
        "--output-csv", str(raw_csv),
        "--solution-json", str(solution_json),
        "--solution-csv", str(solution_csv),
    ]
    if method.restricted:
        cmd.extend([
            "--initial-candidate-policy", "burn-frequency",
            "--initial-candidate-size", "50",
            "--candidate-activation-policy", "benders-coefficients",
            "--candidate-activation-batch-size", "20",
            "--max-candidate-rounds", "2",
        ])
        if method.heuristic:
            cmd.extend(["--restricted-heuristic-mode", "--stop-after-candidate-rounds", "2"])
        else:
            cmd.extend(["--restricted-exact-mode", "--eventually-activate-all"])
    return rid, json_path, log_path, cmd


def read_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def branch(data: dict[str, Any]) -> dict[str, Any]:
    return data.get("branch_benders") or {}


def row_from_json(method: Method, alpha: float, case_id: int, seed: int, data: dict[str, Any]) -> dict[str, Any]:
    restricted = data.get("restricted_candidate") or {}
    branch_block = branch(data)
    round_log = restricted.get("round_log") or []
    notes = data.get("notes", [])
    if not isinstance(notes, list):
        notes = [str(notes)]
    return {
        "method": method.label,
        "alpha": alpha,
        "case_id": case_id,
        "seed": seed,
        "status": data.get("solver_status", ""),
        "objective": data.get("objective_in_sample", ""),
        "best_bound": data.get("best_bound", ""),
        "mip_gap": data.get("mip_gap", ""),
        "runtime_seconds": data.get("runtime_seconds", ""),
        "lazy_cuts_added": branch_block.get("lazy_cuts_added", ""),
        "candidate_incumbents_checked": branch_block.get("candidate_incumbents_checked", ""),
        "final_max_cut_violation": data.get("max_cut_violation", branch_block.get("max_cut_violation", "")),
        "restricted_candidate_enabled": restricted.get("enabled", False),
        "global_optimality_certified": restricted.get("global_optimality_certified", ""),
        "eventually_activated_all": restricted.get("eventually_activated_all", ""),
        "stopped_before_full_activation": restricted.get("stopped_before_full_activation", ""),
        "final_active_candidate_fraction": restricted.get("final_active_fraction", ""),
        "candidate_rounds": restricted.get("candidate_rounds", ""),
        "cut_pool_size": restricted.get("cut_pool_size", ""),
        "cuts_reused_in_full_stage": restricted.get("cuts_reused_in_full_stage", ""),
        "restricted_stage_count": len(round_log),
        "restricted_initial_stage_runtime": restricted.get("initial_stage_runtime_seconds", ""),
        "restricted_activation_stage_runtime_total": restricted.get("activation_stage_runtime_total_seconds", ""),
        "restricted_final_full_stage_runtime": restricted.get("final_full_stage_runtime_seconds", ""),
        "restricted_final_stage_time_limit": restricted.get("final_stage_time_limit_seconds", ""),
        "restricted_global_time_limit": restricted.get("global_time_limit_seconds", ""),
        "restricted_elapsed_time_total": restricted.get("elapsed_time_total_seconds", ""),
        "restricted_time_budget_exhausted": restricted.get("time_budget_exhausted", ""),
        "initial_candidate_policy": restricted.get("initial_candidate_policy", ""),
        "activation_policy": restricted.get("activation_policy", ""),
        "notes": " | ".join(str(note) for note in notes),
    }


def write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
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
    return value


def parse_float(value: Any) -> float | None:
    if value in ("", None):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def pairwise_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_case: dict[tuple[str, str], dict[str, dict[str, Any]]] = {}
    for row in rows:
        by_case.setdefault((str(row["alpha"]), str(row["case_id"])), {})[row["method"]] = row
    comparisons = [
        ("FPP-Branch-Benders-CVaR", "FPP-Restricted-Branch-Benders-CVaR-Exact", "exact"),
        ("FPP-Branch-Benders-CVaR", "FPP-Restricted-Branch-Benders-CVaR-Heuristic", "heuristic"),
        ("FPP-Restricted-Branch-Benders-CVaR-Exact", "FPP-Restricted-Branch-Benders-CVaR-Heuristic", "heuristic"),
    ]
    out = []
    for (alpha, case_id), methods in sorted(by_case.items()):
        for left_name, right_name, comparison_type in comparisons:
            left = methods.get(left_name)
            right = methods.get(right_name)
            row: dict[str, Any] = {
                "alpha": alpha,
                "case_id": case_id,
                "comparison_type": comparison_type,
                "left_method": left_name,
                "right_method": right_name,
                "left_status": left.get("status", "missing") if left else "missing",
                "right_status": right.get("status", "missing") if right else "missing",
                "objective_diff_right_minus_left": "",
                "abs_objective_diff": "",
                "runtime_ratio_right_over_left": "",
                "right_global_optimality_certified": right.get("global_optimality_certified", "") if right else "",
                "right_eventually_activated_all": right.get("eventually_activated_all", "") if right else "",
                "right_time_budget_exhausted": right.get("restricted_time_budget_exhausted", "") if right else "",
                "right_final_active_fraction": right.get("final_active_candidate_fraction", "") if right else "",
            }
            if left and right:
                left_obj = parse_float(left.get("objective"))
                right_obj = parse_float(right.get("objective"))
                left_runtime = parse_float(left.get("runtime_seconds"))
                right_runtime = parse_float(right.get("runtime_seconds"))
                if left_obj is not None and right_obj is not None:
                    row["objective_diff_right_minus_left"] = right_obj - left_obj
                    row["abs_objective_diff"] = abs(right_obj - left_obj)
                if left_runtime is not None and right_runtime is not None and left_runtime > 0.0:
                    row["runtime_ratio_right_over_left"] = right_runtime / left_runtime
            out.append(row)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--alpha", type=float, default=0.02)
    parser.add_argument("--cases", default="0", help="Comma-separated case ids. Default: 0.")
    parser.add_argument("--time-limit", type=float, default=600.0)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--skip-existing", action="store_true")
    args = parser.parse_args()

    output_dir = args.output_dir
    for subdir in ["json", "logs", "splits", "solutions"]:
        (output_dir / subdir).mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, Any]] = []
    command_rows: list[dict[str, Any]] = []
    for case_id in [int(token) for token in args.cases.split(",") if token.strip()]:
        seed = SEED_BASE + case_id
        train_ids, test_ids = generate_split(seed)
        split_prefix = f"alpha_{alpha_token(args.alpha)}_case_{case_id}_seed_{seed}"
        (output_dir / "splits" / f"{split_prefix}_train.csv").write_text(
            "\n".join(str(v) for v in train_ids) + "\n",
            encoding="utf-8")
        (output_dir / "splits" / f"{split_prefix}_test.csv").write_text(
            "\n".join(str(v) for v in test_ids) + "\n",
            encoding="utf-8")

        for method in METHODS:
            rid, json_path, log_path, cmd = build_command(
                method,
                args.alpha,
                case_id,
                train_ids,
                test_ids,
                output_dir,
                args.time_limit,
            )
            command_status = "planned"
            return_code: int | str = ""
            data = read_json(json_path)
            if args.skip_existing and data is not None:
                command_status = "skipped_existing"
            else:
                print(f"Running {method.label} alpha={args.alpha} case={case_id}", flush=True)
                completed = subprocess.run(
                    cmd,
                    cwd=PROJECT_DIR,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    check=False,
                )
                return_code = completed.returncode
                log_path.write_text(completed.stdout, encoding="utf-8")
                data = read_json(json_path)
                command_status = "completed" if completed.returncode == 0 and data is not None else "failed"
            if data is None:
                rows.append({
                    "method": method.label,
                    "alpha": args.alpha,
                    "case_id": case_id,
                    "seed": seed,
                    "status": "Failed",
                    "notes": f"command failed or JSON missing; log={log_path}",
                })
            else:
                rows.append(row_from_json(method, args.alpha, case_id, seed, data))
            command_rows.append({
                "run_id": rid,
                "method": method.label,
                "alpha": args.alpha,
                "case_id": case_id,
                "seed": seed,
                "command_status": command_status,
                "return_code": return_code,
                "json_path": json_path,
                "log_path": log_path,
                "command": " ".join(cmd),
            })

    summary_csv = output_dir / "phase1n_rerun_summary.csv"
    pairwise_csv = output_dir / "phase1n_rerun_pairwise.csv"
    commands_csv = output_dir / "phase1n_rerun_command_manifest.csv"
    write_csv(summary_csv, rows, SUMMARY_COLUMNS)
    write_csv(
        pairwise_csv,
        pairwise_rows(rows),
        [
            "alpha",
            "case_id",
            "comparison_type",
            "left_method",
            "right_method",
            "left_status",
            "right_status",
            "objective_diff_right_minus_left",
            "abs_objective_diff",
            "runtime_ratio_right_over_left",
            "right_global_optimality_certified",
            "right_eventually_activated_all",
            "right_time_budget_exhausted",
            "right_final_active_fraction",
        ],
    )
    write_csv(
        commands_csv,
        command_rows,
        ["run_id", "method", "alpha", "case_id", "seed", "command_status", "return_code", "json_path", "log_path", "command"],
    )
    print(f"Produced rows: {len(rows)}")
    for method in METHODS:
        statuses: dict[str, int] = {}
        for row in rows:
            if row["method"] == method.label:
                statuses[str(row.get("status", ""))] = statuses.get(str(row.get("status", "")), 0) + 1
        print(f"{method.label}: {statuses}")
    print(f"Wrote {summary_csv}")
    print(f"Wrote {pairwise_csv}")
    print(f"Wrote {commands_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
