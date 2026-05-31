#!/usr/bin/env python3
"""Run one 24-row Sub20 projected LLBI integral worker manifest."""

from __future__ import annotations

import argparse
import csv
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_RESULTS_DIR = Path("results/batch/sub20_projected_llbi_integral_alpha001_002_tl300")
TRAIN_COUNT = 100
TEST_COUNT = 900
EXPECTED_ROWS_PER_WORKER = 24

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
    "projected_variant",
    "projected_llbi_strategy",
    "risk_measure",
    "cvar_beta",
    "cvar_lambda",
    "use_root_cuts",
    "use_projected_coverage_llbi_poly",
    "use_projected_path_llbi_poly",
    "use_projected_coverage_llbi_exp",
    "use_projected_path_llbi_exp",
    "projected_llbi_root_rounds",
    "projected_llbi_max_cuts_per_round",
    "projected_llbi_violation_tolerance",
    "projected_llbi_cut_density_limit",
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
    return [int(token) for token in re.split(r"[\s,;]+", path.read_text(encoding="utf-8").strip()) if token]


def split_pair(split_dir: Path, case: int, seed: int) -> tuple[Path, Path]:
    prefix = f"Sub20_seed{seed}_train{TRAIN_COUNT}_test{TEST_COUNT}_case{case}"
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def validate_split(train_path: Path, test_path: Path) -> tuple[list[int], list[int]]:
    if not train_path.exists() or not test_path.exists():
        raise RuntimeError(f"Missing split pair: {train_path}, {test_path}")
    train_ids = read_ids(train_path)
    test_ids = read_ids(test_path)
    if len(train_ids) != TRAIN_COUNT:
        raise RuntimeError(f"{train_path} has {len(train_ids)} IDs; expected {TRAIN_COUNT}.")
    if len(test_ids) != TEST_COUNT:
        raise RuntimeError(f"{test_path} has {len(test_ids)} IDs; expected {TEST_COUNT}.")
    if len(set(train_ids)) != TRAIN_COUNT or len(set(test_ids)) != TEST_COUNT:
        raise RuntimeError("Split contains duplicate IDs.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Train and test split IDs overlap.")
    return train_ids, test_ids


def ids_arg(ids: list[int]) -> str:
    return ",".join(str(value) for value in ids)


def find_binary(explicit: Path | None) -> Path:
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)
    if os.environ.get("FIREBREAK_BIN"):
        candidates.append(Path(os.environ["FIREBREAK_BIN"]))
    candidates.extend([Path("build_gpp/firebreak_cpp"), Path("build/firebreak_cpp"), Path("firebreak_cpp")])
    for path in candidates:
        if path.exists() and os.access(path, os.X_OK):
            return path
    raise RuntimeError("Could not find firebreak_cpp binary. Build first or set FIREBREAK_BIN.")


