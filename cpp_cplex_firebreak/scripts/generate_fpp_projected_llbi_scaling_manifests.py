#!/usr/bin/env python3
"""Generate instance-block manifests for the projected LLBI scaling experiment."""

from __future__ import annotations

import argparse
import csv
import random
import re
from pathlib import Path


FIELDS = [
    "task_id",
    "worker_id",
    "case_id",
    "case_index",
    "seed_base",
    "seed",
    "landscape",
    "forest_path",
    "results_path",
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
    "projected_llbi_family",
    "projected_llbi_variant",
    "use_root_user_cuts",
    "use_lifted_lower_bounds",
    "use_projected_llbi",
    "use_projected_coverage_llbi_poly",
    "use_projected_path_llbi_poly",
    "use_projected_coverage_llbi_exp",
    "use_projected_path_llbi_exp",
    "use_combinatorial_benders",
    "combinatorial_benders_lift",
    "combinatorial_benders_cut_sampling_ratio",
    "combinatorial_benders_separate_fractional",
    "combinatorial_benders_initial_cuts",
    "projected_llbi_root_rounds",
    "projected_llbi_max_cuts_per_round",
    "projected_llbi_violation_tolerance",
    "projected_llbi_cut_density_limit",
    "projected_poly_max_cuts",
    "time_limit",
    "mip_gap",
    "threads",
    "split_dir",
    "output_dir",
    "output_csv",
    "output_json",
    "solution_dir",
    "run_id",
    "solver_command",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("results/batch/fpp_projected_llbi_scaling"))
    parser.add_argument("--landscape", default="Sub20")
    parser.add_argument("--forest-path", default=None)
    parser.add_argument("--results-path", default=None)
    parser.add_argument("--train-counts", default="100,200,400,800")
    parser.add_argument("--alphas", default="0.01,0.02,0.03")
    parser.add_argument("--test-count", type=int, default=200)
    parser.add_argument("--num-cases", type=int, default=5)
    parser.add_argument("--seed-base", type=int, default=20260529)
    parser.add_argument("--time-limit", default="1800")
    parser.add_argument("--mip-gap", default="0.001")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--method-file", type=Path, default=Path("config/fpp_projected_llbi_scaling_methods.txt"))
    parser.add_argument("--cvar-beta", default="0.9")
    parser.add_argument("--mean-cvar-lambda", default="0.5")
    parser.add_argument("--projected-llbi-root-rounds", type=int, default=10)
    parser.add_argument("--projected-llbi-max-cuts-per-round", type=int, default=100)
    parser.add_argument("--projected-llbi-violation-tolerance", default="1e-6")
    parser.add_argument("--projected-llbi-cut-density-limit", default="0")
    parser.add_argument("--projected-poly-max-cuts", default="100000")
    parser.add_argument("--verify-only", action="store_true")
    return parser.parse_args()


def split_csv(text: str) -> list[str]:
    return [item.strip() for item in text.split(",") if item.strip()]


def bool_text(value: bool) -> str:
    return "true" if value else "false"


