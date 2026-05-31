#!/usr/bin/env python3
"""Run the Phase 1M Sub20 FPP-CVaR restricted-candidate comparison."""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import re
import statistics
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


PROJECT_DIR = Path(__file__).resolve().parents[2]
REPO_ROOT = PROJECT_DIR.parent
RESULT_DIR = PROJECT_DIR / "results" / "new_method_phase1m_fpp_cvar_restricted_comparison"
JSON_DIR = RESULT_DIR / "json"
LOG_DIR = RESULT_DIR / "logs"
SPLIT_DIR = RESULT_DIR / "splits"
SOLUTION_DIR = RESULT_DIR / "solutions"

LANDSCAPE = "Sub20"
FOREST_PATH = REPO_ROOT / "sample_test" / "data" / "CanadianFBP" / LANDSCAPE
RESULTS_PATH = REPO_ROOT / "sample_test" / LANDSCAPE

ALPHAS = [0.01, 0.02]
TRAIN_COUNT = 100
REQUESTED_TEST_COUNT = 100
NUM_CASES = 3
SEED_BASE = 41000
MIP_GAP = 0.001
THREADS = 1
RISK_MEASURE = "cvar"
CVAR_BETA = 0.9
DEFAULT_TIME_LIMIT = 600.0

MAIN_CSV = RESULT_DIR / "fpp_cvar_restricted_comparison.csv"
PAIRWISE_CSV = RESULT_DIR / "fpp_cvar_restricted_pairwise.csv"
SUMMARY_CSV = RESULT_DIR / "fpp_cvar_restricted_summary_by_method.csv"
FAILURES_CSV = RESULT_DIR / "fpp_cvar_restricted_failures.csv"
COMMAND_MANIFEST_CSV = RESULT_DIR / "fpp_cvar_restricted_command_manifest.csv"
RAW_RUNNER_CSV = RESULT_DIR / "raw_runner_results.csv"

MAIN_COLUMNS = [
    "method",
    "alpha",
    "train_count",
    "test_count",
    "case_id",
    "seed",
    "status",
    "objective",
    "best_bound",
    "mip_gap",
    "runtime_seconds",
    "selected_firebreaks",
    "risk_measure",
    "cvar_beta",
    "cvar_lambda",
    "expected_loss_component",
    "cvar_loss_component",
    "train_expected_burned_area",
    "test_expected_burned_area",
    "train_empirical_var_burned_area",
    "train_empirical_cvar_burned_area",
    "test_empirical_var_burned_area",
    "test_empirical_cvar_burned_area",
    "lazy_cuts_added",
    "candidate_incumbents_checked",
    "final_max_cut_violation",
    "restricted_candidate_enabled",
    "restricted_candidate_exact_mode",
    "heuristic_mode_enabled",
    "global_optimality_certified",
    "eventually_activated_all",
    "stopped_before_full_activation",
    "initial_candidate_policy",
    "activation_policy",
    "initial_active_candidate_count",
    "final_active_candidate_count",
    "final_active_candidate_fraction",
    "candidate_rounds",
    "restricted_stage_count",
    "restricted_initial_stage_runtime",
    "restricted_activation_stage_runtime_total",
    "restricted_final_full_stage_runtime",
    "restricted_final_stage_time_limit",
    "restricted_global_time_limit",
    "restricted_elapsed_time_total",
    "restricted_time_budget_exhausted",
    "cut_reuse_enabled",
    "cut_pool_size",
    "cuts_reused_in_full_stage",
    "notes",
]


@dataclass(frozen=True)
class MethodSpec:
    label: str
    command: str
    restricted: bool = False
    heuristic: bool = False


METHODS = [
    MethodSpec("FPP-SAA-CVaR", "run-fpp-saa-oos"),
    MethodSpec("FPP-Branch-Benders-CVaR", "run-fpp-branch-benders-oos"),
    MethodSpec("FPP-Restricted-Branch-Benders-CVaR-Exact", "run-fpp-restricted-branch-benders-oos", restricted=True),
    MethodSpec(
        "FPP-Restricted-Branch-Benders-CVaR-Heuristic",
        "run-fpp-restricted-branch-benders-oos",
        restricted=True,
        heuristic=True,
    ),
]


