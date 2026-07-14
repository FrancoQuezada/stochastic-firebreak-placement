#!/usr/bin/env python3
"""Generate manifests for the FPP new_instances scaling experiment."""

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
    "split_seed",
    "instance_id",
    "folder_name",
    "instance_type",
    "declared_cells",
    "inferred_cells",
    "metadata_status",
    "metadata_consistent",
    "graph_variant",
    "landscape",
    "forest_path",
    "results_path",
    "alpha",
    "train_count",
    "test_count",
    "training_pool_min",
    "training_pool_max",
    "test_pool_min",
    "test_pool_max",
    "fixed_oos_test_set",
    "train_split_path",
    "test_split_path",
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
    "combinatorial_benders_scenario_order",
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
    parser.add_argument("--output-dir", type=Path, default=Path("results/batch/fpp_new_instances_scaling"))
    parser.add_argument("--instances-root", type=Path, default=Path("new_instances"))
    parser.add_argument("--instance-config", type=Path, default=Path("config/fpp_new_instances_scaling_instances.csv"))
    parser.add_argument("--instance-filter", default="")
    parser.add_argument("--preflight-csv", type=Path, default=None)
    parser.add_argument("--train-counts", default="100,200,400,800")
    parser.add_argument("--alphas", default="0.01,0.02,0.03")
    parser.add_argument("--test-count", type=int, default=1000)
    parser.add_argument("--training-pool-min", type=int, default=1)
    parser.add_argument("--training-pool-max", type=int, default=9000)
    parser.add_argument("--test-pool-min", type=int, default=9001)
    parser.add_argument("--test-pool-max", type=int, default=10000)
    parser.add_argument("--num-cases", type=int, default=5)
    parser.add_argument("--seed-base", type=int, default=20260529)
    parser.add_argument("--time-limit", default="1800")
    parser.add_argument("--mip-gap", default="0.001")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--method-file", type=Path, default=Path("config/fpp_new_instances_scaling_methods.txt"))
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


def bool_value(value: str | None) -> bool:
    return str(value or "").strip().lower() in {"1", "true", "yes", "on"}


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
    duplicates = sorted({method for method in methods if methods.count(method) > 1})
    if duplicates:
        raise RuntimeError(f"Duplicate methods in {path}: {duplicates}")
    return methods


def read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        raise RuntimeError(f"Missing CSV: {path}")
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def resolve_instance_path(raw: str, instances_root: Path, folder_name: str) -> Path:
    if not raw:
        return instances_root / folder_name
    path = Path(raw)
    if path.is_absolute():
        return path
    parts = path.parts
    if parts and parts[0] == "new_instances":
        return instances_root.joinpath(*parts[1:])
    return path


def read_preflight(path: Path | None) -> dict[str, dict[str, str]]:
    if path is None:
        return {}
    if not path.exists():
        raise RuntimeError(f"Missing preflight CSV: {path}")
    rows = read_csv(path)
    return {row.get("folder", ""): row for row in rows if row.get("folder")}


def selected_instances(args: argparse.Namespace) -> list[dict[str, str]]:
    rows = read_csv(args.instance_config)
    for raw_index, row in enumerate(rows):
        row["_config_index"] = str(raw_index)
    filters = split_csv(args.instance_filter)
    filter_set = set(filters)
    enabled = [row for row in rows if bool_value(row.get("enabled", "true"))]
    if filters:
        known = {row.get("instance_id", "") for row in enabled}
        unknown = sorted(filter_set - known)
        if unknown:
            raise RuntimeError(f"Unknown enabled instance IDs in --instance-filter: {unknown}")
        enabled = [row for row in enabled if row.get("instance_id", "") in filter_set]
    if not enabled:
        raise RuntimeError("No enabled instances selected.")

    preflight = read_preflight(args.preflight_csv)
    out: list[dict[str, str]] = []
    for row in enabled:
        folder_name = row["folder_name"]
        pf = preflight.get(folder_name, {})
        instance = dict(row)
        instance["instance_index"] = row["_config_index"]
        instance["forest_path"] = str(resolve_instance_path(row.get("forest_path", ""), args.instances_root, folder_name))
        instance["results_path"] = str(resolve_instance_path(row.get("results_path", ""), args.instances_root, folder_name))
        instance["inferred_cells"] = pf.get("inferred_n_cells", "")
        instance["metadata_status"] = pf.get("status", "unknown" if preflight else "not_checked")
        instance["metadata_consistent"] = pf.get("metadata_consistent", "")
        instance["graph_variant"] = pf.get("graph_variant", "")
        out.append(instance)
    return out


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