def slug(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()


def alpha_slug(alpha: str) -> str:
    return alpha.replace(".", "p").replace("-", "m")


def read_methods(path: Path) -> list[str]:
    if not path.exists():
        raise RuntimeError(f"Missing method file: {path}")
    methods: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            methods.append(line)
    if not methods:
        raise RuntimeError(f"No methods found in {path}")
    return methods


def discover_scenario_ids(results_path: Path) -> list[int]:
    message_dir = results_path / "Messages"
    if not message_dir.is_dir():
        raise RuntimeError(f"Missing Messages directory: {message_dir}")
    ids: list[int] = []
    for path in message_dir.glob("MessagesFile*.csv"):
        match = re.search(r"MessagesFile0*([0-9]+)\.csv$", path.name)
        if match:
            ids.append(int(match.group(1)))
    ids = sorted(set(ids))
    if not ids:
        raise RuntimeError(f"No scenario message files found under {message_dir}")
    return ids


def read_ids(path: Path) -> list[int]:
    if not path.exists():
        raise RuntimeError(f"Missing split file: {path}")
    text = path.read_text(encoding="utf-8")
    return [int(token) for token in re.split(r"[\s,;]+", text.strip()) if token]


def split_prefix(landscape: str, seed: int, train_count: int, test_count: int, case_index: int) -> str:
    return f"{landscape}_seed{seed}_train{train_count}_test{test_count}_case{case_index}"


def split_paths(
    split_dir: Path,
    landscape: str,
    seed: int,
    train_count: int,
    test_count: int,
    case_index: int,
) -> tuple[Path, Path]:
    prefix = split_prefix(landscape, seed, train_count, test_count, case_index)
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def validate_split(train_ids: list[int], test_ids: list[int], train_count: int, test_count: int) -> None:
    if len(train_ids) != train_count:
        raise RuntimeError(f"Train split has {len(train_ids)} IDs; expected {train_count}.")
    if len(test_ids) != test_count:
        raise RuntimeError(f"Test split has {len(test_ids)} IDs; expected {test_count}.")
    if len(set(train_ids)) != len(train_ids):
        raise RuntimeError("Train split contains duplicates.")
    if len(set(test_ids)) != len(test_ids):
        raise RuntimeError("Test split contains duplicates.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Train and test splits overlap.")


def prepare_splits(
    *,
    split_dir: Path,
    landscape: str,
    results_path: Path,
    train_counts: list[int],
    test_count: int,
    num_cases: int,
    seed_base: int,
) -> None:
    scenario_ids = discover_scenario_ids(results_path)
    split_dir.mkdir(parents=True, exist_ok=True)
    for train_count in train_counts:
        if train_count + test_count > len(scenario_ids):
            raise RuntimeError(
                f"Requested train_count={train_count} + test_count={test_count}, "
                f"but only {len(scenario_ids)} scenarios are available.")
        for case_index in range(num_cases):
            seed = seed_base + case_index
            train_path, test_path = split_paths(
                split_dir, landscape, seed, train_count, test_count, case_index)
            if train_path.exists() and test_path.exists():
                validate_split(read_ids(train_path), read_ids(test_path), train_count, test_count)
                continue
            shuffled = list(scenario_ids)
            random.Random(seed).shuffle(shuffled)
            train_ids = sorted(shuffled[:train_count])
            test_ids = sorted(shuffled[train_count:train_count + test_count])
            validate_split(train_ids, test_ids, train_count, test_count)
            train_path.write_text("".join(f"{value}\n" for value in train_ids), encoding="utf-8")
            test_path.write_text("".join(f"{value}\n" for value in test_ids), encoding="utf-8")


def objective_settings(method: str, cvar_beta: str, mean_cvar_lambda: str) -> tuple[str, str, str, str]:
    if "MeanCVaR" in method:
        return "MeanCVaR", "mean-cvar", cvar_beta, mean_cvar_lambda
    if "CVaR" in method:
        return "CVaR", "cvar", cvar_beta, "1.0"
    return "Expected", "expected", cvar_beta, "1.0"


def method_flags(method: str) -> dict[str, str]:
    is_saa = method.startswith("FPP-SAA")
    is_combinatorial = "Combinatorial" in method
    projected_family = "none"
    projected_variant = "none"
    if "ProjectedCoverageLLBI" in method:
        projected_family = "coverage"
    elif "ProjectedPathLLBI" in method:
        projected_family = "path"
    if "-poly" in method:
        projected_variant = "poly"
    elif "-exp" in method:
        projected_variant = "exp"

    tokens = method.split("-")
    use_standard_llbi = "LLBI" in tokens
    use_root = method.endswith("-RootCuts")
    return {
        "method_family": "FPP-SAA" if is_saa else (
            "FPP-Branch-Benders-Combinatorial" if is_combinatorial else "FPP-Branch-Benders"),
        "fpp_mode": "fpp_base" if is_saa else "",
        "solver_command": "run-fpp-saa-oos" if is_saa else "run-fpp-branch-benders-oos",
        "projected_llbi_family": projected_family,
        "projected_llbi_variant": projected_variant,
        "use_root_user_cuts": bool_text(use_root),
        "use_lifted_lower_bounds": bool_text(use_standard_llbi),
        "use_projected_llbi": bool_text(projected_family != "none"),
        "use_projected_coverage_llbi_poly": bool_text(projected_family == "coverage" and projected_variant == "poly"),
        "use_projected_path_llbi_poly": bool_text(projected_family == "path" and projected_variant == "poly"),
        "use_projected_coverage_llbi_exp": bool_text(projected_family == "coverage" and projected_variant == "exp"),
        "use_projected_path_llbi_exp": bool_text(projected_family == "path" and projected_variant == "exp"),
        "use_combinatorial_benders": bool_text(is_combinatorial),
    }


def row_for_method(
    *,
    task_index: int,
    worker_id: str,
    case_index: int,
    seed_base: int,
    landscape: str,
    forest_path: Path,
    results_path: Path,
    alpha: str,
    train_count: int,
    test_count: int,
    method: str,
    output_dir: Path,
    split_dir: Path,
    time_limit: str,
    mip_gap: str,
    threads: int,
    cvar_beta: str,
    mean_cvar_lambda: str,
    projected_llbi_root_rounds: int,
    projected_llbi_max_cuts_per_round: int,
    projected_llbi_violation_tolerance: str,
    projected_llbi_cut_density_limit: str,
    projected_poly_max_cuts: str,
) -> dict[str, str]:
    seed = seed_base + case_index
    case_id = f"case{case_index:02d}"
    objective_family, risk_measure, beta, cvar_lambda = objective_settings(
        method, cvar_beta, mean_cvar_lambda)
    flags = method_flags(method)
    worker_dir = output_dir / "workers" / worker_id
    run_id = (
        f"{landscape.lower()}_{case_id}_train{train_count}_test{test_count}_"
        f"alpha{alpha_slug(alpha)}_{slug(method)}"
    )
    task_id = f"{worker_id}_task_{task_index:03d}"
    return {
        "task_id": task_id,
        "worker_id": worker_id,
        "case_id": case_id,
        "case_index": str(case_index),
        "seed_base": str(seed_base),
        "seed": str(seed),
        "landscape": landscape,
        "forest_path": str(forest_path),
        "results_path": str(results_path),
        "alpha": alpha,
        "train_count": str(train_count),
        "test_count": str(test_count),
        "objective_family": objective_family,
        "method": method,
        "method_family": flags["method_family"],
        "fpp_mode": flags["fpp_mode"],
        "risk_measure": risk_measure,
        "cvar_beta": beta,
        "cvar_lambda": cvar_lambda,
        "projected_llbi_family": flags["projected_llbi_family"],
        "projected_llbi_variant": flags["projected_llbi_variant"],
        "use_root_user_cuts": flags["use_root_user_cuts"],
        "use_lifted_lower_bounds": flags["use_lifted_lower_bounds"],
        "use_projected_llbi": flags["use_projected_llbi"],
        "use_projected_coverage_llbi_poly": flags["use_projected_coverage_llbi_poly"],
        "use_projected_path_llbi_poly": flags["use_projected_path_llbi_poly"],
        "use_projected_coverage_llbi_exp": flags["use_projected_coverage_llbi_exp"],
        "use_projected_path_llbi_exp": flags["use_projected_path_llbi_exp"],
        "use_combinatorial_benders": flags["use_combinatorial_benders"],
        "combinatorial_benders_lift": "heuristic",
        "combinatorial_benders_cut_sampling_ratio": "0.10",
        "combinatorial_benders_separate_fractional": "true",
        "combinatorial_benders_initial_cuts": "true",
        "projected_llbi_root_rounds": str(projected_llbi_root_rounds),
        "projected_llbi_max_cuts_per_round": str(projected_llbi_max_cuts_per_round),
        "projected_llbi_violation_tolerance": projected_llbi_violation_tolerance,
        "projected_llbi_cut_density_limit": projected_llbi_cut_density_limit,
        "projected_poly_max_cuts": projected_poly_max_cuts,
        "time_limit": time_limit,
        "mip_gap": mip_gap,
        "threads": str(threads),
        "split_dir": str(split_dir),
        "output_dir": str(worker_dir),
        "output_csv": str(worker_dir / f"batch_results_{worker_id}.csv"),
        "output_json": str(worker_dir / "json" / f"{task_id}.json"),
        "solution_dir": str(worker_dir / "solutions"),
        "run_id": run_id,
        "solver_command": flags["solver_command"],
    }


def build_rows(args: argparse.Namespace, methods: list[str], split_dir: Path) -> list[dict[str, str]]:
    train_counts = [int(value) for value in split_csv(args.train_counts)]
    alphas = split_csv(args.alphas)
    forest_path = Path(args.forest_path or f"../sample_test/data/CanadianFBP/{args.landscape}")
    results_path = Path(args.results_path or f"../sample_test/{args.landscape}")
    rows: list[dict[str, str]] = []
    worker_index = 0
    for train_count in train_counts:
        for alpha in alphas:
            for case_index in range(args.num_cases):
                worker_id = f"worker_{worker_index:03d}"
                for task_index, method in enumerate(methods):
                    rows.append(row_for_method(
                        task_index=task_index,
                        worker_id=worker_id,
                        case_index=case_index,
                        seed_base=args.seed_base,
                        landscape=args.landscape,
                        forest_path=forest_path,
                        results_path=results_path,
                        alpha=alpha,
                        train_count=train_count,
                        test_count=args.test_count,
                        method=method,
                        output_dir=args.output_dir,
                        split_dir=split_dir,
                        time_limit=args.time_limit,
                        mip_gap=args.mip_gap,
                        threads=args.threads,
                        cvar_beta=args.cvar_beta,
                        mean_cvar_lambda=args.mean_cvar_lambda,
                        projected_llbi_root_rounds=args.projected_llbi_root_rounds,
                        projected_llbi_max_cuts_per_round=args.projected_llbi_max_cuts_per_round,
                        projected_llbi_violation_tolerance=args.projected_llbi_violation_tolerance,
                        projected_llbi_cut_density_limit=args.projected_llbi_cut_density_limit,
                        projected_poly_max_cuts=args.projected_poly_max_cuts,
                    ))
                worker_index += 1
    return rows


def write_csv(path: Path, rows: list[dict[str, str]], fieldnames: list[str] = FIELDS) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def verify_manifests(manifest_dir: Path, expected_rows: int, expected_workers: int, rows_per_worker: int) -> None:
    full = manifest_dir / "full_task_manifest.csv"
    if not full.exists():
        raise RuntimeError(f"Missing manifest: {full}")
    with full.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != expected_rows:
        raise RuntimeError(f"{full} has {len(rows)} rows; expected {expected_rows}.")
    for worker_index in range(expected_workers):
        path = manifest_dir / f"worker_{worker_index:03d}_manifest.csv"
        if not path.exists():
            raise RuntimeError(f"Missing worker manifest: {path}")
        with path.open(newline="", encoding="utf-8") as inp:
            worker_rows = list(csv.DictReader(inp))
        if len(worker_rows) != rows_per_worker:
            raise RuntimeError(f"{path} has {len(worker_rows)} rows; expected {rows_per_worker}.")


def main() -> int:
    args = parse_args()
    methods = read_methods(args.method_file)
    train_counts = [int(value) for value in split_csv(args.train_counts)]
    alphas = split_csv(args.alphas)
    expected_workers = len(train_counts) * len(alphas) * args.num_cases
    rows_per_worker = len(methods)
    expected_rows = expected_workers * rows_per_worker
    split_dir = args.output_dir / "splits"
    manifest_dir = args.output_dir / "manifests"

    if args.verify_only:
        verify_manifests(manifest_dir, expected_rows, expected_workers, rows_per_worker)
        print(
            f"Verified {expected_rows} manifest rows "
            f"({expected_workers} workers x {rows_per_worker} rows).")
        return 0

    prepare_splits(
        split_dir=split_dir,
        landscape=args.landscape,
        results_path=Path(args.results_path or f"../sample_test/{args.landscape}"),
        train_counts=train_counts,
        test_count=args.test_count,
        num_cases=args.num_cases,
        seed_base=args.seed_base,
    )
    rows = build_rows(args, methods, split_dir)
    if len(rows) != expected_rows:
        raise RuntimeError(f"Generated {len(rows)} rows; expected {expected_rows}.")
    write_csv(manifest_dir / "full_task_manifest.csv", rows)
    for worker_index in range(expected_workers):
        worker_id = f"worker_{worker_index:03d}"
        worker_rows = [row for row in rows if row["worker_id"] == worker_id]
        if len(worker_rows) != rows_per_worker:
            raise RuntimeError(
                f"{worker_id} has {len(worker_rows)} rows; expected {rows_per_worker}.")
        write_csv(manifest_dir / f"{worker_id}_manifest.csv", worker_rows)
    print(
        f"Wrote {expected_rows} manifest rows "
        f"({expected_workers} workers x {rows_per_worker} rows) under {manifest_dir}.")
    print(f"Prepared controlled splits under {split_dir}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