def alpha_token(alpha: float) -> str:
    return f"{alpha:.3f}".replace(".", "p").rstrip("0").rstrip("p")


def csv_join_ints(values: list[int]) -> str:
    return ";".join(str(v) for v in values)


def parse_float(value: Any) -> float | None:
    if value is None:
        return None
    if isinstance(value, (int, float)):
        number = float(value)
        if math.isfinite(number):
            return number
        return None
    if isinstance(value, str):
        try:
            number = float(value)
        except ValueError:
            return None
        if math.isfinite(number):
            return number
    return None


def format_cell(value: Any) -> Any:
    if value is None:
        return ""
    if isinstance(value, float):
        if math.isfinite(value):
            return f"{value:.10g}"
        return ""
    return value


def ensure_dirs() -> None:
    for path in [RESULT_DIR, JSON_DIR, LOG_DIR, SPLIT_DIR, SOLUTION_DIR]:
        path.mkdir(parents=True, exist_ok=True)


def available_scenario_ids() -> list[int]:
    messages_dir = RESULTS_PATH / "Messages"
    pattern = re.compile(r"^MessagesFile([0-9]+)\.csv$")
    ids: list[int] = []
    for path in messages_dir.glob("MessagesFile*.csv"):
        match = pattern.match(path.name)
        if match:
            ids.append(int(match.group(1)))
    ids = sorted(ids)
    if not ids:
        raise RuntimeError(f"No Sub20 message files found under {messages_dir}")
    return ids


def generate_split(ids: list[int], seed: int, train_count: int, test_count: int) -> tuple[list[int], list[int]]:
    if train_count + test_count > len(ids):
        raise RuntimeError("Requested train/test split is larger than available scenario count.")
    shuffled = list(sorted(ids))
    random.Random(seed).shuffle(shuffled)
    train = sorted(shuffled[:train_count])
    test = sorted(shuffled[train_count : train_count + test_count])
    if set(train).intersection(test):
        raise RuntimeError("Generated split is not disjoint.")
    return train, test


def write_ids(path: Path, ids: list[int]) -> None:
    path.write_text("\n".join(str(v) for v in ids) + "\n", encoding="utf-8")


def run_id_for(method: MethodSpec, alpha: float, case_id: int) -> str:
    method_token = (
        method.label.lower()
        .replace("fpp-", "")
        .replace("-branch-benders", "_bb")
        .replace("-restricted", "_restricted")
        .replace("-cvar", "_cvar")
        .replace("-heuristic", "_heuristic")
        .replace("-exact", "_exact")
        .replace("-", "_")
    )
    return f"phase1m_{method_token}_alpha_{alpha_token(alpha)}_case_{case_id}"


def command_for(
    method: MethodSpec,
    alpha: float,
    case_id: int,
    seed: int,
    train_ids: list[int],
    test_ids: list[int],
    test_count: int,
    time_limit: float,
) -> tuple[str, Path, Path, list[str]]:
    run_id = run_id_for(method, alpha, case_id)
    json_path = JSON_DIR / f"{run_id}.json"
    log_path = LOG_DIR / f"{run_id}.log"
    solution_json = SOLUTION_DIR / f"{run_id}_solution.json"
    solution_csv = SOLUTION_DIR / f"{run_id}_solution.csv"

    cmd = [
        str(PROJECT_DIR / "build_gpp" / "firebreak_cpp"),
        method.command,
        "--landscape",
        LANDSCAPE,
        "--forest-path",
        str(FOREST_PATH),
        "--results-path",
        str(RESULTS_PATH),
        "--train-ids",
        ",".join(str(v) for v in train_ids),
        "--test-ids",
        ",".join(str(v) for v in test_ids),
        "--alpha",
        f"{alpha:.6g}",
        "--risk-measure",
        RISK_MEASURE,
        "--cvar-beta",
        f"{CVAR_BETA:.6g}",
        "--time-limit",
        f"{time_limit:.6g}",
        "--mip-gap",
        f"{MIP_GAP:.6g}",
        "--threads",
        str(THREADS),
        "--run-id",
        run_id,
        "--output-json",
        str(json_path),
        "--output-csv",
        str(RAW_RUNNER_CSV),
        "--solution-json",
        str(solution_json),
        "--solution-csv",
        str(solution_csv),
    ]

    if method.restricted:
        cmd.extend(
            [
                "--initial-candidate-policy",
                "burn-frequency",
                "--initial-candidate-size",
                "50",
                "--candidate-activation-policy",
                "benders-coefficients",
                "--candidate-activation-batch-size",
                "20",
                "--max-candidate-rounds",
                "2",
            ]
        )
        if method.heuristic:
            cmd.extend(["--restricted-heuristic-mode", "--stop-after-candidate-rounds", "2"])
        else:
            cmd.extend(["--eventually-activate-all", "--restricted-exact-mode"])

    return run_id, json_path, log_path, cmd