def write_ids(path: Path, ids: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"{value}\n" for value in ids), encoding="utf-8")


def id_list_arg(ids: list[int]) -> str:
    return ",".join(str(value) for value in ids)


def split_seed(seed_base: int, instance_index: int, case_index: int, train_count: int) -> int:
    return seed_base + instance_index * 100000 + case_index * 1000 + train_count


def split_prefix(instance_id: str, seed: int, train_count: int, test_count: int, case_index: int) -> str:
    return f"{instance_id}_seed{seed}_train{train_count}_test{test_count}_case{case_index}"


def split_paths(
    split_dir: Path,
    instance_id: str,
    seed: int,
    train_count: int,
    test_count: int,
    case_index: int,
) -> tuple[Path, Path]:
    prefix = split_prefix(instance_id, seed, train_count, test_count, case_index)
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def validate_pool_args(args: argparse.Namespace) -> list[int]:
    if args.training_pool_min > args.training_pool_max:
        raise RuntimeError("training-pool-min must be <= training-pool-max.")
    if args.test_pool_min > args.test_pool_max:
        raise RuntimeError("test-pool-min must be <= test-pool-max.")
    test_ids = list(range(args.test_pool_min, args.test_pool_max + 1))
    if len(test_ids) != args.test_count:
        raise RuntimeError(
            f"Fixed OOS range {args.test_pool_min}..{args.test_pool_max} has {len(test_ids)} IDs; "
            f"expected --test-count {args.test_count}.")
    return test_ids


def validate_split(
    *,
    train_ids: list[int],
    test_ids: list[int],
    train_count: int,
    expected_test_ids: list[int],
    training_pool_min: int,
    training_pool_max: int,
) -> None:
    if len(train_ids) != train_count:
        raise RuntimeError(f"Train split has {len(train_ids)} IDs; expected {train_count}.")
    if len(test_ids) != len(expected_test_ids):
        raise RuntimeError(f"Test split has {len(test_ids)} IDs; expected {len(expected_test_ids)}.")
    if len(set(train_ids)) != len(train_ids):
        raise RuntimeError("Train split contains duplicates.")
    if len(set(test_ids)) != len(test_ids):
        raise RuntimeError("Test split contains duplicates.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Train and test splits overlap.")
    outside = [value for value in train_ids if value < training_pool_min or value > training_pool_max]
    if outside:
        raise RuntimeError(f"Train split contains IDs outside training pool: {outside[:10]}")
    if test_ids != expected_test_ids:
        raise RuntimeError("Test split does not match the configured fixed OOS set.")