def load_manifest(path: Path, worker_id: str) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        rows = [row for row in csv.DictReader(inp) if row.get("worker_id") == worker_id]
    if len(rows) != EXPECTED_ROWS_PER_WORKER:
        raise RuntimeError(f"{path} has {len(rows)} rows for {worker_id}; expected {EXPECTED_ROWS_PER_WORKER}.")
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
    fields: list[str] = []
    for field in MANIFEST_FIELDS + WORKER_FIELDS:
        if field not in fields:
            fields.append(field)
    for row in rows:
        for field in row:
            if field not in fields:
                fields.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def build_command(binary: Path, row: dict[str, str], train_ids: list[int], test_ids: list[int], temp_csv: Path) -> list[str]:
    solution_dir = Path(row["solution_dir"])
    solution_json = solution_dir / f"{row['task_id']}.json"
    solution_csv = solution_dir / f"{row['task_id']}.csv"
    args = [
        str(binary),
        "run-fpp-branch-benders-oos",
        "--landscape", row["landscape"],
        "--forest-path", "../sample_test/data/CanadianFBP/Sub20",
        "--results-path", "../sample_test/Sub20",
        "--train-ids", ids_arg(train_ids),
        "--test-ids", ids_arg(test_ids),
        "--alpha", row["alpha"],
        "--time-limit", row["time_limit"],
        "--mip-gap", row["mip_gap"],
        "--threads", row["threads"],
        "--risk-measure", row["risk_measure"],
        "--cvar-beta", row["cvar_beta"],
        "--cvar-lambda", row["cvar_lambda"],
        "--run-id", row["run_id"],
        "--output-json", row["output_json"],
        "--output-csv", str(temp_csv),
        "--solution-json", str(solution_json),
        "--solution-csv", str(solution_csv),
    ]

    if bool_value(row["use_root_cuts"]):
        args.extend(["--use-root-user-cuts", "--root-user-cut-max-rounds", "1"])
    if bool_value(row["use_projected_coverage_llbi_poly"]):
        args.append("--use-projected-coverage-llbi-poly")
    if bool_value(row["use_projected_path_llbi_poly"]):
        args.append("--use-projected-path-llbi-poly")
    if bool_value(row["use_projected_coverage_llbi_exp"]):
        args.append("--use-projected-coverage-llbi-exp")
    if bool_value(row["use_projected_path_llbi_exp"]):
        args.append("--use-projected-path-llbi-exp")

    if row["projected_llbi_strategy"] == "exp":
        args.extend([
            "--projected-llbi-root-rounds", row["projected_llbi_root_rounds"],
            "--projected-llbi-max-cuts-per-round", row["projected_llbi_max_cuts_per_round"],
            "--projected-llbi-violation-tolerance", row["projected_llbi_violation_tolerance"],
            "--projected-llbi-cut-density-limit", row["projected_llbi_cut_density_limit"],
        ])
    elif row["projected_llbi_cut_density_limit"] not in {"", "0", "0.0"}:
        args.extend(["--projected-llbi-cut-density-limit", row["projected_llbi_cut_density_limit"]])
    return args


def read_single_solver_row(path: Path) -> dict[str, str]:
    if not path.exists():
        raise RuntimeError(f"Solver did not write expected row CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != 1:
        raise RuntimeError(f"{path} contains {len(rows)} rows; expected exactly 1.")
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
            print(f"SKIP complete {task_id}: {row['method']}")
            continue

        split_dir = Path(row["split_dir"])
        train_path, test_path = split_pair(split_dir, case_index(row["case_id"]), int(row["seed"]))
        train_ids, test_ids = validate_split(train_path, test_path)

        temp_csv = temp_dir / f"{task_id}.csv"
        if temp_csv.exists():
            temp_csv.unlink()
        log_path = log_dir / f"{task_id}.log"
        command = build_command(binary, row, train_ids, test_ids, temp_csv)
        print(f"START {task_id}: {row['method']}")
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
                row, {}, train_ids=train_ids, test_ids=test_ids, command=command,
                log_path=log_path, return_code=completed.returncode, started=started, finished=finished)
            by_task[task_id] = failed_row
            final_rows = [r for r in final_rows if r.get("task_id") != task_id] + [failed_row]
            write_worker_csv(output_csv, final_rows)
            print(f"FAILED {task_id}; see {log_path}", file=sys.stderr)
            return completed.returncode

        solver_row = read_single_solver_row(temp_csv)
        completed_row = merged_result(
            row, solver_row, train_ids=train_ids, test_ids=test_ids, command=command,
            log_path=log_path, return_code=0, started=started, finished=finished)
        by_task[task_id] = completed_row
        final_rows = [r for r in final_rows if r.get("task_id") != task_id] + [completed_row]
        final_rows.sort(key=lambda r: r.get("task_id", ""))
        write_worker_csv(output_csv, final_rows)
        print(f"END {task_id}: {finished - started:.3f}s")

    if args.dry_run:
        print("Dry run complete; no solver commands were executed.")
        return 0
    completed_count = sum(1 for row in final_rows if row_complete(row))
    if completed_count != EXPECTED_ROWS_PER_WORKER:
        print(f"{args.worker_id} has {completed_count} complete rows; expected {EXPECTED_ROWS_PER_WORKER}.", file=sys.stderr)
        return 1
    print(f"{args.worker_id} complete: {EXPECTED_ROWS_PER_WORKER} rows written to {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