def read_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def row_from_json(
    method: MethodSpec,
    alpha: float,
    train_count: int,
    test_count: int,
    case_id: int,
    seed: int,
    data: dict[str, Any],
    notes_extra: str = "",
) -> dict[str, Any]:
    restricted = data.get("restricted_candidate") or {}
    branch = data.get("branch_benders") or {}
    branch_enabled = bool(branch.get("enabled", data.get("branch_benders_enabled", False)))
    notes = data.get("notes", [])
    if not isinstance(notes, list):
        notes = [str(notes)]
    if notes_extra:
        notes.append(notes_extra)

    return {
        "method": method.label,
        "alpha": alpha,
        "train_count": train_count,
        "test_count": test_count,
        "case_id": case_id,
        "seed": seed,
        "status": data.get("solver_status", ""),
        "objective": data.get("objective_in_sample", ""),
        "best_bound": data.get("best_bound", ""),
        "mip_gap": data.get("mip_gap", ""),
        "runtime_seconds": data.get("runtime_seconds", ""),
        "selected_firebreaks": csv_join_ints(data.get("selected_firebreaks", []) or []),
        "risk_measure": data.get("risk_measure", ""),
        "cvar_beta": data.get("cvar_beta", ""),
        "cvar_lambda": data.get("cvar_lambda", ""),
        "expected_loss_component": data.get("expected_loss_component", ""),
        "cvar_loss_component": data.get("cvar_loss_component", ""),
        "train_expected_burned_area": data.get("train_expected_burned_area", ""),
        "test_expected_burned_area": data.get("test_expected_burned_area", ""),
        "train_empirical_var_burned_area": data.get("train_empirical_var_burned_area", ""),
        "train_empirical_cvar_burned_area": data.get("train_empirical_cvar_burned_area", ""),
        "test_empirical_var_burned_area": data.get("test_empirical_var_burned_area", ""),
        "test_empirical_cvar_burned_area": data.get("test_empirical_cvar_burned_area", ""),
        "lazy_cuts_added": branch.get("lazy_cuts_added", data.get("branch_benders_lazy_cuts_added", "")) if branch_enabled else "",
        "candidate_incumbents_checked": branch.get(
            "candidate_incumbents_checked",
            data.get("branch_benders_candidate_incumbents_checked", ""),
        ) if branch_enabled else "",
        "final_max_cut_violation": data.get("max_cut_violation", branch.get("max_cut_violation", "")),
        "restricted_candidate_enabled": restricted.get("enabled", False),
        "restricted_candidate_exact_mode": restricted.get("exact_mode", ""),
        "heuristic_mode_enabled": restricted.get("heuristic_mode_enabled", ""),
        "global_optimality_certified": restricted.get("global_optimality_certified", ""),
        "eventually_activated_all": restricted.get("eventually_activated_all", ""),
        "stopped_before_full_activation": restricted.get("stopped_before_full_activation", ""),
        "initial_candidate_policy": restricted.get("initial_candidate_policy", restricted.get("initial_policy", "")),
        "activation_policy": restricted.get("activation_policy", ""),
        "initial_active_candidate_count": restricted.get("initial_active_count", ""),
        "final_active_candidate_count": restricted.get("final_active_count", ""),
        "final_active_candidate_fraction": restricted.get("final_active_fraction", ""),
        "candidate_rounds": restricted.get("candidate_rounds", ""),
        "restricted_stage_count": len(restricted.get("round_log", []) or []),
        "restricted_initial_stage_runtime": restricted.get("initial_stage_runtime_seconds", ""),
        "restricted_activation_stage_runtime_total": restricted.get("activation_stage_runtime_total_seconds", ""),
        "restricted_final_full_stage_runtime": restricted.get("final_full_stage_runtime_seconds", ""),
        "restricted_final_stage_time_limit": restricted.get("final_stage_time_limit_seconds", ""),
        "restricted_global_time_limit": restricted.get("global_time_limit_seconds", ""),
        "restricted_elapsed_time_total": restricted.get("elapsed_time_total_seconds", ""),
        "restricted_time_budget_exhausted": restricted.get("time_budget_exhausted", ""),
        "cut_reuse_enabled": restricted.get("cut_reuse_enabled", ""),
        "cut_pool_size": restricted.get("cut_pool_size", ""),
        "cuts_reused_in_full_stage": restricted.get("cuts_reused_in_full_stage", ""),
        "notes": " | ".join(str(note) for note in notes),
    }


