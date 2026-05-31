#!/usr/bin/env python3
"""Run one 45-row Sub20 alpha 0.01/0.02 FPP worker manifest."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")
TRAIN_COUNT = 100
TEST_COUNT = 900
SEED_BASE = 20260601


MANIFEST_FIELDS = [
    "task_id",
    "worker_id",
    "case_id",
    "seed_base",
    "seed",
    "landscape",
    "alpha",
    "train_count",
    "test_count",
    "objective_family",
    "method",
    "method_family",
    "fpp_mode",
    "risk_measure",
    "cvar_beta",
    "cvar_lambda",
    "lb_config",
    "use_llbi",
    "use_coverage_llbi",
    "use_path_llbi",
    "use_root_cuts",
    "is_combinatorial",
    "combinatorial_benders_lift",
    "combinatorial_benders_cut_sampling_ratio",
    "combinatorial_benders_separate_fractional",
    "combinatorial_benders_initial_cuts",
    "path_llbi_max_paths_per_node",
    "time_limit",
    "mip_gap",
    "threads",
    "split_dir",
    "output_dir",
    "output_csv",
    "output_json",
    "solution_dir",
    "run_id",
]

WORKER_FIELDS = [
    "worker_status",
    "worker_return_code",
    "worker_started_at_epoch",
    "worker_finished_at_epoch",
    "worker_runtime_seconds",
    "worker_command",
    "worker_log",
    "solver_method",
    "configured_mip_gap",
    "solver_mip_gap",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker-id", required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--binary", type=Path, default=None)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def require_project_root() -> None:
    if not Path("src/main.cpp").exists() or not Path("include").is_dir():
        raise SystemExit("Run this worker from cpp_cplex_firebreak/.")


def bool_value(value: str) -> bool:
    return value.strip().lower() in {"1", "true", "yes", "on"}


def case_index(case_id: str) -> int:
    match = re.fullmatch(r"case(\d+)", case_id)
    if not match:
        raise RuntimeError(f"Invalid case_id in manifest: {case_id}")
    return int(match.group(1))


def read_ids(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    return [int(token) for token in re.split(r"[\s,;]+", text.strip()) if token]


def cxx_split_paths(split_dir: Path, case: int, seed: int) -> tuple[Path, Path]:
    prefix = f"Sub20_seed{seed}_train{TRAIN_COUNT}_test{TEST_COUNT}_case{case}"
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def case_pattern(case: int) -> re.Pattern[str]:
    return re.compile(rf"(^|[^0-9])case0?{case}([^0-9]|$)")


def active_split_dir(results_dir: Path, manifest_split_dir: str) -> Path:
    config_path = results_dir / "splits" / "shared_split_config.json"
    if config_path.exists():
        payload = json.loads(config_path.read_text(encoding="utf-8"))
        active = payload.get("active_split_dir")
        if active:
            return Path(active)
    return Path(manifest_split_dir)


def find_split_pair(split_dir: Path, case: int, seed: int) -> tuple[Path, Path]:
    exact_train, exact_test = cxx_split_paths(split_dir, case, seed)
    if exact_train.exists() and exact_test.exists():
        return exact_train, exact_test

    pattern = case_pattern(case)
    train_candidates = sorted(
        path for path in split_dir.iterdir()
        if path.is_file()
        and pattern.search(path.name)
        and re.search(r"_train(_ids)?\.(csv|txt)$", path.name)
    )
    for train_path in train_candidates:
        test_names = [
            train_path.name.replace("_train_ids.", "_test_ids."),
            train_path.name.replace("_train.", "_test."),
        ]
        for test_name in test_names:
            test_path = train_path.with_name(test_name)
            if test_path.exists():
                return train_path, test_path
    raise RuntimeError(f"No split pair found for case{case:02d} under {split_dir}")


def validate_split(train_ids: list[int], test_ids: list[int]) -> None:
    if len(train_ids) != TRAIN_COUNT:
        raise RuntimeError(f"Train split has {len(train_ids)} IDs; expected {TRAIN_COUNT}.")
    if len(test_ids) != TEST_COUNT:
        raise RuntimeError(f"Test split has {len(test_ids)} IDs; expected {TEST_COUNT}.")
    if len(set(train_ids)) != len(train_ids):
        raise RuntimeError("Train split contains duplicates.")
    if len(set(test_ids)) != len(test_ids):
        raise RuntimeError("Test split contains duplicates.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Train and test split IDs overlap.")


def ids_arg(ids: list[int]) -> str:
    return ",".join(str(value) for value in ids)


def find_binary(explicit: Path | None) -> Path:
    candidates = []
    if explicit is not None:
        candidates.append(explicit)
    env_bin = os.environ.get("FIREBREAK_BIN")
    if env_bin:
        candidates.append(Path(env_bin))
    candidates.extend([
        Path("build_gpp/firebreak_cpp"),
        Path("build/firebreak_cpp"),
        Path("firebreak_cpp"),
    ])
    for path in candidates:
        if path.exists() and os.access(path, os.X_OK):
            return path
    raise RuntimeError(
        "Could not find firebreak_cpp binary. Build first or set FIREBREAK_BIN.")


def load_manifest(path: Path, worker_id: str) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    rows = [row for row in rows if row.get("worker_id") == worker_id]
    if len(rows) != 45:
        raise RuntimeError(f"{path} has {len(rows)} rows for {worker_id}; expected 45.")
    return rows


def load_existing(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def row_complete(row: dict[str, str]) -> bool:
    return row.get("worker_return_code") == "0" and bool(row.get("solver_status") or row.get("status"))


def write_worker_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    seen: list[str] = []
    for field in MANIFEST_FIELDS + WORKER_FIELDS:
        if field not in seen:
            seen.append(field)
    for row in rows:
        for field in row:
            if field not in seen:
                seen.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=seen, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def risk_args(row: dict[str, str]) -> list[str]:
    args = [
        "--risk-measure", row["risk_measure"],
        "--cvar-beta", row["cvar_beta"],
        "--cvar-lambda", row["cvar_lambda"],
    ]
    return args


def build_command(
    binary: Path,
    row: dict[str, str],
    train_ids: list[int],
    test_ids: list[int],
    temp_csv: Path,
) -> list[str]:
    method_family = row["method_family"]
    command = "run-fpp-saa-oos" if method_family == "FPP-SAA" else "run-fpp-branch-benders-oos"
    solution_dir = Path(row["solution_dir"])
    solution_json = solution_dir / f"{row['task_id']}.json"
    solution_csv = solution_dir / f"{row['task_id']}.csv"
    args = [
        str(binary),
        command,
        "--landscape", row["landscape"],
        "--forest-path", "../sample_test/data/CanadianFBP/Sub20",
        "--results-path", "../sample_test/Sub20",
        "--train-ids", ids_arg(train_ids),
        "--test-ids", ids_arg(test_ids),
        "--alpha", row["alpha"],
        "--time-limit", row["time_limit"],
        "--mip-gap", row["mip_gap"],
        "--threads", row["threads"],
        "--run-id", row["run_id"],
        "--output-json", row["output_json"],
        "--output-csv", str(temp_csv),
        "--solution-json", str(solution_json),
        "--solution-csv", str(solution_csv),
    ]
    args.extend(risk_args(row))

    if method_family == "FPP-SAA":
        formulation = "cut" if row["fpp_mode"] == "fpp_cut" else "base"
        args.extend(["--fpp-formulation", formulation])
        return args

    if bool_value(row["use_llbi"]):
        args.append("--use-lifted-lower-bounds")
    if bool_value(row["use_root_cuts"]):
        args.extend(["--use-root-user-cuts", "--root-user-cut-max-rounds", "1"])
    if bool_value(row["use_coverage_llbi"]):
        args.append("--use-coverage-llbi")
    if bool_value(row["use_path_llbi"]):
        args.extend([
            "--use-path-llbi",
            "--path-llbi-max-paths-per-node",
            row.get("path_llbi_max_paths_per_node") or "8",
        ])
    if bool_value(row["is_combinatorial"]):
        args.append("--use-combinatorial-benders")
        args.extend([
            "--combinatorial-benders-lift",
            row["combinatorial_benders_lift"] or "heuristic",
            "--combinatorial-benders-cut-sampling-ratio",
            row["combinatorial_benders_cut_sampling_ratio"] or "0.10",
            "--combinatorial-benders-separate-fractional",
            row["combinatorial_benders_separate_fractional"] or "true",
            "--combinatorial-benders-initial-cuts",
            row["combinatorial_benders_initial_cuts"] or "true",
        ])
    return args


def read_single_solver_row(path: Path) -> dict[str, str]:
    if not path.exists():
        raise RuntimeError(f"Solver did not write expected row CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != 1:
        raise RuntimeError(f"{path} contains {len(rows)} data rows; expected exactly 1.")
    return rows[0]


def merged_result(
    manifest_row: dict[str, str],
    solver_row: dict[str, str],
    *,
    train_ids: list[int],
    test_ids: list[int],
    command: list[str],
    log_path: Path,
    return_code: int,
    started: float,
    finished: float,
) -> dict[str, str]:
    out = dict(solver_row)
    solver_mip_gap = solver_row.get("mip_gap", "")
    out["solver_method"] = solver_row.get("method", "")
    out.update(manifest_row)
    out["configured_mip_gap"] = manifest_row.get("mip_gap", "")
    out["solver_mip_gap"] = solver_mip_gap
    if solver_mip_gap:
        out["mip_gap"] = solver_mip_gap
    out["train_ids"] = ids_arg(train_ids)
    out["test_ids"] = ids_arg(test_ids)
    out["train_scenario_count"] = str(len(train_ids))
    out["test_scenario_count"] = str(len(test_ids))
    out["worker_status"] = "ok" if return_code == 0 else "failed"
    out["worker_return_code"] = str(return_code)
    out["worker_started_at_epoch"] = f"{started:.6f}"
    out["worker_finished_at_epoch"] = f"{finished:.6f}"
    out["worker_runtime_seconds"] = f"{finished - started:.6f}"
    out["worker_command"] = " ".join(shlex.quote(part) for part in command)
    out["worker_log"] = str(log_path)
    return out


def main() -> int:
    args = parse_args()
    require_project_root()
    binary = find_binary(args.binary)
    manifest_rows = load_manifest(args.manifest, args.worker_id)
    output_csv = Path(manifest_rows[0]["output_csv"])
    output_dir = Path(manifest_rows[0]["output_dir"])
    log_dir = output_dir / "logs"
    temp_dir = log_dir / "row_csv"
    log_dir.mkdir(parents=True, exist_ok=True)
    temp_dir.mkdir(parents=True, exist_ok=True)
    Path(manifest_rows[0]["solution_dir"]).mkdir(parents=True, exist_ok=True)
    (output_dir / "json").mkdir(parents=True, exist_ok=True)

    existing = load_existing(output_csv)
    by_task = {row.get("task_id", ""): row for row in existing if row.get("task_id")}
    final_rows = [row for row in existing if row.get("task_id")]

    for row in manifest_rows:
        task_id = row["task_id"]
        if task_id in by_task and row_complete(by_task[task_id]):
            print(f"SKIP complete {task_id}: {row['method']} {row['lb_config']}")
            continue

        split_dir = active_split_dir(args.results_dir, row["split_dir"])
        train_path, test_path = find_split_pair(split_dir, case_index(row["case_id"]), int(row["seed"]))
        train_ids = read_ids(train_path)
        test_ids = read_ids(test_path)
        validate_split(train_ids, test_ids)

        temp_csv = temp_dir / f"{task_id}.csv"
        if temp_csv.exists():
            temp_csv.unlink()
        log_path = log_dir / f"{task_id}.log"
        command = build_command(binary, row, train_ids, test_ids, temp_csv)
        print(f"START {task_id}: {row['method']} {row['fpp_mode']} {row['lb_config']}")
        print("  " + " ".join(shlex.quote(part) for part in command))
        if args.dry_run:
            continue

        started = time.time()
        with log_path.open("w", encoding="utf-8") as log:
            log.write("COMMAND: " + " ".join(shlex.quote(part) for part in command) + "\n")
            log.flush()
            completed = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        finished = time.time()
        if completed.returncode != 0:
            failed_row = merged_result(
                row,
                {},
                train_ids=train_ids,
                test_ids=test_ids,
                command=command,
                log_path=log_path,
                return_code=completed.returncode,
                started=started,
                finished=finished,
            )
            by_task[task_id] = failed_row
            final_rows = [r for r in final_rows if r.get("task_id") != task_id] + [failed_row]
            write_worker_csv(output_csv, final_rows)
            print(f"FAILED {task_id}; see {log_path}", file=sys.stderr)
            return completed.returncode

        solver_row = read_single_solver_row(temp_csv)
        completed_row = merged_result(
            row,
            solver_row,
            train_ids=train_ids,
            test_ids=test_ids,
            command=command,
            log_path=log_path,
            return_code=0,
            started=started,
            finished=finished,
        )
        by_task[task_id] = completed_row
        final_rows = [r for r in final_rows if r.get("task_id") != task_id] + [completed_row]
        final_rows.sort(key=lambda r: r.get("task_id", ""))
        write_worker_csv(output_csv, final_rows)
        print(f"END {task_id}: {finished - started:.3f}s")

    if args.dry_run:
        print("Dry run complete; no solver commands were executed.")
        return 0
    completed_count = sum(1 for row in final_rows if row_complete(row))
    if completed_count != 45:
        print(f"{args.worker_id} has {completed_count} complete rows; expected 45.", file=sys.stderr)
        return 1
    print(f"{args.worker_id} complete: 45 rows written to {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
