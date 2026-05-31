#!/usr/bin/env python3
"""Generate the controlled Sub20 alpha 0.01/0.02 FPP exact-study manifests."""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


LANDSCAPE = "Sub20"
FOREST_PATH = "../sample_test/data/CanadianFBP/Sub20"
RESULTS_PATH = "../sample_test/Sub20"
DEFAULT_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")
PREVIOUS_SPLIT_DIR = Path("results/batch/sub20_100train_900test_5cases_alpha002_003_tl300/splits")
TRAIN_COUNT = 100
TEST_COUNT = 900
ALPHAS = ["0.01", "0.02"]
SEED_BASE = 20260601
TIME_LIMIT = 300
MIP_GAP = "0.001"
THREADS = 1
CVAR_BETA = "0.9"
MEAN_CVAR_LAMBDA = "0.5"
PATH_LLBI_MAX_PATHS_PER_NODE = 8


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


OBJECTIVES = [
    ("Expected", "expected", "", "1.0"),
    ("CVaR", "cvar", "-CVaR", "1.0"),
    ("MeanCVaR", "mean-cvar", "-MeanCVaR", MEAN_CVAR_LAMBDA),
]


LB_CONFIGS = [
    ("None", "", False, False, False),
    ("LLBI", "-LLBI", True, False, False),
    ("CoverageLLBI", "-CoverageLLBI", False, True, False),
    ("PathLLBI", "-PathLLBI", False, False, True),
    ("CoverageLLBI+PathLLBI", "-CoverageLLBI-PathLLBI", False, True, True),
    ("LLBI+CoverageLLBI+PathLLBI", "-LLBI-CoverageLLBI-PathLLBI", True, True, True),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--split-dir", type=Path, default=None)
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def case_id(case_index: int) -> str:
    return f"case{case_index:02d}"


def worker_id(worker_index: int) -> str:
    return f"worker_{worker_index:02d}"


def alpha_slug(alpha: str) -> str:
    return alpha.replace(".", "")


def slug(value: str) -> str:
    out = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()
    return out or "row"


def load_active_split_dir(results_dir: Path, explicit_split_dir: Path | None) -> Path:
    if explicit_split_dir is not None:
        return explicit_split_dir
    config_path = results_dir / "splits" / "shared_split_config.json"
    if config_path.exists():
        payload = json.loads(config_path.read_text(encoding="utf-8"))
        active = payload.get("active_split_dir")
        if active:
            return Path(active)
    if PREVIOUS_SPLIT_DIR.exists():
        return PREVIOUS_SPLIT_DIR
    return results_dir / "splits"


def base_row(
    *,
    task_index: int,
    worker: str,
    case: str,
    seed: int,
    alpha: str,
    objective_family: str,
    method: str,
    method_family: str,
    fpp_mode: str,
    risk_measure: str,
    cvar_lambda: str,
    lb_config: str,
    use_llbi: bool,
    use_coverage_llbi: bool,
    use_path_llbi: bool,
    use_root_cuts: bool,
    is_combinatorial: bool,
    split_dir: Path,
    results_dir: Path,
) -> dict[str, str]:
    output_dir = results_dir / worker
    task_id = f"{worker}_task_{task_index:03d}"
    run_id = (
        f"sub20_{case}_alpha{alpha_slug(alpha)}_"
        f"{slug(method)}_{slug(fpp_mode or 'na')}_{slug(lb_config)}"
    )
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
        "method_family": method_family,
        "fpp_mode": fpp_mode,
        "risk_measure": risk_measure,
        "cvar_beta": CVAR_BETA,
        "cvar_lambda": cvar_lambda,
        "lb_config": lb_config,
        "use_llbi": bool_text(use_llbi),
        "use_coverage_llbi": bool_text(use_coverage_llbi),
        "use_path_llbi": bool_text(use_path_llbi),
        "use_root_cuts": bool_text(use_root_cuts),
        "is_combinatorial": bool_text(is_combinatorial),
        "combinatorial_benders_lift": "heuristic" if is_combinatorial else "",
        "combinatorial_benders_cut_sampling_ratio": "0.10" if is_combinatorial else "",
        "combinatorial_benders_separate_fractional": "true" if is_combinatorial else "",
        "combinatorial_benders_initial_cuts": "true" if is_combinatorial else "",
        "path_llbi_max_paths_per_node": str(PATH_LLBI_MAX_PATHS_PER_NODE),
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


def rows_for_block(
    *,
    worker: str,
    case: str,
    seed: int,
    alpha: str,
    split_dir: Path,
    results_dir: Path,
) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    task_index = 0

    for objective_family, risk_measure, risk_suffix, cvar_lambda in OBJECTIVES:
        method = "FPP-SAA" + risk_suffix
        for fpp_mode in ("fpp_base", "fpp_cut"):
            rows.append(base_row(
                task_index=task_index,
                worker=worker,
                case=case,
                seed=seed,
                alpha=alpha,
                objective_family=objective_family,
                method=method,
                method_family="FPP-SAA",
                fpp_mode=fpp_mode,
                risk_measure=risk_measure,
                cvar_lambda=cvar_lambda,
                lb_config="None",
                use_llbi=False,
                use_coverage_llbi=False,
                use_path_llbi=False,
                use_root_cuts=False,
                is_combinatorial=False,
                split_dir=split_dir,
                results_dir=results_dir,
            ))
            task_index += 1

    for objective_family, risk_measure, risk_suffix, cvar_lambda in OBJECTIVES:
        prefix = "FPP-Branch-Benders" + risk_suffix
        for lb_config, lb_suffix, use_llbi, use_coverage, use_path in LB_CONFIGS:
            for use_root in (False, True):
                method = prefix + lb_suffix + ("-RootCuts" if use_root else "")
                rows.append(base_row(
                    task_index=task_index,
                    worker=worker,
                    case=case,
                    seed=seed,
                    alpha=alpha,
                    objective_family=objective_family,
                    method=method,
                    method_family="FPP-Branch-Benders",
                    fpp_mode="",
                    risk_measure=risk_measure,
                    cvar_lambda=cvar_lambda,
                    lb_config=lb_config,
                    use_llbi=use_llbi,
                    use_coverage_llbi=use_coverage,
                    use_path_llbi=use_path,
                    use_root_cuts=use_root,
                    is_combinatorial=False,
                    split_dir=split_dir,
                    results_dir=results_dir,
                ))
                task_index += 1

    for objective_family, risk_measure, risk_suffix, cvar_lambda in OBJECTIVES:
        method = "FPP-Branch-Benders-Combinatorial" + risk_suffix
        rows.append(base_row(
            task_index=task_index,
            worker=worker,
            case=case,
            seed=seed,
            alpha=alpha,
            objective_family=objective_family,
            method=method,
            method_family="FPP-Branch-Benders-Combinatorial",
            fpp_mode="",
            risk_measure=risk_measure,
            cvar_lambda=cvar_lambda,
            lb_config="Combinatorial",
            use_llbi=False,
            use_coverage_llbi=False,
            use_path_llbi=False,
            use_root_cuts=False,
            is_combinatorial=True,
            split_dir=split_dir,
            results_dir=results_dir,
        ))
        task_index += 1

    if len(rows) != 45:
        raise RuntimeError(f"{worker} generated {len(rows)} rows; expected 45.")
    return rows


def build_rows(results_dir: Path, split_dir: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    worker_index = 0
    for case_index in range(5):
        seed = SEED_BASE + case_index
        for alpha in ALPHAS:
            worker = worker_id(worker_index)
            rows.extend(rows_for_block(
                worker=worker,
                case=case_id(case_index),
                seed=seed,
                alpha=alpha,
                split_dir=split_dir,
                results_dir=results_dir,
            ))
            worker_index += 1
    if len(rows) != 450:
        raise RuntimeError(f"Generated {len(rows)} rows; expected 450.")
    return rows


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def verify_existing(path: Path, expected_rows: int) -> None:
    if not path.exists():
        raise RuntimeError(f"Missing manifest: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != expected_rows:
        raise RuntimeError(f"{path} has {len(rows)} rows; expected {expected_rows}.")


def main() -> int:
    args = parse_args()
    results_dir = args.results_dir
    split_dir = load_active_split_dir(results_dir, args.split_dir)
    manifest_dir = results_dir / "manifests"
    full_path = manifest_dir / "full_task_manifest.csv"

    if args.verify_only:
        verify_existing(full_path, 450)
        for index in range(10):
            verify_existing(manifest_dir / f"worker_{index:02d}_manifest.csv", 45)
        print(f"Verified manifests under {manifest_dir}")
        return 0

    rows = build_rows(results_dir, split_dir)
    write_csv(full_path, rows)
    for index in range(10):
        wid = worker_id(index)
        worker_rows = [row for row in rows if row["worker_id"] == wid]
        write_csv(manifest_dir / f"{wid}_manifest.csv", worker_rows)
    print(f"Wrote {full_path} with {len(rows)} rows.")
    print(f"Wrote 10 worker manifests with 45 rows each.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