def failed_row(
    method: MethodSpec,
    alpha: float,
    train_count: int,
    test_count: int,
    case_id: int,
    seed: int,
    message: str,
) -> dict[str, Any]:
    row = {column: "" for column in MAIN_COLUMNS}
    row.update(
        {
            "method": method.label,
            "alpha": alpha,
            "train_count": train_count,
            "test_count": test_count,
            "case_id": case_id,
            "seed": seed,
            "status": "Failed",
            "risk_measure": RISK_MEASURE,
            "cvar_beta": CVAR_BETA,
            "notes": message,
        }
    )
    return row


def numeric(row: dict[str, Any], key: str) -> float | None:
    return parse_float(row.get(key))


def status_is_time_limit(status: str) -> bool:
    return "time" in status.lower() and "limit" in status.lower()


def row_is_time_limited(row: dict[str, Any]) -> bool:
    status = str(row.get("status", ""))
    if status_is_time_limit(status):
        return True
    if status not in {"Feasible", "RestrictedFeasible"}:
        return False
    runtime = numeric(row, "runtime_seconds")
    gap = numeric(row, "mip_gap")
    if runtime is None:
        return False
    if gap is not None and gap <= MIP_GAP:
        return False
    return runtime >= 0.99 * DEFAULT_TIME_LIMIT


def status_is_feasible(status: str) -> bool:
    return status in {"Feasible", "RestrictedFeasible", "RestrictedHeuristic"}


def write_csv(path: Path, rows: list[dict[str, Any]], columns: list[str]) -> None:
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({column: format_cell(row.get(column, "")) for column in columns})