def prepare_splits(
    *,
    args: argparse.Namespace,
    instances: list[dict[str, str]],
    train_counts: list[int],
    split_dir: Path,
) -> dict[tuple[str, int, int], tuple[Path, Path, int, list[int], list[int]]]:
    split_dir.mkdir(parents=True, exist_ok=True)
    expected_test_ids = validate_pool_args(args)
    training_pool = list(range(args.training_pool_min, args.training_pool_max + 1))
    if not training_pool:
        raise RuntimeError("Training pool is empty.")

    split_index: dict[tuple[str, int, int], tuple[Path, Path, int, list[int], list[int]]] = {}
    for instance in instances:
        instance_offset = int(instance["instance_index"])
        scenario_ids = set(discover_scenario_ids(Path(instance["results_path"])))
        needed_ids = set(training_pool).union(expected_test_ids)
        missing = sorted(needed_ids - scenario_ids)
        if missing:
            raise RuntimeError(
                f"{instance['instance_id']} is missing {len(missing)} message files required by the configured pools; "
                f"first missing IDs: {missing[:10]}")
        for train_count in train_counts:
            if train_count > len(training_pool):
                raise RuntimeError(
                    f"train_count={train_count} exceeds training pool size {len(training_pool)}.")
            for case_index in range(args.num_cases):
                seed = split_seed(args.seed_base, instance_offset, case_index, train_count)
                train_path, test_path = split_paths(
                    split_dir, instance["instance_id"], seed, train_count, args.test_count, case_index)
                train_ids = sorted(random.Random(seed).sample(training_pool, train_count))
                test_ids = list(expected_test_ids)
                validate_split(
                    train_ids=train_ids,
                    test_ids=test_ids,
                    train_count=train_count,
                    expected_test_ids=expected_test_ids,
                    training_pool_min=args.training_pool_min,
                    training_pool_max=args.training_pool_max,
                )
                if train_path.exists() or test_path.exists():
                    existing_train = read_ids(train_path)
                    existing_test = read_ids(test_path)
                    if existing_train != train_ids or existing_test != test_ids:
                        raise RuntimeError(
                            f"Existing split files do not match deterministic split for "
                            f"{instance['instance_id']} case={case_index} train_count={train_count}: "
                            f"{train_path}, {test_path}")
                else:
                    write_ids(train_path, train_ids)
                    write_ids(test_path, test_ids)
                split_index[(instance["instance_id"], train_count, case_index)] = (
                    train_path, test_path, seed, train_ids, test_ids)
    return split_index


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
    scenario_order = "eta-desc" if method.endswith("-EtaDesc") else "eta-asc"
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
        "combinatorial_benders_scenario_order": scenario_order,
    }


