#!/usr/bin/env python3
"""Generate manifests for the Sub20 projected LLBI integral experiment."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


LANDSCAPE = "Sub20"
DEFAULT_RESULTS_DIR = Path("results/batch/sub20_projected_llbi_integral_alpha001_002_tl300")
PREFERRED_SPLIT_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300/splits")
OLDER_SPLIT_DIR = Path("results/batch/sub20_100train_900test_5cases_alpha002_003_tl300/splits")
TRAIN_COUNT = 100
TEST_COUNT = 900
ALPHAS = ["0.01", "0.02"]
SEED_BASE = 20260601
TIME_LIMIT = 300
MIP_GAP = "0.001"
THREADS = 1
CVAR_BETA = "0.9"
MEAN_CVAR_LAMBDA = "0.5"
ROOT_ROUNDS_EXP = 10
MAX_CUTS_PER_ROUND = 100
VIOLATION_TOLERANCE = "1e-6"
CUT_DENSITY_LIMIT = 0


FIELDS = [
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


OBJECTIVES = [
    ("Expected", "expected", "", "1.0"),
    ("CVaR", "cvar", "-CVaR", "1.0"),
    ("MeanCVaR", "mean-cvar", "-MeanCVaR", MEAN_CVAR_LAMBDA),
]

PROJECTED_VARIANTS = [
    ("ProjectedCoverageLLBI-poly", "poly", True, False, False, False),
    ("ProjectedPathLLBI-poly", "poly", False, True, False, False),
    ("ProjectedCoverageLLBI-exp", "exp", False, False, True, False),
    ("ProjectedPathLLBI-exp", "exp", False, False, False, True),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--split-dir", type=Path, default=None)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--verify-splits-only", action="store_true")
    return parser.parse_args()


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def worker_id(index: int) -> str:
    return f"worker_{index:02d}"


def case_id(index: int) -> str:
    return f"case{index:02d}"


def alpha_slug(alpha: str) -> str:
    return alpha.replace(".", "")


def slug(value: str) -> str:
    out = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()
    return out or "row"


def read_ids(path: Path) -> list[int]:
    if not path.exists():
        raise RuntimeError(f"Missing split file: {path}")
    return [int(token) for token in re.split(r"[\s,;]+", path.read_text(encoding="utf-8").strip()) if token]


def split_pair(split_dir: Path, case_index: int, seed: int) -> tuple[Path, Path]:
    prefix = f"Sub20_seed{seed}_train{TRAIN_COUNT}_test{TEST_COUNT}_case{case_index}"
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def find_split_dir(explicit: Path | None) -> Path:
    candidates = [explicit] if explicit is not None else [PREFERRED_SPLIT_DIR, OLDER_SPLIT_DIR]
    for candidate in candidates:
        if candidate is not None and candidate.exists() and candidate.is_dir():
            validate_split_dir(candidate)
            return candidate
    raise RuntimeError(
        "No compatible split directory found. Expected one of: "
        f"{PREFERRED_SPLIT_DIR} or {OLDER_SPLIT_DIR}. This experiment must reuse previous splits.")


def validate_split_dir(split_dir: Path) -> None:
    for case_index in range(5):
        seed = SEED_BASE + case_index
        train_path, test_path = split_pair(split_dir, case_index, seed)
        train_ids = read_ids(train_path)
        test_ids = read_ids(test_path)
        if len(train_ids) != TRAIN_COUNT:
            raise RuntimeError(f"{train_path} has {len(train_ids)} IDs; expected {TRAIN_COUNT}.")
        if len(test_ids) != TEST_COUNT:
            raise RuntimeError(f"{test_path} has {len(test_ids)} IDs; expected {TEST_COUNT}.")
        if len(set(train_ids)) != TRAIN_COUNT:
            raise RuntimeError(f"{train_path} contains duplicate IDs.")
        if len(set(test_ids)) != TEST_COUNT:
            raise RuntimeError(f"{test_path} contains duplicate IDs.")
        if set(train_ids).intersection(test_ids):
            raise RuntimeError(f"{train_path} and {test_path} overlap.")


def row_for_task(
    *,
    task_index: int,
    worker: str,
    case: str,
    seed: int,
    alpha: str,
    objective_family: str,
    risk_measure: str,
    risk_suffix: str,
    cvar_lambda: str,
    projected_variant: str,
    projected_strategy: str,
    root_cuts: bool,
    flags: tuple[bool, bool, bool, bool],
    split_dir: Path,
    results_dir: Path,
) -> dict[str, str]:
    output_dir = results_dir / worker
    task_id = f"{worker}_task_{task_index:03d}"
    method = f"FPP-Branch-Benders{risk_suffix}-{projected_variant}"
    if root_cuts:
        method += "-RootCuts"
    root_rounds = ROOT_ROUNDS_EXP if projected_strategy == "exp" else 0
    run_id = (
        f"sub20_{case}_alpha{alpha_slug(alpha)}_"
        f"{slug(method)}"
    )
    use_cov_poly, use_path_poly, use_cov_exp, use_path_exp = flags
    return {
        "task_id": task_id,
        "worker_id": worker,
        "case_id": case,
        "seed_base": str(SEED_BASE),
        "seed": str(seed),
        "landscape": LANDSCAPE,
        "alpha": alpha,
        "train_count": str(TRAIN_COUNT),
        "test_count": str(TEST_COUNT),
        "objective_family": objective_family,
        "method": method,
        "method_family": "FPP-Branch-Benders",
        "projected_variant": projected_variant,
        "projected_llbi_strategy": projected_strategy,
        "risk_measure": risk_measure,
        "cvar_beta": CVAR_BETA,
        "cvar_lambda": cvar_lambda,
        "use_root_cuts": bool_text(root_cuts),
        "use_projected_coverage_llbi_poly": bool_text(use_cov_poly),
        "use_projected_path_llbi_poly": bool_text(use_path_poly),
        "use_projected_coverage_llbi_exp": bool_text(use_cov_exp),
        "use_projected_path_llbi_exp": bool_text(use_path_exp),
        "projected_llbi_root_rounds": str(root_rounds),
        "projected_llbi_max_cuts_per_round": str(MAX_CUTS_PER_ROUND),
        "projected_llbi_violation_tolerance": VIOLATION_TOLERANCE,
        "projected_llbi_cut_density_limit": str(CUT_DENSITY_LIMIT),
        "time_limit": str(TIME_LIMIT),
        "mip_gap": MIP_GAP,
        "threads": str(THREADS),
        "split_dir": str(split_dir),
        "output_dir": str(output_dir),
        "output_csv": str(output_dir / f"batch_results_{worker}.csv"),
        "output_json": str(output_dir / "json" / f"{task_id}.json"),
        "solution_dir": str(output_dir / "solutions"),
        "run_id": run_id,
    }


def rows_for_worker(worker: str, case: str, seed: int, alpha: str, split_dir: Path, results_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    task_index = 0
    for objective_family, risk_measure, risk_suffix, cvar_lambda in OBJECTIVES:
        for projected_variant, strategy, use_cov_poly, use_path_poly, use_cov_exp, use_path_exp in PROJECTED_VARIANTS:
            for root_cuts in (False, True):
                rows.append(row_for_task(
                    task_index=task_index,
                    worker=worker,
                    case=case,
                    seed=seed,
                    alpha=alpha,
                    objective_family=objective_family,
                    risk_measure=risk_measure,
                    risk_suffix=risk_suffix,
                    cvar_lambda=cvar_lambda,
                    projected_variant=projected_variant,
                    projected_strategy=strategy,
                    root_cuts=root_cuts,
                    flags=(use_cov_poly, use_path_poly, use_cov_exp, use_path_exp),
                    split_dir=split_dir,
                    results_dir=results_dir,
                ))
                task_index += 1
    if len(rows) != 24:
        raise RuntimeError(f"{worker} generated {len(rows)} rows; expected 24.")
    return rows


def build_rows(results_dir: Path, split_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    worker_index = 0
    for case_index in range(5):
        seed = SEED_BASE + case_index
        case = case_id(case_index)
        for alpha in ALPHAS:
            rows.extend(rows_for_worker(worker_id(worker_index), case, seed, alpha, split_dir, results_dir))
            worker_index += 1
    if len(rows) != 240:
        raise RuntimeError(f"Generated {len(rows)} rows; expected 240.")
    return rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def verify_csv(path: Path, expected_rows: int) -> None:
    if not path.exists():
        raise RuntimeError(f"Missing manifest: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != expected_rows:
        raise RuntimeError(f"{path} has {len(rows)} rows; expected {expected_rows}.")


def main() -> int:
    args = parse_args()
    split_dir = find_split_dir(args.split_dir)
    if args.verify_splits_only:
        print(f"Verified reusable split directory: {split_dir}")
        return 0

    manifest_dir = args.results_dir / "manifests"
    full_path = manifest_dir / "full_task_manifest.csv"
    if args.verify_only:
        verify_csv(full_path, 240)
        for index in range(10):
            verify_csv(manifest_dir / f"{worker_id(index)}_manifest.csv", 24)
        print(f"Verified manifests under {manifest_dir}")
        return 0

    rows = build_rows(args.results_dir, split_dir)
    write_csv(full_path, rows)
    for index in range(10):
        wid = worker_id(index)
        worker_rows = [row for row in rows if row["worker_id"] == wid]
        write_csv(manifest_dir / f"{wid}_manifest.csv", worker_rows)
    print(f"Wrote {full_path} with {len(rows)} rows.")
    print("Wrote 10 worker manifests with 24 rows each.")
    print(f"Using split directory: {split_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