def method_summary(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    summary_rows: list[dict[str, Any]] = []
    for method in [m.label for m in METHODS]:
        subset = [row for row in rows if row["method"] == method]

        def mean_of(key: str) -> float | None:
            values = [numeric(row, key) for row in subset]
            finite = [value for value in values if value is not None]
            return statistics.mean(finite) if finite else None

        def median_of(key: str) -> float | None:
            values = [numeric(row, key) for row in subset]
            finite = [value for value in values if value is not None]
            return statistics.median(finite) if finite else None

        statuses = [str(row.get("status", "")) for row in subset]
        summary_rows.append(
            {
                "method": method,
                "rows": len(subset),
                "optimal_rows": sum(status == "Optimal" for status in statuses),
                "feasible_rows": sum(status_is_feasible(status) for status in statuses),
                "time_limit_rows": sum(row_is_time_limited(row) for row in subset),
                "failed_rows": sum(status == "Failed" for status in statuses),
                "mean_runtime": mean_of("runtime_seconds"),
                "median_runtime": median_of("runtime_seconds"),
                "max_runtime": max(
                    [numeric(row, "runtime_seconds") for row in subset if numeric(row, "runtime_seconds") is not None],
                    default=None,
                ),
                "mean_objective": mean_of("objective"),
                "mean_best_bound": mean_of("best_bound"),
                "mean_gap": mean_of("mip_gap"),
                "mean_expected_component": mean_of("expected_loss_component"),
                "mean_cvar_component": mean_of("cvar_loss_component"),
                "mean_train_expected_burned_area": mean_of("train_expected_burned_area"),
                "mean_test_expected_burned_area": mean_of("test_expected_burned_area"),
                "mean_train_empirical_cvar_burned_area": mean_of("train_empirical_cvar_burned_area"),
                "mean_test_empirical_cvar_burned_area": mean_of("test_empirical_cvar_burned_area"),
                "mean_lazy_cuts": mean_of("lazy_cuts_added"),
                "mean_candidate_checks": mean_of("candidate_incumbents_checked"),
                "mean_cut_pool_size": mean_of("cut_pool_size"),
                "mean_final_active_fraction": mean_of("final_active_candidate_fraction"),
                "mean_candidate_rounds": mean_of("candidate_rounds"),
            }
        )
    return summary_rows


def pairwise_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_case: dict[tuple[str, str, str], dict[str, dict[str, Any]]] = {}
    for row in rows:
        key = (str(row["alpha"]), str(row["train_count"]), str(row["case_id"]))
        by_case.setdefault(key, {})[str(row["method"])] = row

    comparisons = [
        ("FPP-SAA-CVaR", "FPP-Branch-Benders-CVaR", "exact"),
        ("FPP-SAA-CVaR", "FPP-Restricted-Branch-Benders-CVaR-Exact", "exact"),
        ("FPP-Branch-Benders-CVaR", "FPP-Restricted-Branch-Benders-CVaR-Exact", "exact"),
        ("FPP-SAA-CVaR", "FPP-Restricted-Branch-Benders-CVaR-Heuristic", "heuristic"),
        ("FPP-Branch-Benders-CVaR", "FPP-Restricted-Branch-Benders-CVaR-Heuristic", "heuristic"),
        ("FPP-Restricted-Branch-Benders-CVaR-Exact", "FPP-Restricted-Branch-Benders-CVaR-Heuristic", "heuristic"),
    ]

    out: list[dict[str, Any]] = []
    for (alpha, train_count, case_id), methods in sorted(by_case.items()):
        for left_label, right_label, comparison_type in comparisons:
            left = methods.get(left_label)
            right = methods.get(right_label)
            row: dict[str, Any] = {
                "alpha": alpha,
                "train_count": train_count,
                "case_id": case_id,
                "comparison_type": comparison_type,
                "left_method": left_label,
                "right_method": right_label,
                "left_status": left.get("status", "") if left else "missing",
                "right_status": right.get("status", "") if right else "missing",
                "objective_diff_right_minus_left": "",
                "abs_objective_diff": "",
                "selected_firebreaks_equal": "",
                "runtime_ratio_right_over_left": "",
                "left_runtime": "",
                "right_runtime": "",
                "left_objective": "",
                "right_objective": "",
                "left_test_cvar": "",
                "right_test_cvar": "",
                "test_cvar_diff_right_minus_left": "",
                "right_global_optimality_certified": right.get("global_optimality_certified", "") if right else "",
                "right_eventually_activated_all": right.get("eventually_activated_all", "") if right else "",
                "right_final_active_fraction": right.get("final_active_candidate_fraction", "") if right else "",
                "notes": "",
            }
            if left and right:
                left_obj = numeric(left, "objective")
                right_obj = numeric(right, "objective")
                left_runtime = numeric(left, "runtime_seconds")
                right_runtime = numeric(right, "runtime_seconds")
                left_test_cvar = numeric(left, "test_empirical_cvar_burned_area")
                right_test_cvar = numeric(right, "test_empirical_cvar_burned_area")
                if left_obj is not None and right_obj is not None:
                    diff = right_obj - left_obj
                    row["objective_diff_right_minus_left"] = diff
                    row["abs_objective_diff"] = abs(diff)
                if left_runtime is not None and right_runtime is not None and left_runtime > 0.0:
                    row["runtime_ratio_right_over_left"] = right_runtime / left_runtime
                if left_test_cvar is not None and right_test_cvar is not None:
                    row["test_cvar_diff_right_minus_left"] = right_test_cvar - left_test_cvar
                row["selected_firebreaks_equal"] = left.get("selected_firebreaks") == right.get("selected_firebreaks")
                row["left_runtime"] = left_runtime
                row["right_runtime"] = right_runtime
                row["left_objective"] = left_obj
                row["right_objective"] = right_obj
                row["left_test_cvar"] = left_test_cvar
                row["right_test_cvar"] = right_test_cvar
                if comparison_type == "exact" and left.get("status") == "Optimal" and right.get("status") == "Optimal":
                    if row["abs_objective_diff"] != "" and row["abs_objective_diff"] > 1.0e-6:
                        row["notes"] = "objective mismatch above 1e-6"
                elif comparison_type == "exact":
                    row["notes"] = "non-optimal exact pair; objective equality not required"
            else:
                row["notes"] = "missing comparison row"
            out.append(row)
    return out


def print_status_summary(rows: list[dict[str, Any]]) -> None:
    print(f"Produced rows: {len(rows)}")
    for method in [m.label for m in METHODS]:
        statuses: dict[str, int] = {}
        for row in rows:
            if row["method"] == method:
                statuses[str(row.get("status", ""))] = statuses.get(str(row.get("status", "")), 0) + 1
        print(f"{method}: {statuses}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-existing", action="store_true", help="Skip runs whose JSON output already exists and parses.")
    parser.add_argument("--only-missing", action="store_true", help="Alias for --skip-existing; still rewrites aggregate CSVs.")
    parser.add_argument("--dry-run", action="store_true", help="Write command manifest without executing commands.")
    parser.add_argument("--time-limit", type=float, default=DEFAULT_TIME_LIMIT, help="Solver time limit in seconds for every run.")
    args = parser.parse_args()

    ensure_dirs()
    ids = available_scenario_ids()
    test_count = REQUESTED_TEST_COUNT
    if TRAIN_COUNT + test_count > len(ids):
        test_count = max(0, len(ids) - TRAIN_COUNT)
        if test_count == 0:
            raise RuntimeError("No feasible Sub20 test scenarios remain after the 100-scenario training set.")
        print(f"Requested test_count={REQUESTED_TEST_COUNT} is infeasible; using test_count={test_count}.")

    rows: list[dict[str, Any]] = []
    command_rows: list[dict[str, Any]] = []
    skip_existing = args.skip_existing or args.only_missing

    for alpha in ALPHAS:
        for case_id in range(NUM_CASES):
            seed = SEED_BASE + case_id
            train_ids, test_ids = generate_split(ids, seed, TRAIN_COUNT, test_count)
            split_prefix = f"alpha_{alpha_token(alpha)}_case_{case_id}_seed_{seed}"
            write_ids(SPLIT_DIR / f"{split_prefix}_train.csv", train_ids)
            write_ids(SPLIT_DIR / f"{split_prefix}_test.csv", test_ids)

            for method in METHODS:
                run_id, json_path, log_path, cmd = command_for(
                    method,
                    alpha,
                    case_id,
                    seed,
                    train_ids,
                    test_ids,
                    test_count,
                    args.time_limit,
                )
                command_status = "planned"
                return_code: int | str = ""
                existing = read_json(json_path)
                if skip_existing and existing is not None:
                    command_status = "skipped_existing"
                    rows.append(
                        row_from_json(
                            method,
                            alpha,
                            TRAIN_COUNT,
                            test_count,
                            case_id,
                            seed,
                            existing,
                            "Loaded from existing JSON via --skip-existing." if args.skip_existing else "",
                        )
                    )
                elif args.dry_run:
                    command_status = "dry_run"
                else:
                    print(f"Running {method.label} alpha={alpha} case={case_id} seed={seed}", flush=True)
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
                    if completed.returncode == 0 and data is not None:
                        command_status = "completed"
                        rows.append(
                            row_from_json(
                                method,
                                alpha,
                                TRAIN_COUNT,
                                test_count,
                                case_id,
                                seed,
                                data,
                            )
                        )
                    else:
                        command_status = "failed"
                        message = f"command failed with return code {completed.returncode}; log={log_path}"
                        if data is not None:
                            rows.append(
                                row_from_json(
                                    method,
                                    alpha,
                                    TRAIN_COUNT,
                                    test_count,
                                    case_id,
                                    seed,
                                    data,
                                    message,
                                )
                            )
                        else:
                            rows.append(
                                failed_row(
                                    method,
                                    alpha,
                                    TRAIN_COUNT,
                                    test_count,
                                    case_id,
                                    seed,
                                    message,
                                )
                            )

                command_rows.append(
                    {
                        "run_id": run_id,
                        "method": method.label,
                        "alpha": alpha,
                        "train_count": TRAIN_COUNT,
                        "test_count": test_count,
                        "case_id": case_id,
                        "seed": seed,
                        "command_status": command_status,
                        "return_code": return_code,
                        "json_path": json_path,
                        "log_path": log_path,
                        "command": " ".join(cmd),
                    }
                )

    write_csv(MAIN_CSV, rows, MAIN_COLUMNS)
    pair_rows = pairwise_rows(rows)
    write_csv(
        PAIRWISE_CSV,
        pair_rows,
        [
            "alpha",
            "train_count",
            "case_id",
            "comparison_type",
            "left_method",
            "right_method",
            "left_status",
            "right_status",
            "objective_diff_right_minus_left",
            "abs_objective_diff",
            "selected_firebreaks_equal",
            "runtime_ratio_right_over_left",
            "left_runtime",
            "right_runtime",
            "left_objective",
            "right_objective",
            "left_test_cvar",
            "right_test_cvar",
            "test_cvar_diff_right_minus_left",
            "right_global_optimality_certified",
            "right_eventually_activated_all",
            "right_final_active_fraction",
            "notes",
        ],
    )
    write_csv(
        SUMMARY_CSV,
        method_summary(rows),
        [
            "method",
            "rows",
            "optimal_rows",
            "feasible_rows",
            "time_limit_rows",
            "failed_rows",
            "mean_runtime",
            "median_runtime",
            "max_runtime",
            "mean_objective",
            "mean_best_bound",
            "mean_gap",
            "mean_expected_component",
            "mean_cvar_component",
            "mean_train_expected_burned_area",
            "mean_test_expected_burned_area",
            "mean_train_empirical_cvar_burned_area",
            "mean_test_empirical_cvar_burned_area",
            "mean_lazy_cuts",
            "mean_candidate_checks",
            "mean_cut_pool_size",
            "mean_final_active_fraction",
            "mean_candidate_rounds",
        ],
    )
    failure_rows = [
        row for row in rows
        if str(row.get("status", "")) == "Failed" or row_is_time_limited(row)
    ]
    write_csv(FAILURES_CSV, failure_rows, MAIN_COLUMNS)
    write_csv(
        COMMAND_MANIFEST_CSV,
        command_rows,
        [
            "run_id",
            "method",
            "alpha",
            "train_count",
            "test_count",
            "case_id",
            "seed",
            "command_status",
            "return_code",
            "json_path",
            "log_path",
            "command",
        ],
    )

    expected_rows = len(ALPHAS) * NUM_CASES * len(METHODS)
    print(f"Expected rows: {expected_rows}")
    print_status_summary(rows)
    print(f"Wrote {MAIN_CSV}")
    print(f"Wrote {PAIRWISE_CSV}")
    print(f"Wrote {SUMMARY_CSV}")
    print(f"Wrote {FAILURES_CSV}")
    print(f"Wrote {COMMAND_MANIFEST_CSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
