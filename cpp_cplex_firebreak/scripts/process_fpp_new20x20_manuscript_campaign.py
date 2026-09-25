#!/usr/bin/env python3
"""Validate and post-process the dedicated new20x20 manuscript campaign.

This module is deliberately separate from the legacy scaling merge path.  It keeps
the full worker CSVs as diagnostics while producing the small, stable manuscript
tables requested for the principal computational campaign.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path


EXPECTED_INSTANCE = "new20x20"
EXPECTED_REBURN_INSTANCE = "new20x20_reburn"
EXPECTED_N = (100, 200, 400, 800)
EXPECTED_ALPHAS = ("0.01", "0.02", "0.03")
EXPECTED_OBJECTIVES = ("Expected", "CVaR", "MeanCVaR")
EXPECTED_CASES = tuple(f"case{i:02d}" for i in range(30))
EXPECTED_RUNS = 8640
SEED_BASE = 20260529
VALIDATION_ABS_TOL = 1.0e-6
VALIDATION_REL_TOL = 1.0e-6

EXPECTED_METHODS = (
    "FPP-SAA",
    "FPP-Branch-Benders-RootCuts",
    "FPP-Branch-Benders-LLBI-RootCuts",
    "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts",
    "FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts",
    "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts",
    "FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts",
    "FPP-Branch-Benders-Combinatorial",
    "FPP-SAA-CVaR",
    "FPP-Branch-Benders-CVaR-RootCuts",
    "FPP-Branch-Benders-CVaR-LLBI-RootCuts",
    "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-poly-RootCuts",
    "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-poly-RootCuts",
    "FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp-RootCuts",
    "FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp-RootCuts",
    "FPP-Branch-Benders-Combinatorial-CVaR",
    "FPP-SAA-MeanCVaR",
    "FPP-Branch-Benders-MeanCVaR-RootCuts",
    "FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts",
    "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-poly-RootCuts",
    "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-poly-RootCuts",
    "FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts",
    "FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp-RootCuts",
    "FPP-Branch-Benders-Combinatorial-MeanCVaR",
)

MANUSCRIPT_FIELDS = (
    "experiment_id",
    "run_id",
    "case_id",
    "seed",
    "instance_id",
    "N",
    "alpha",
    "objective_type",
    "risk_measure",
    "cvar_beta",
    "cvar_lambda",
    "method",
    "time_limit_sec",
    "threads",
    "target_mip_gap",
    "solver_status",
    "termination_reason",
    "has_incumbent",
    "objective_value",
    "upper_bound",
    "lower_bound",
    "mip_gap",
    "runtime_sec",
    "wall_time_sec",
    "train_expected_burned_area",
    "train_cvar_burned_area",
    "train_worst10_burned_area",
    "train_eval_objective_value",
    "oos_expected_burned_area",
    "oos_cvar_burned_area",
    "oos_worst10_burned_area",
    "oos_eval_objective_value",
    "reburn_expected_burned_area",
    "reburn_cvar_burned_area",
    "reburn_worst10_burned_area",
    "reburn_eval_objective_value",
    "selected_firebreak_count",
    "train_objective_abs_discrepancy",
    "train_objective_rel_discrepancy",
    "objective_validation_passed",
    "execution_status",
    "attempt",
)

SOLUTION_FIELDS = (
    "run_id",
    "instance_id",
    "case_id",
    "N",
    "alpha",
    "objective_type",
    "method",
    "selected_firebreak_count",
    "selected_firebreak_ids",
)

SUMMARY_FIELDS = (
    "N",
    "alpha",
    "objective_type",
    "method",
    "expected_runs",
    "observed_runs",
    "runs_with_incumbent",
    "runs_solved_to_proven_optimality",
    "runs_stopped_by_time_limit_with_incumbent",
    "failed_no_incumbent_runs",
    "optimality_rate",
    "time_limit_rate",
    "mean_runtime_sec",
    "median_runtime_sec",
    "stddev_runtime_sec",
    "mean_final_mip_gap",
    "median_final_mip_gap",
    "mip_gap_unit",
    "mean_upper_bound",
    "mean_lower_bound",
    "mean_train_objective_value",
    "stddev_train_objective_value",
    "mean_oos_objective_value",
    "stddev_oos_objective_value",
    "mean_reburn_objective_value",
    "stddev_reburn_objective_value",
    "mean_oos_expected_burned_area",
    "mean_oos_cvar",
    "mean_reburn_expected_burned_area",
    "mean_reburn_cvar",
)

METHOD_CONFIGURATION_FIELDS = (
    "method",
    "objective_type",
    "risk_measure",
    "time_limit_sec",
    "target_mip_gap",
    "threads",
    "cvar_beta",
    "cvar_lambda",
    "root_cuts_enabled",
    "root_user_cut_max_rounds",
    "standard_llbi_enabled",
    "projected_llbi_enabled",
    "projected_family",
    "projected_variant",
    "projected_root_rounds",
    "projected_max_cuts_per_round",
    "projected_violation_tolerance",
    "projected_density_limit",
    "projected_poly_max_cuts",
    "combinatorial_benders_enabled",
    "combinatorial_lift",
    "combinatorial_sampling_ratio",
    "combinatorial_fractional_separation",
    "combinatorial_initial_cuts",
    "combinatorial_scenario_order",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=Path("results/batch/fpp_new20x20_manuscript_campaign"),
    )
    parser.add_argument("--experiment-id", default="fpp_new20x20_manuscript_campaign")
    parser.add_argument("--manifest-only", action="store_true")
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="Permit missing worker rows for intermediate inspection. Observed malformed rows still fail.",
    )
    parser.add_argument(
        "--allow-nonstandard-design",
        action="store_true",
        help="Validate structural invariants but not the exact 8640-run scientific grid (smoke tests only).",
    )
    parser.add_argument(
        "--keep-full-json",
        action="store_true",
        help="Record that successful detailed JSON is retained by the launcher.",
    )
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_csv(path: Path, rows: list[dict[str, object]], fields: tuple[str, ...] | list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    with temp.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(fields), extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    temp.replace(path)


def clean(value: object) -> str:
    if value is None:
        return ""
    text = str(value).strip()
    return "" if text.lower() in {"", "nan", "na", "n/a", "none", "null"} else text


def first(row: dict[str, str], *names: str) -> str:
    for name in names:
        value = clean(row.get(name))
        if value:
            return value
    return ""


def number(value: object) -> float | None:
    text = clean(value)
    if not text:
        return None
    try:
        result = float(text)
    except ValueError:
        return None
    return result if math.isfinite(result) else None


def integer(value: object) -> int | None:
    parsed = number(value)
    return None if parsed is None else int(parsed)


def truthy(value: object) -> bool:
    return clean(value).lower() in {"1", "true", "yes", "on"}


def fmt(value: float | int | None) -> str:
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.12g}"


def normalize_alpha(value: object) -> str:
    parsed = number(value)
    return "" if parsed is None else f"{parsed:.2f}"


def number_equals(value: object, expected: float, tolerance: float = 1.0e-12) -> bool:
    parsed = number(value)
    return parsed is not None and abs(parsed - expected) <= tolerance


def parse_ids(value: object) -> list[int]:
    text = clean(value)
    if not text:
        return []
    return [int(token) for token in re.split(r"[\s,;]+", text) if token]


def objective_for_method(method: str) -> str:
    if "MeanCVaR" in method:
        return "MeanCVaR"
    if "CVaR" in method:
        return "CVaR"
    return "Expected"


def evaluated_objective(objective: str, expected: float | None, cvar: float | None,
                        cvar_lambda: float | None) -> float | None:
    if objective == "Expected":
        return expected
    if objective == "CVaR":
        return cvar
    if expected is None or cvar is None or cvar_lambda is None:
        return None
    return (1.0 - cvar_lambda) * expected + cvar_lambda * cvar


def is_optimal(status: str) -> bool:
    lowered = status.lower().replace("_", " ")
    return "optimal" in lowered and "not optimal" not in lowered


def is_time_limit(status: str, reason: str) -> bool:
    text = f"{status} {reason}".lower().replace("_", " ")
    return "time limit" in text or "timelimit" in text


def status_has_no_incumbent(status: str) -> bool:
    lowered = status.lower()
    return any(token in lowered for token in ("no feasible", "infeasible", "unbounded", "failed"))


def read_split(path_text: str, cache: dict[str, tuple[int, ...]], errors: list[str]) -> tuple[int, ...]:
    if path_text in cache:
        return cache[path_text]
    path = Path(path_text)
    try:
        ids = tuple(parse_ids(path.read_text(encoding="utf-8")))
    except (OSError, ValueError) as exc:
        errors.append(f"cannot read split {path}: {exc}")
        ids = ()
    cache[path_text] = ids
    return ids


def validate_manifest(rows: list[dict[str, str]], *, exact_design: bool) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    notes: list[str] = []
    if not rows:
        return ["manifest is empty"], notes

    for field in ("task_id", "run_id"):
        values = [clean(row.get(field)) for row in rows]
        if any(not value for value in values):
            errors.append(f"manifest contains blank {field}")
        duplicates = [value for value, count in Counter(values).items() if value and count > 1]
        if duplicates:
            errors.append(f"manifest contains {len(duplicates)} duplicate {field} values")

    instances = {clean(row.get("instance_id")) for row in rows}
    if instances != {EXPECTED_INSTANCE}:
        errors.append(f"optimization instances are {sorted(instances)}; expected only {EXPECTED_INSTANCE}")
    if any(clean(row.get("instance_id")) == EXPECTED_REBURN_INSTANCE for row in rows):
        errors.append("manifest contains an optimization task on new20x20_reburn")
    if {clean(row.get("landscape")) for row in rows} != {EXPECTED_INSTANCE}:
        errors.append("manifest landscape column is not exclusively new20x20")
    if any(clean(row.get("paired_reburn_instance_id")) != EXPECTED_REBURN_INSTANCE for row in rows):
        errors.append("not every row resolves paired_reburn_instance_id=new20x20_reburn")
    if any(not truthy(row.get("paired_evaluation_enabled")) for row in rows):
        errors.append("not every row enables paired reburn evaluation")
    if any(clean(row.get("weight_profile")) not in {"", "homogeneous"} for row in rows):
        errors.append("campaign contains a non-homogeneous weight profile")
    if any(clean(row.get("weight_map_path")) for row in rows):
        errors.append("ordinary homogeneous campaign unexpectedly requires a weight registry entry")
    if any(clean(row.get("weight_replicate")) not in {"", "0"} for row in rows):
        errors.append("homogeneous campaign contains more than the single weight replicate 0")

    methods = {clean(row.get("method")) for row in rows}
    objectives = {clean(row.get("objective_family")) for row in rows}
    ns = {integer(row.get("train_count")) for row in rows}
    alphas = {normalize_alpha(row.get("alpha")) for row in rows}
    cases = {clean(row.get("case_id")) for row in rows}
    for row in rows:
        method = clean(row.get("method"))
        if clean(row.get("objective_family")) != objective_for_method(method):
            errors.append(f"objective/method mismatch for run_id={row.get('run_id', '')}")
            break

    if exact_design:
        if len(rows) != EXPECTED_RUNS:
            errors.append(f"manifest has {len(rows)} tasks; expected {EXPECTED_RUNS}")
        if methods != set(EXPECTED_METHODS):
            errors.append(
                f"method panel mismatch: missing={sorted(set(EXPECTED_METHODS) - methods)} "
                f"extra={sorted(methods - set(EXPECTED_METHODS))}")
        if ns != set(EXPECTED_N):
            errors.append(f"N values are {sorted(x for x in ns if x is not None)}; expected {EXPECTED_N}")
        if alphas != set(EXPECTED_ALPHAS):
            errors.append(f"alpha values are {sorted(alphas)}; expected {EXPECTED_ALPHAS}")
        if objectives != set(EXPECTED_OBJECTIVES):
            errors.append(f"objectives are {sorted(objectives)}; expected {EXPECTED_OBJECTIVES}")
        if cases != set(EXPECTED_CASES):
            errors.append(f"cases are not exactly case00..case29 (observed {len(cases)})")
        exact_settings = (
            ("seed_base", lambda row: clean(row.get("seed_base")) == str(SEED_BASE), str(SEED_BASE)),
            ("time_limit", lambda row: number_equals(row.get("time_limit"), 1800.0), "1800"),
            ("mip_gap", lambda row: number_equals(row.get("mip_gap"), 0.001), "0.001"),
            ("threads", lambda row: clean(row.get("threads")) == "1", "1"),
            ("training_pool_min", lambda row: clean(row.get("training_pool_min")) == "1", "1"),
            ("training_pool_max", lambda row: clean(row.get("training_pool_max")) == "9000", "9000"),
            ("test_pool_min", lambda row: clean(row.get("test_pool_min")) == "9001", "9001"),
            ("test_pool_max", lambda row: clean(row.get("test_pool_max")) == "10000", "10000"),
            ("test_count", lambda row: clean(row.get("test_count")) == "1000", "1000"),
            ("cvar_beta", lambda row: number_equals(row.get("cvar_beta"), 0.9), "0.9"),
            ("projected_llbi_root_rounds", lambda row: clean(row.get("projected_llbi_root_rounds")) == "100", "100"),
            ("projected_llbi_max_cuts_per_round", lambda row: clean(row.get("projected_llbi_max_cuts_per_round")) == "100", "100"),
            ("projected_llbi_violation_tolerance", lambda row: number_equals(row.get("projected_llbi_violation_tolerance"), 1.0e-3), "1e-3"),
            ("projected_llbi_cut_density_limit", lambda row: number_equals(row.get("projected_llbi_cut_density_limit"), 0.0), "0"),
            ("projected_poly_max_cuts", lambda row: clean(row.get("projected_poly_max_cuts")) == "100000", "100000"),
        )
        for name, predicate, expected in exact_settings:
            if any(not predicate(row) for row in rows):
                errors.append(f"manifest setting {name} is not uniformly {expected}")
        for row in rows:
            method = clean(row.get("method"))
            if objective_for_method(method) == "MeanCVaR" and not number_equals(row.get("cvar_lambda"), 0.5):
                errors.append("Mean-CVaR lambda is not uniformly 0.5")
                break
        combinatorial_rows = [row for row in rows if "Combinatorial" in clean(row.get("method"))]
        combinatorial_checks = (
            ("lift", lambda row: clean(row.get("combinatorial_benders_lift")) == "heuristic"),
            ("sampling ratio", lambda row: number_equals(row.get("combinatorial_benders_cut_sampling_ratio"), 0.10)),
            ("fractional separation", lambda row: truthy(row.get("combinatorial_benders_separate_fractional"))),
            ("initial cuts", lambda row: truthy(row.get("combinatorial_benders_initial_cuts"))),
            ("scenario order", lambda row: clean(row.get("combinatorial_benders_scenario_order")) == "eta-asc"),
        )
        for label, predicate in combinatorial_checks:
            if any(not predicate(row) for row in combinatorial_rows):
                errors.append(f"combinatorial Benders {label} does not match the scientific configuration")

    block_methods: dict[tuple[int | None, str, str], list[str]] = defaultdict(list)
    group_cases: dict[tuple[int | None, str, str, str], list[str]] = defaultdict(list)
    for row in rows:
        n = integer(row.get("train_count"))
        alpha = normalize_alpha(row.get("alpha"))
        case = clean(row.get("case_id"))
        method = clean(row.get("method"))
        objective = clean(row.get("objective_family"))
        block_methods[(n, alpha, case)].append(method)
        group_cases[(n, alpha, objective, method)].append(case)
    expected_method_count = 24 if exact_design else len(methods)
    bad_blocks = [key for key, values in block_methods.items()
                  if len(values) != expected_method_count or len(set(values)) != expected_method_count]
    if bad_blocks:
        errors.append(f"{len(bad_blocks)} controlled blocks do not contain exactly {expected_method_count} unique methods")
    if exact_design:
        bad_case_groups = [key for key, values in group_cases.items()
                           if len(values) != 30 or set(values) != set(EXPECTED_CASES)]
        if bad_case_groups:
            errors.append(f"{len(bad_case_groups)} (N,alpha,objective,method) groups do not contain 30 cases")

    split_cache: dict[str, tuple[int, ...]] = {}
    paired_splits: dict[tuple[int | None, str], set[tuple[int, ...]]] = defaultdict(set)
    paired_seeds: dict[tuple[int | None, str], set[str]] = defaultdict(set)
    all_test_splits: set[tuple[int, ...]] = set()
    for row in rows:
        n = integer(row.get("train_count"))
        case = clean(row.get("case_id"))
        train_ids = read_split(clean(row.get("train_split_path")), split_cache, errors)
        test_ids = read_split(clean(row.get("test_split_path")), split_cache, errors)
        if n is not None and len(train_ids) != n:
            errors.append(f"run_id={row.get('run_id', '')}: train split has {len(train_ids)} IDs, expected {n}")
        if len(train_ids) != len(set(train_ids)) or len(test_ids) != len(set(test_ids)):
            errors.append(f"run_id={row.get('run_id', '')}: split contains duplicate IDs")
        if set(train_ids).intersection(test_ids):
            errors.append(f"run_id={row.get('run_id', '')}: training and OOS IDs overlap")
        training_min = integer(row.get("training_pool_min"))
        training_max = integer(row.get("training_pool_max"))
        if training_min is not None and training_max is not None and any(
            value < training_min or value > training_max for value in train_ids
        ):
            errors.append(f"run_id={row.get('run_id', '')}: training ID outside configured pool")
        test_min = integer(row.get("test_pool_min"))
        test_max = integer(row.get("test_pool_max"))
        if test_min is not None and test_max is not None and test_ids != tuple(range(test_min, test_max + 1)):
            errors.append(f"run_id={row.get('run_id', '')}: OOS split is not its configured fixed range")
        paired_splits[(n, case)].add(train_ids)
        paired_seeds[(n, case)].add(clean(row.get("seed")))
        all_test_splits.add(test_ids)
    mismatched = [key for key, versions in paired_splits.items() if len(versions) != 1]
    if mismatched:
        errors.append(f"{len(mismatched)} (N,case) groups use more than one training split")
    seed_mismatched = [key for key, versions in paired_seeds.items() if len(versions) != 1]
    if seed_mismatched:
        errors.append(f"{len(seed_mismatched)} (N,case) groups use more than one split seed")
    if exact_design:
        expected_test = tuple(range(9001, 10001))
        if all_test_splits != {expected_test}:
            errors.append("OOS IDs are not exactly the common fixed set 9001..10000")
        for (n, case), seeds in paired_seeds.items():
            if n is None or not case.startswith("case"):
                continue
            expected_seed = SEED_BASE + int(case[4:]) * 1000 + n
            if seeds != {str(expected_seed)}:
                errors.append(f"split seed mismatch for N={n}, {case}: {sorted(seeds)} != {expected_seed}")
                break

    notes.extend([
        f"manifest_tasks={len(rows)}",
        f"unique_run_ids={len({row.get('run_id', '') for row in rows})}",
        f"optimization_instances={','.join(sorted(instances))}",
        f"paired_instance={EXPECTED_REBURN_INSTANCE}",
        f"methods={len(methods)}",
        f"controlled_blocks={len(block_methods)}",
        f"training_split_groups={len(paired_splits)}",
        f"fixed_oos_versions={len(all_test_splits)}",
    ])
    return errors, notes


def meaningful_method_configuration(row: dict[str, str]) -> dict[str, str]:
    method = clean(row.get("method"))
    projected = "Projected" in method
    combinatorial = "Combinatorial" in method
    root = method.endswith("-RootCuts")
    standard_llbi = "-LLBI-" in method and not projected
    objective = objective_for_method(method)
    family = "coverage" if "ProjectedCoverage" in method else ("path" if "ProjectedPath" in method else "")
    variant = "poly" if "-poly-" in method else ("exp" if "-exp-" in method else "")
    return {
        "method": method,
        "objective_type": objective,
        "risk_measure": clean(row.get("risk_measure")),
        "time_limit_sec": clean(row.get("time_limit")),
        "target_mip_gap": clean(row.get("mip_gap")),
        "threads": clean(row.get("threads")),
        "cvar_beta": clean(row.get("cvar_beta")) if objective != "Expected" else "N/A",
        "cvar_lambda": clean(row.get("cvar_lambda")) if objective == "MeanCVaR" else "N/A",
        "root_cuts_enabled": str(root).lower(),
        "root_user_cut_max_rounds": "1" if root else "N/A",
        "standard_llbi_enabled": str(standard_llbi).lower(),
        "projected_llbi_enabled": str(projected).lower(),
        "projected_family": family or "N/A",
        "projected_variant": variant or "N/A",
        "projected_root_rounds": clean(row.get("projected_llbi_root_rounds")) if projected else "N/A",
        "projected_max_cuts_per_round": clean(row.get("projected_llbi_max_cuts_per_round")) if projected else "N/A",
        "projected_violation_tolerance": clean(row.get("projected_llbi_violation_tolerance")) if projected else "N/A",
        "projected_density_limit": clean(row.get("projected_llbi_cut_density_limit")) if projected else "N/A",
        "projected_poly_max_cuts": clean(row.get("projected_poly_max_cuts")) if variant == "poly" else "N/A",
        "combinatorial_benders_enabled": str(combinatorial).lower(),
        "combinatorial_lift": clean(row.get("combinatorial_benders_lift")) if combinatorial else "N/A",
        "combinatorial_sampling_ratio": clean(row.get("combinatorial_benders_cut_sampling_ratio")) if combinatorial else "N/A",
        "combinatorial_fractional_separation": clean(row.get("combinatorial_benders_separate_fractional")) if combinatorial else "N/A",
        "combinatorial_initial_cuts": clean(row.get("combinatorial_benders_initial_cuts")) if combinatorial else "N/A",
        "combinatorial_scenario_order": clean(row.get("combinatorial_benders_scenario_order")) if combinatorial else "N/A",
    }


def write_configuration(args: argparse.Namespace, manifest: list[dict[str, str]]) -> None:
    first_row = manifest[0]
    ordered: dict[str, dict[str, str]] = {}
    for row in manifest:
        ordered.setdefault(clean(row.get("method")), row)
    method_rows = [meaningful_method_configuration(row) for row in ordered.values()]
    write_csv(args.results_dir / "method_configuration.csv", method_rows, METHOD_CONFIGURATION_FIELDS)

    ns = sorted({integer(row.get("train_count")) for row in manifest if integer(row.get("train_count")) is not None})
    alphas = sorted({normalize_alpha(row.get("alpha")) for row in manifest}, key=float)
    cases = sorted({clean(row.get("case_id")) for row in manifest})
    objectives = sorted({clean(row.get("objective_family")) for row in manifest})
    configuration = [
        ("experiment_id", args.experiment_id),
        ("optimization_instance", EXPECTED_INSTANCE),
        ("paired_evaluation_instance", EXPECTED_REBURN_INSTANCE),
        ("train_counts", ";".join(str(value) for value in ns)),
        ("alphas", ";".join(alphas)),
        ("num_cases", str(len(cases))),
        ("objectives", ";".join(objectives)),
        ("method_count", str(len(ordered))),
        ("expected_runs", str(len(manifest))),
        ("training_pool", f"{first_row.get('training_pool_min', '')}..{first_row.get('training_pool_max', '')}"),
        ("test_pool", f"{first_row.get('test_pool_min', '')}..{first_row.get('test_pool_max', '')}"),
        ("test_count", clean(first_row.get("test_count"))),
        ("seed_base", clean(first_row.get("seed_base"))),
        ("time_limit_sec", clean(first_row.get("time_limit"))),
        ("target_mip_gap", clean(first_row.get("mip_gap"))),
        ("mip_gap_unit", "fraction"),
        ("threads", clean(first_row.get("threads"))),
        ("weight_profile", clean(first_row.get("weight_profile")) or "homogeneous"),
        ("weight_replicates", clean(first_row.get("weight_replicate")) or "0"),
        ("paired_reburn_evaluation", "true"),
        ("successful_full_json_retained", str(args.keep_full_json).lower()),
        ("summary_standard_deviation", "sample (n-1)"),
    ]
    write_csv(
        args.results_dir / "experiment_configuration.csv",
        [{"parameter": key, "value": value} for key, value in configuration],
        ("parameter", "value"),
    )


def load_worker_rows(results_dir: Path, manifest: list[dict[str, str]]) -> tuple[dict[str, dict[str, str]], list[str]]:
    errors: list[str] = []
    expected_tasks = {clean(row.get("task_id")) for row in manifest}
    by_task: dict[str, dict[str, str]] = {}
    workers = sorted({clean(row.get("worker_id")) for row in manifest})
    for worker in workers:
        path = results_dir / "workers" / worker / f"batch_results_{worker}.csv"
        if not path.exists():
            continue
        try:
            rows = read_csv(path)
        except (OSError, csv.Error) as exc:
            errors.append(f"cannot read worker CSV {path}: {exc}")
            continue
        for row in rows:
            task_id = clean(row.get("task_id"))
            if not task_id:
                errors.append(f"worker CSV {path} contains a row without task_id")
                continue
            if task_id not in expected_tasks:
                errors.append(f"worker CSV {path} contains stale/unknown task_id={task_id}")
                continue
            if task_id in by_task:
                errors.append(f"duplicate worker result for task_id={task_id}")
                continue
            by_task[task_id] = row
    return by_task, errors


def solution_ids(manifest_row: dict[str, str], worker_row: dict[str, str], errors: list[str]) -> list[int]:
    solution_path = Path(clean(manifest_row.get("solution_dir"))) / f"{manifest_row.get('task_id', '')}.csv"
    if not solution_path.exists():
        errors.append(f"run_id={manifest_row.get('run_id', '')}: missing solution file {solution_path}")
        return parse_ids(first(worker_row, "selected_firebreak_original_ids", "selected_firebreaks"))
    try:
        ids = parse_ids(solution_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        errors.append(f"run_id={manifest_row.get('run_id', '')}: malformed solution file: {exc}")
        return []
    return ids


def result_row(
    args: argparse.Namespace,
    manifest_row: dict[str, str],
    worker_row: dict[str, str],
    errors: list[str],
) -> tuple[dict[str, str], dict[str, str] | None]:
    run_id = clean(manifest_row.get("run_id"))
    if clean(worker_row.get("run_id")) != run_id:
        errors.append(f"task_id={manifest_row.get('task_id', '')}: worker run_id does not match manifest")
    rc = integer(worker_row.get("worker_return_code"))
    status = first(worker_row, "solver_status", "status")
    objective = number(first(worker_row, "objective_in_sample", "objective_value"))
    has_incumbent = rc == 0 and objective is not None and not status_has_no_incumbent(status)
    reason = first(worker_row, "benders_termination_reason", "termination_reason")
    if not reason:
        if is_optimal(status):
            reason = "optimal"
        elif is_time_limit(status, ""):
            reason = "time_limit"
        elif integer(worker_row.get("solver_status_code")) == 11:
            reason = "time_limit"
        elif rc not in (None, 0):
            reason = ":".join(filter(None, [clean(worker_row.get("failure_stage")), clean(worker_row.get("failure_type"))]))

    n = integer(manifest_row.get("train_count")) or 0
    objective_type = objective_for_method(clean(manifest_row.get("method")))
    cvar_lambda = number(manifest_row.get("cvar_lambda"))
    values = {
        "train_expected": number(first(worker_row, "train_expected_burned_area")),
        "train_cvar": number(first(worker_row, "train_empirical_cvar_burned_area", "train_cvar_burned_area")),
        "train_worst": number(first(worker_row, "train_worst_10pct_burned_area", "train_worst10_burned_area")),
        "oos_expected": number(first(worker_row, "test_expected_burned_area", "oos_expected_burned_area")),
        "oos_cvar": number(first(worker_row, "test_empirical_cvar_burned_area", "test_cvar_burned_area", "oos_cvar_burned_area")),
        "oos_worst": number(first(worker_row, "test_worst_10pct_burned_area", "test_worst10_burned_area", "oos_worst10_burned_area")),
        "reburn_expected": number(first(worker_row, "paired_reburn_train_expected_burned_area", "reburn_expected_burned_area")),
        "reburn_cvar": number(first(worker_row, "paired_reburn_train_empirical_cvar_90pct_burned_area", "reburn_cvar_burned_area")),
        "reburn_worst": number(first(worker_row, "paired_reburn_train_worst_10pct_burned_area", "reburn_worst10_burned_area")),
    }
    train_eval = evaluated_objective(objective_type, values["train_expected"], values["train_cvar"], cvar_lambda)
    oos_eval = evaluated_objective(objective_type, values["oos_expected"], values["oos_cvar"], cvar_lambda)
    reburn_eval = evaluated_objective(objective_type, values["reburn_expected"], values["reburn_cvar"], cvar_lambda)

    selected: list[int] = []
    solution: dict[str, str] | None = None
    abs_diff: float | None = None
    rel_diff: float | None = None
    validation_passed = False
    if has_incumbent:
        missing_metrics = [name for name, value in values.items() if value is None]
        if missing_metrics:
            errors.append(f"run_id={run_id}: incumbent is missing evaluations: {','.join(missing_metrics)}")
        if clean(worker_row.get("paired_reburn_status")) != "ok":
            errors.append(f"run_id={run_id}: paired reburn evaluation is not ok")
        if clean(worker_row.get("paired_reburn_instance_resolved")) != EXPECTED_REBURN_INSTANCE:
            errors.append(f"run_id={run_id}: paired reburn instance did not resolve correctly")
        paired_count = integer(worker_row.get("paired_reburn_train_scenario_count"))
        if paired_count != n:
            errors.append(f"run_id={run_id}: paired reburn evaluated {paired_count} scenarios; expected {n}")
        if integer(worker_row.get("paired_selected_firebreaks_missing")) != 0:
            errors.append(f"run_id={run_id}: selected firebreak mapping to reburn is incomplete")

        selected = solution_ids(manifest_row, worker_row, errors)
        if len(selected) != len(set(selected)):
            errors.append(f"run_id={run_id}: solution contains duplicate Cell2Fire IDs")
        reported_selected = parse_ids(first(
            worker_row, "selected_firebreak_original_ids", "selected_firebreaks"))
        if reported_selected and sorted(reported_selected) != sorted(selected):
            errors.append(f"run_id={run_id}: worker-selected IDs do not match the solution file")
        declared_cells = integer(manifest_row.get("declared_cells")) or 400
        invalid_ids = [cell for cell in selected if cell < 1 or cell > declared_cells]
        if invalid_ids:
            errors.append(f"run_id={run_id}: solution contains ineligible IDs {invalid_ids[:10]}")
        budget = math.floor((number(manifest_row.get("alpha")) or 0.0) * declared_cells + 1.0e-12)
        if len(selected) > budget:
            errors.append(f"run_id={run_id}: selected {len(selected)} firebreaks, budget is {budget}")
        mapped = integer(worker_row.get("paired_selected_firebreaks_mapped"))
        if mapped != len(selected):
            errors.append(f"run_id={run_id}: reburn mapped count {mapped} != selected count {len(selected)}")

        if objective is not None and train_eval is not None:
            abs_diff = abs(objective - train_eval)
            rel_diff = abs_diff / max(1.0, abs(train_eval))
            validation_passed = abs_diff <= VALIDATION_ABS_TOL or rel_diff <= VALIDATION_REL_TOL
        if not validation_passed:
            errors.append(
                f"run_id={run_id}: solver/train objective discrepancy exceeds tolerances "
                f"(abs={fmt(abs_diff)}, rel={fmt(rel_diff)})")
        raw_validation = clean(worker_row.get("objective_validation_passed"))
        if raw_validation and not truthy(raw_validation):
            errors.append(f"run_id={run_id}: solver objective_validation_passed=false")

        solution = {
            "run_id": run_id,
            "instance_id": EXPECTED_INSTANCE,
            "case_id": clean(manifest_row.get("case_id")),
            "N": str(n),
            "alpha": normalize_alpha(manifest_row.get("alpha")),
            "objective_type": objective_type,
            "method": clean(manifest_row.get("method")),
            "selected_firebreak_count": str(len(selected)),
            "selected_firebreak_ids": ";".join(str(value) for value in sorted(selected)),
        }

    paired_ok = clean(worker_row.get("paired_reburn_status")) == "ok"
    if rc not in (None, 0):
        execution_status = "failed"
        if clean(worker_row.get("worker_status")).lower() != "failed":
            errors.append(f"run_id={run_id}: nonzero worker return code lacks a clean failure status")
        if not first(worker_row, "failure_stage", "failure_type"):
            errors.append(f"run_id={run_id}: failed row lacks failure diagnostics")
    elif not has_incumbent:
        execution_status = "no_incumbent"
    elif not paired_ok:
        execution_status = "incomplete_paired_evaluation"
    else:
        execution_status = "completed"

    upper = objective if has_incumbent else None
    lower = number(first(worker_row, "best_bound", "lower_bound"))
    if rc not in (None, 0):
        lower = None
    solver_runtime = number(first(worker_row, "runtime_seconds", "runtime_sec"))
    row = {
        "experiment_id": args.experiment_id,
        "run_id": run_id,
        "case_id": clean(manifest_row.get("case_id")),
        "seed": clean(manifest_row.get("seed")),
        "instance_id": EXPECTED_INSTANCE,
        "N": str(n),
        "alpha": normalize_alpha(manifest_row.get("alpha")),
        "objective_type": objective_type,
        "risk_measure": clean(manifest_row.get("risk_measure")),
        "cvar_beta": clean(manifest_row.get("cvar_beta")),
        "cvar_lambda": clean(manifest_row.get("cvar_lambda")),
        "method": clean(manifest_row.get("method")),
        "time_limit_sec": clean(manifest_row.get("time_limit")),
        "threads": clean(manifest_row.get("threads")),
        "target_mip_gap": clean(manifest_row.get("mip_gap")),
        "solver_status": status,
        "termination_reason": reason,
        "has_incumbent": str(has_incumbent).lower(),
        "objective_value": fmt(upper),
        "upper_bound": fmt(upper),
        "lower_bound": fmt(lower),
        "mip_gap": fmt(number(first(worker_row, "solver_mip_gap", "mip_gap")) if has_incumbent else None),
        "runtime_sec": fmt(solver_runtime),
        "wall_time_sec": fmt(number(first(worker_row, "worker_runtime_seconds", "wall_time_sec"))),
        "train_expected_burned_area": fmt(values["train_expected"] if has_incumbent else None),
        "train_cvar_burned_area": fmt(values["train_cvar"] if has_incumbent else None),
        "train_worst10_burned_area": fmt(values["train_worst"] if has_incumbent else None),
        "train_eval_objective_value": fmt(train_eval if has_incumbent else None),
        "oos_expected_burned_area": fmt(values["oos_expected"] if has_incumbent else None),
        "oos_cvar_burned_area": fmt(values["oos_cvar"] if has_incumbent else None),
        "oos_worst10_burned_area": fmt(values["oos_worst"] if has_incumbent else None),
        "oos_eval_objective_value": fmt(oos_eval if has_incumbent else None),
        "reburn_expected_burned_area": fmt(values["reburn_expected"] if has_incumbent else None),
        "reburn_cvar_burned_area": fmt(values["reburn_cvar"] if has_incumbent else None),
        "reburn_worst10_burned_area": fmt(values["reburn_worst"] if has_incumbent else None),
        "reburn_eval_objective_value": fmt(reburn_eval if has_incumbent else None),
        "selected_firebreak_count": str(len(selected)) if has_incumbent else "",
        "train_objective_abs_discrepancy": fmt(abs_diff),
        "train_objective_rel_discrepancy": fmt(rel_diff),
        "objective_validation_passed": str(validation_passed).lower() if has_incumbent else "",
        "execution_status": execution_status,
        "attempt": clean(worker_row.get("attempt")) or "1",
    }
    return row, solution


def finite_values(rows: list[dict[str, str]], field: str) -> list[float]:
    return [value for value in (number(row.get(field)) for row in rows) if value is not None]


def mean(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def sample_stddev(values: list[float]) -> float | None:
    if not values:
        return None
    return statistics.stdev(values) if len(values) > 1 else 0.0


def build_summary(manifest: list[dict[str, str]], rows: list[dict[str, str]]) -> list[dict[str, str]]:
    expected: Counter[tuple[str, str, str, str]] = Counter()
    observed: dict[tuple[str, str, str, str], list[dict[str, str]]] = defaultdict(list)
    for row in manifest:
        key = (
            clean(row.get("train_count")), normalize_alpha(row.get("alpha")),
            objective_for_method(clean(row.get("method"))), clean(row.get("method")),
        )
        expected[key] += 1
    for row in rows:
        observed[(row["N"], row["alpha"], row["objective_type"], row["method"])].append(row)

    output: list[dict[str, str]] = []
    for key in sorted(expected, key=lambda item: (int(item[0]), float(item[1]), item[2], item[3])):
        group = observed.get(key, [])
        incumbent = [row for row in group if truthy(row.get("has_incumbent"))]
        optimal = [row for row in incumbent if is_optimal(row.get("solver_status", ""))]
        timed = [row for row in incumbent if is_time_limit(
            row.get("solver_status", ""), row.get("termination_reason", ""))]
        failed = [row for row in group if not truthy(row.get("has_incumbent"))
                  or row.get("execution_status") != "completed"]
        denominator = expected[key]
        metric = lambda field: finite_values(incumbent, field)  # noqa: E731
        all_metric = lambda field: finite_values(group, field)  # noqa: E731
        n, alpha, objective, method = key
        output.append({
            "N": n,
            "alpha": alpha,
            "objective_type": objective,
            "method": method,
            "expected_runs": str(expected[key]),
            "observed_runs": str(len(group)),
            "runs_with_incumbent": str(len(incumbent)),
            "runs_solved_to_proven_optimality": str(len(optimal)),
            "runs_stopped_by_time_limit_with_incumbent": str(len(timed)),
            "failed_no_incumbent_runs": str(len(failed)),
            "optimality_rate": fmt(len(optimal) / denominator if denominator else None),
            "time_limit_rate": fmt(len(timed) / denominator if denominator else None),
            "mean_runtime_sec": fmt(mean(all_metric("runtime_sec"))),
            "median_runtime_sec": fmt(median(all_metric("runtime_sec"))),
            "stddev_runtime_sec": fmt(sample_stddev(all_metric("runtime_sec"))),
            "mean_final_mip_gap": fmt(mean(metric("mip_gap"))),
            "median_final_mip_gap": fmt(median(metric("mip_gap"))),
            "mip_gap_unit": "fraction",
            "mean_upper_bound": fmt(mean(metric("upper_bound"))),
            "mean_lower_bound": fmt(mean(all_metric("lower_bound"))),
            "mean_train_objective_value": fmt(mean(metric("train_eval_objective_value"))),
            "stddev_train_objective_value": fmt(sample_stddev(metric("train_eval_objective_value"))),
            "mean_oos_objective_value": fmt(mean(metric("oos_eval_objective_value"))),
            "stddev_oos_objective_value": fmt(sample_stddev(metric("oos_eval_objective_value"))),
            "mean_reburn_objective_value": fmt(mean(metric("reburn_eval_objective_value"))),
            "stddev_reburn_objective_value": fmt(sample_stddev(metric("reburn_eval_objective_value"))),
            "mean_oos_expected_burned_area": fmt(mean(metric("oos_expected_burned_area"))),
            "mean_oos_cvar": fmt(mean(metric("oos_cvar_burned_area"))),
            "mean_reburn_expected_burned_area": fmt(mean(metric("reburn_expected_burned_area"))),
            "mean_reburn_cvar": fmt(mean(metric("reburn_cvar_burned_area"))),
        })
    return output


def paired_difference_row(label: str, lhs: dict[str, str], rhs: dict[str, str]) -> dict[str, str]:
    out = {
        "comparison": label,
        "N": lhs["N"],
        "alpha": lhs["alpha"],
        "objective_type": lhs["objective_type"],
        "case_id": lhs["case_id"],
        "lhs_run_id": lhs["run_id"],
        "rhs_run_id": rhs["run_id"],
        "lhs_method": lhs["method"],
        "rhs_method": rhs["method"],
    }
    for field in ("runtime_sec", "upper_bound", "mip_gap", "train_eval_objective_value",
                  "oos_eval_objective_value", "reburn_eval_objective_value"):
        left = number(lhs.get(field))
        right = number(rhs.get(field))
        out[f"{field}_lhs_minus_rhs"] = fmt(left - right if left is not None and right is not None else None)
    return out


def build_comparisons(rows: list[dict[str, str]]) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    eligible = [row for row in rows if row.get("execution_status") == "completed"]
    by_block: dict[tuple[str, str, str, str], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in eligible:
        by_block[(row["N"], row["alpha"], row["objective_type"], row["case_id"])][row["method"]] = row

    saa_rows: list[dict[str, str]] = []
    projected_rows: list[dict[str, str]] = []
    projected_pairs = (
        ("coverage_poly_vs_coverage_exp", "ProjectedCoverageLLBI-poly", "ProjectedCoverageLLBI-exp"),
        ("path_poly_vs_path_exp", "ProjectedPathLLBI-poly", "ProjectedPathLLBI-exp"),
        ("coverage_poly_vs_path_poly", "ProjectedCoverageLLBI-poly", "ProjectedPathLLBI-poly"),
        ("coverage_exp_vs_path_exp", "ProjectedCoverageLLBI-exp", "ProjectedPathLLBI-exp"),
    )
    for (_n, _alpha, objective, _case), indexed in sorted(by_block.items()):
        baseline = {
            "Expected": "FPP-SAA",
            "CVaR": "FPP-SAA-CVaR",
            "MeanCVaR": "FPP-SAA-MeanCVaR",
        }[objective]
        if baseline in indexed:
            for method, row in indexed.items():
                if method != baseline:
                    saa_rows.append(paired_difference_row("method_vs_fpp_saa", row, indexed[baseline]))
        for label, lhs_token, rhs_token in projected_pairs:
            lhs = next((row for method, row in indexed.items() if lhs_token in method), None)
            rhs = next((row for method, row in indexed.items() if rhs_token in method), None)
            if lhs is not None and rhs is not None:
                projected_rows.append(paired_difference_row(label, lhs, rhs))
    return saa_rows, projected_rows


def comparison_fields(rows: list[dict[str, str]]) -> list[str]:
    fields = [
        "comparison", "N", "alpha", "objective_type", "case_id",
        "lhs_run_id", "rhs_run_id", "lhs_method", "rhs_method",
    ]
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)
    return fields


def write_validation_report(path: Path, *, errors: list[str], notes: list[str],
                            allow_partial: bool, manifest_only: bool) -> None:
    status = "PASS" if not errors else "FAIL"
    lines = [
        f"validation_status={status}",
        f"campaign_complete={str(not errors and not allow_partial and not manifest_only).lower()}",
        f"allow_partial={str(allow_partial).lower()}",
        f"manifest_only={str(manifest_only).lower()}",
        f"errors={len(errors)}",
        *notes,
    ]
    if errors:
        lines.extend(["", "Validation errors:", *[f"- {message}" for message in errors]])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    manifest_path = args.results_dir / "manifests" / "full_task_manifest.csv"
    if not manifest_path.exists():
        raise SystemExit(f"Missing manifest: {manifest_path}")
    manifest = read_csv(manifest_path)
    exact_design = not args.allow_nonstandard_design
    errors, notes = validate_manifest(manifest, exact_design=exact_design)
    write_configuration(args, manifest)

    if args.manifest_only:
        report = args.results_dir / "manuscript_validation_report.txt"
        write_validation_report(report, errors=errors, notes=notes, allow_partial=False, manifest_only=True)
        if errors:
            raise SystemExit(f"Manifest validation failed; see {report}")
        print(f"Validated {len(manifest)} unique manifest tasks; zero reburn optimization tasks.")
        print(f"Wrote experiment_configuration.csv and method_configuration.csv under {args.results_dir}")
        return 0

    worker_rows, worker_errors = load_worker_rows(args.results_dir, manifest)
    errors.extend(worker_errors)
    missing = [row for row in manifest if clean(row.get("task_id")) not in worker_rows]
    if missing and not args.allow_partial:
        errors.append(f"missing {len(missing)} expected worker result rows")
    elif missing:
        notes.append(f"partial_missing_rows={len(missing)}")

    manuscript_rows: list[dict[str, str]] = []
    solutions: list[dict[str, str]] = []
    for manifest_row in manifest:
        task_id = clean(manifest_row.get("task_id"))
        worker_row = worker_rows.get(task_id)
        if worker_row is None:
            continue
        row, solution = result_row(args, manifest_row, worker_row, errors)
        manuscript_rows.append(row)
        if solution is not None:
            solutions.append(solution)

    if len({row["run_id"] for row in manuscript_rows}) != len(manuscript_rows):
        errors.append("manuscript rows contain duplicate logical run_id values")
    notes.extend([
        f"observed_worker_rows={len(manuscript_rows)}",
        f"incumbent_solutions={len(solutions)}",
    ])

    manuscript_rows.sort(key=lambda row: (
        int(row["N"]), float(row["alpha"]), row["case_id"],
        (0, EXPECTED_METHODS.index(row["method"]))
        if row["method"] in EXPECTED_METHODS else (1, row["method"]),
    ))
    solutions.sort(key=lambda row: row["run_id"])
    write_csv(args.results_dir / "manuscript_results.csv", manuscript_rows, MANUSCRIPT_FIELDS)
    write_csv(args.results_dir / "solutions.csv", solutions, SOLUTION_FIELDS)

    report = args.results_dir / "manuscript_validation_report.txt"
    write_validation_report(
        report, errors=errors, notes=notes, allow_partial=args.allow_partial, manifest_only=False)
    if errors:
        raise SystemExit(f"Campaign validation failed; see {report}")

    summary = build_summary(manifest, manuscript_rows)
    write_csv(args.results_dir / "manuscript_summary.csv", summary, SUMMARY_FIELDS)
    saa_comparisons, projected_comparisons = build_comparisons(manuscript_rows)
    write_csv(
        args.results_dir / "method_vs_fpp_saa.csv",
        saa_comparisons,
        comparison_fields(saa_comparisons),
    )
    write_csv(
        args.results_dir / "projected_llbi_paired_comparisons.csv",
        projected_comparisons,
        comparison_fields(projected_comparisons),
    )
    print(f"Wrote {len(manuscript_rows)} manuscript rows and {len(solutions)} solutions.")
    print(f"Wrote {len(summary)} N x alpha x objective x method summary rows.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