def row_for_method(
    *,
    task_index: int,
    worker_id: str,
    case_index: int,
    seed_base: int,
    instance: dict[str, str],
    alpha: str,
    train_count: int,
    test_count: int,
    method: str,
    output_dir: Path,
    split_dir: Path,
    train_path: Path,
    test_path: Path,
    seed: int,
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
    training_pool_min: int,
    training_pool_max: int,
    test_pool_min: int,
    test_pool_max: int,
) -> dict[str, str]:
    case_id = f"case{case_index:02d}"
    objective_family, risk_measure, beta, cvar_lambda = objective_settings(
        method, cvar_beta, mean_cvar_lambda)
    flags = method_flags(method)
    worker_dir = output_dir / "workers" / worker_id
    run_id = (
        f"{instance['instance_id']}_{case_id}_train{train_count}_test{test_count}_"
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
        "split_seed": str(seed),
        "instance_id": instance["instance_id"],
        "folder_name": instance["folder_name"],
        "instance_type": instance["instance_type"],
        "declared_cells": instance["declared_cells"],
        "inferred_cells": instance.get("inferred_cells", ""),
        "metadata_status": instance.get("metadata_status", ""),
        "metadata_consistent": instance.get("metadata_consistent", ""),
        "graph_variant": instance.get("graph_variant", ""),
        "landscape": instance["landscape"],
        "forest_path": instance["forest_path"],
        "results_path": instance["results_path"],
        "alpha": alpha,
        "train_count": str(train_count),
        "test_count": str(test_count),
        "training_pool_min": str(training_pool_min),
        "training_pool_max": str(training_pool_max),
        "test_pool_min": str(test_pool_min),
        "test_pool_max": str(test_pool_max),
        "fixed_oos_test_set": "true",
        "train_split_path": str(train_path),
        "test_split_path": str(test_path),
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
        "combinatorial_benders_scenario_order": flags["combinatorial_benders_scenario_order"],
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


def build_rows(
    args: argparse.Namespace,
    methods: list[str],
    instances: list[dict[str, str]],
    split_dir: Path,
    split_index: dict[tuple[str, int, int], tuple[Path, Path, int, list[int], list[int]]],
) -> list[dict[str, str]]:
    train_counts = [int(value) for value in split_csv(args.train_counts)]
    alphas = split_csv(args.alphas)
    rows: list[dict[str, str]] = []
    worker_index = 0
    for instance in instances:
        for train_count in train_counts:
            for alpha in alphas:
                for case_index in range(args.num_cases):
                    worker_id = f"worker_{worker_index:03d}"
                    train_path, test_path, seed, _train_ids, _test_ids = split_index[
                        (instance["instance_id"], train_count, case_index)]
                    for task_index, method in enumerate(methods):
                        rows.append(row_for_method(
                            task_index=task_index,
                            worker_id=worker_id,
                            case_index=case_index,
                            seed_base=args.seed_base,
                            instance=instance,
                            alpha=alpha,
                            train_count=train_count,
                            test_count=args.test_count,
                            method=method,
                            output_dir=args.output_dir,
                            split_dir=split_dir,
                            train_path=train_path,
                            test_path=test_path,
                            seed=seed,
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
                            training_pool_min=args.training_pool_min,
                            training_pool_max=args.training_pool_max,
                            test_pool_min=args.test_pool_min,
                            test_pool_max=args.test_pool_max,
                        ))
                    worker_index += 1
    return rows


def write_csv(path: Path, rows: list[dict[str, str]], fieldnames: list[str] = FIELDS) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def verify_manifests(
    *,
    manifest_dir: Path,
    expected_rows: int,
    expected_workers: int,
    rows_per_worker: int,
) -> None:
    full = manifest_dir / "full_task_manifest.csv"
    if not full.exists():
        raise RuntimeError(f"Missing manifest: {full}")
    with full.open(newline="", encoding="utf-8") as inp:
        rows = list(csv.DictReader(inp))
    if len(rows) != expected_rows:
        raise RuntimeError(f"{full} has {len(rows)} rows; expected {expected_rows}.")
    seen_workers = sorted({row.get("worker_id", "") for row in rows})
    if len(seen_workers) != expected_workers:
        raise RuntimeError(f"{full} has {len(seen_workers)} workers; expected {expected_workers}.")
    for worker_index in range(expected_workers):
        worker_id = f"worker_{worker_index:03d}"
        path = manifest_dir / f"{worker_id}_manifest.csv"
        if not path.exists():
            raise RuntimeError(f"Missing worker manifest: {path}")
        with path.open(newline="", encoding="utf-8") as inp:
            worker_rows = list(csv.DictReader(inp))
        if len(worker_rows) != rows_per_worker:
            raise RuntimeError(f"{path} has {len(worker_rows)} rows; expected {rows_per_worker}.")
        methods = {row.get("method", "") for row in worker_rows}
        if len(methods) != rows_per_worker:
            raise RuntimeError(f"{path} has duplicate method rows.")
    print(
        f"Verified {expected_rows} manifest rows "
        f"({expected_workers} workers x {rows_per_worker} rows).")


def main() -> int:
    args = parse_args()
    methods = read_methods(args.method_file)
    instances = selected_instances(args)
    train_counts = [int(value) for value in split_csv(args.train_counts)]
    alphas = split_csv(args.alphas)
    if not train_counts:
        raise RuntimeError("No train counts configured.")
    if not alphas:
        raise RuntimeError("No alpha values configured.")

    expected_workers = len(instances) * len(train_counts) * len(alphas) * args.num_cases
    rows_per_worker = len(methods)
    expected_rows = expected_workers * rows_per_worker
    split_dir = args.output_dir / "splits"
    manifest_dir = args.output_dir / "manifests"

    if args.verify_only:
        verify_manifests(
            manifest_dir=manifest_dir,
            expected_rows=expected_rows,
            expected_workers=expected_workers,
            rows_per_worker=rows_per_worker,
        )
        return 0

    split_index = prepare_splits(
        args=args,
        instances=instances,
        train_counts=train_counts,
        split_dir=split_dir,
    )
    rows = build_rows(args, methods, instances, split_dir, split_index)
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

    selected_ids = ",".join(instance["instance_id"] for instance in instances)
    print(f"Selected instances: {selected_ids}")
    print(f"Methods: {rows_per_worker}")
    print(
        f"Wrote {expected_rows} manifest rows "
        f"({expected_workers} workers x {rows_per_worker} rows) under {manifest_dir}.")
    print(f"Prepared controlled fixed-OOS splits under {split_dir}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
