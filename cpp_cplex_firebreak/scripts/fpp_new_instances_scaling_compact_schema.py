#!/usr/bin/env python3
"""Compact CSV schema for the FPP new_instances scaling experiment."""

from __future__ import annotations

import csv
import math
from pathlib import Path


COMPACT_CSV_FIELDS = [
    "experiment_id",
    "run_id",
    "case_id",
    "landscape",
    "instance_name",
    "instance_group",
    "method",
    "objective_type",
    "risk_measure",
    "alpha",
    "cvar_beta",
    "cvar_lambda",
    "seed",
    "train_count",
    "test_count",
    "train_graph_rt_ratio",
    "train_graph_adag_ratio",
    "train_graph_gdg_ratio",
    "train_graph_nfr_ratio",
    "train_graph_empty_ratio",
    "test_graph_rt_ratio",
    "test_graph_adag_ratio",
    "test_graph_gdg_ratio",
    "test_graph_nfr_ratio",
    "test_graph_empty_ratio",
    "instance_graph_rt_ratio",
    "instance_graph_adag_ratio",
    "instance_graph_gdg_ratio",
    "instance_graph_nfr_ratio",
    "instance_graph_empty_ratio",
    "time_limit_sec",
    "threads",
    "solver_status",
    "termination_reason",
    "has_incumbent",
    "objective_value",
    "best_bound",
    "mip_gap",
    "runtime_sec",
    "wall_time_sec",
    "num_nodes",
    "num_iterations",
    "lp_relaxation_value",
    "lp_relaxation_value_initial",
    "lp_relaxation_value_final",
    "lp_relaxation_improvement_abs",
    "lp_relaxation_improvement_pct",
    "uses_llbi",
    "uses_root_cuts",
    "uses_coverage_llbi",
    "uses_path_llbi",
    "uses_projected_coverage_llbi",
    "uses_projected_path_llbi",
    "uses_projected_llbi_poly",
    "uses_projected_llbi_exp",
    "uses_combinatorial_benders",
    "combinatorial_benders_scenario_order",
    "uses_global_dominance",
    "uses_conditional_zero_benefit_fixing",
    "benders_lazy_cuts_added",
    "root_user_cuts_added",
    "llbi_cuts_added",
    "coverage_llbi_cuts_added",
    "path_llbi_cuts_added",
    "projected_llbi_cuts_added",
    "projected_llbi_total_nonzeros",
    "combinatorial_benders_cuts_added",
    "dominator_cuts_added",
    "separator_cuts_added",
    "global_dominance_candidates_removed",
    "conditional_zero_benefit_fixings_attempted",
    "conditional_zero_benefit_fixings_applied",
    "num_variables",
    "num_constraints",
    "num_binary_variables",
    "num_continuous_variables",
    "train_expected_burned_area",
    "test_expected_burned_area",
    "train_cvar_burned_area",
    "test_cvar_burned_area",
    "train_worst10_burned_area",
    "test_worst10_burned_area",
    "train_eval_runtime_sec",
    "test_eval_runtime_sec",
    "paired_reburn_train_expected_burned_area",
    "paired_reburn_train_cvar_burned_area",
    "paired_reburn_train_worst10_burned_area",
    "paired_reburn_train_eval_runtime_sec",
]


MISSING_TEXT = {"", "nan", "na", "n/a", "none", "null"}
GRAPH_RATIO_CODES = ("rt", "adag", "gdg", "nfr", "empty")
GRAPH_RATIO_KEY_ALIASES = {
    "rt": "rt",
    "rooted_arborescence": "rt",
    "adag": "adag",
    "dag_not_tree": "adag",
    "acyclic_dag_not_tree": "adag",
    "gdg": "gdg",
    "general_directed_graph": "gdg",
    "nfr": "nfr",
    "not_fully_reachable_from_ignition": "nfr",
    "empty": "empty",
    "empty_or_invalid": "empty",
}


def is_missing(value: object) -> bool:
    return value is None or str(value).strip().lower() in MISSING_TEXT


def first_value(row: dict[str, str], *names: str) -> str:
    for name in names:
        value = row.get(name)
        if not is_missing(value):
            return str(value).strip()
    return ""


def bool_value(value: object) -> bool:
    return str(value or "").strip().lower() in {"1", "true", "yes", "on"}


def bool01(value: bool) -> str:
    return "1" if value else "0"


def any_bool(row: dict[str, str], *names: str) -> bool:
    return any(bool_value(row.get(name)) for name in names)


def method_tokens(row: dict[str, str]) -> set[str]:
    return set(first_value(row, "method").split("-"))


def objective_type(row: dict[str, str]) -> str:
    source = first_value(row, "objective_type", "objective_family", "risk_measure", "method").lower()
    method = first_value(row, "method")
    if "meancvar" in source or "mean-cvar" in source or "MeanCVaR" in method:
        return "mean_cvar"
    if "cvar" in source or "CVaR" in method:
        return "cvar"
    return "expected"


def numeric_or_blank(value: object) -> str:
    if is_missing(value):
        return ""
    text = str(value).strip()
    try:
        number = float(text)
    except ValueError:
        return text
    if not math.isfinite(number):
        return ""
    return text


def parse_graph_ratio_summary(value: object) -> dict[str, str]:
    if is_missing(value):
        return {}
    ratios: dict[str, str] = {}
    text = str(value).strip()
    for part in text.replace(",", ";").split(";"):
        if not part.strip():
            continue
        if "=" in part:
            raw_key, raw_value = part.split("=", 1)
        elif ":" in part:
            raw_key, raw_value = part.split(":", 1)
        else:
            continue
        key = GRAPH_RATIO_KEY_ALIASES.get(raw_key.strip().lower())
        if key is None:
            continue
        parsed_value = numeric_or_blank(raw_value)
        if parsed_value:
            ratios[key] = parsed_value
    return ratios


def graph_ratio_columns(prefix: str, ratios: dict[str, str]) -> dict[str, str]:
    return {f"{prefix}_graph_{code}_ratio": ratios.get(code, "") for code in GRAPH_RATIO_CODES}


def nonnegative_or_blank(value: object) -> str:
    text = numeric_or_blank(value)
    if not text:
        return ""
    try:
        return text if float(text) >= 0.0 else ""
    except ValueError:
        return text


def cut_count(row: dict[str, str], names: tuple[str, ...], active: bool) -> str:
    value = first_value(row, *names)
    if value:
        return numeric_or_blank(value)
    return "" if active else "0"


def sum_counts(row: dict[str, str], names: tuple[str, ...], active: bool) -> str:
    total = 0
    seen = False
    for name in names:
        value = first_value(row, name)
        if not value:
            continue
        try:
            total += int(float(value))
            seen = True
        except ValueError:
            return ""
    if seen:
        return str(total)
    return "" if active else "0"


def projected_family(row: dict[str, str]) -> str:
    value = first_value(row, "projected_llbi_family").lower()
    method = first_value(row, "method")
    if value in {"coverage", "path"}:
        return value
    if "ProjectedCoverageLLBI" in method:
        return "coverage"
    if "ProjectedPathLLBI" in method:
        return "path"
    return "none"


def projected_variant(row: dict[str, str]) -> str:
    value = first_value(row, "projected_llbi_variant", "projected_llbi_strategy").lower()
    method = first_value(row, "method")
    if value in {"poly", "exp"}:
        return value
    if "-poly" in method:
        return "poly"
    if "-exp" in method:
        return "exp"
    return "none"


def compact_csv_path_for(combined_csv: Path, results_dir: Path, allow_partial: bool = False) -> Path:
    if combined_csv.name == "batch_results_all.csv":
        return results_dir / "batch_results_compact.csv"
    if combined_csv.name == "batch_results_partial.csv":
        return results_dir / "batch_results_partial_compact.csv"
    return combined_csv.with_name(f"{combined_csv.stem}_compact{combined_csv.suffix}")


def default_analysis_input_csv(results_dir: Path, allow_partial: bool = False) -> Path:
    full_csv = results_dir / ("batch_results_partial.csv" if allow_partial else "batch_results_all.csv")
    compact_csv = compact_csv_path_for(full_csv, results_dir, allow_partial)
    return compact_csv if compact_csv.exists() else full_csv


def compact_row(row: dict[str, str], *, default_experiment_id: str = "") -> dict[str, str]:
    method = first_value(row, "method")
    tokens = method_tokens(row)
    family = projected_family(row)
    variant = projected_variant(row)

    uses_standard_llbi = (
        any_bool(row, "use_lifted_lower_bounds", "benders_use_lifted_lower_bounds")
        or "LLBI" in tokens
    )
    uses_projected_coverage = (
        any_bool(row, "use_projected_coverage_llbi_poly", "use_projected_coverage_llbi_exp")
        or any_bool(row, "projected_coverage_llbi_enabled")
        or family == "coverage"
    )
    uses_projected_path = (
        any_bool(row, "use_projected_path_llbi_poly", "use_projected_path_llbi_exp")
        or any_bool(row, "projected_path_llbi_enabled")
        or family == "path"
    )
    uses_coverage = (
        any_bool(row, "coverage_llbi_enabled")
        or uses_projected_coverage
        or "CoverageLLBI" in method
    )
    uses_path = (
        any_bool(row, "path_llbi_enabled")
        or uses_projected_path
        or "PathLLBI" in method
    )
    uses_root = (
        any_bool(row, "use_root_user_cuts", "branch_benders_use_root_user_cuts")
        or method.endswith("-RootCuts")
    )
    uses_combinatorial = (
        any_bool(row, "use_combinatorial_benders", "combinatorial_benders_enabled")
        or "Combinatorial" in method
    )
    uses_global_dominance = any_bool(row, "global_dominance_enabled")
    uses_conditional_fixing = any_bool(row, "conditional_zero_benefit_enabled")
    uses_projected_poly = variant == "poly"
    uses_projected_exp = variant == "exp"
    uses_projected = uses_projected_coverage or uses_projected_path
    scenario_order = first_value(row, "combinatorial_benders_scenario_order")
    if not scenario_order and uses_combinatorial:
        scenario_order = "eta-desc" if method.endswith("-EtaDesc") else "eta-asc"

    objective = first_value(row, "objective_value", "objective_in_sample")
    solver_status = first_value(row, "solver_status", "status")
    status_lower = solver_status.lower()
    has_incumbent = bool(objective) and not any(
        token in status_lower for token in ("infeasible", "unbounded", "no feasible")
    )

    # Root LP values are only recorded by the projected LLBI root-cut loop today.
    # For non-projected methods these stay blank; best_bound is a final MIP bound
    # and is deliberately not used as an LP-relaxation proxy.
    lp_initial = first_value(
        row,
        "lp_relaxation_value_initial",
        "root_lp_bound_initial",
        "root_bound_initial",
        "projected_llbi_root_bound_initial",
    )
    lp_final = first_value(
        row,
        "lp_relaxation_value_final",
        "root_lp_bound_final",
        "root_bound_final",
        "projected_llbi_root_bound_final",
    )
    lp_single = first_value(row, "lp_relaxation_value", "root_lp_bound", "root_bound", "root_relaxation_value")
    if not lp_single and lp_initial and not lp_final:
        lp_single = lp_initial
    elif not lp_single and lp_initial and lp_final:
        lp_single = lp_initial

    combinatorial_direct = first_value(row, "combinatorial_benders_cuts_added")
    combinatorial_total = combinatorial_direct or sum_counts(
        row,
        (
            "combinatorial_benders_integer_cuts_added",
            "combinatorial_benders_fractional_cuts_added",
            "combinatorial_benders_initial_cuts_added",
        ),
        uses_combinatorial,
    )

    train_graph_ratios = parse_graph_ratio_summary(first_value(row, "train_graph_classification_ratios"))
    test_graph_ratios = parse_graph_ratio_summary(first_value(row, "test_graph_classification_ratios"))
    instance_graph_ratios = parse_graph_ratio_summary(first_value(row, "instance_graph_classification_ratios"))
    if not instance_graph_ratios:
        # The new-instances optimizer is built from the training scenario set;
        # keep an instance-level alias for analysis while omitting scenario-level
        # graph labels and scenario IDs from the compact CSV.
        instance_graph_ratios = train_graph_ratios or test_graph_ratios

    out = {
        "experiment_id": first_value(row, "experiment_id") or default_experiment_id,
        "run_id": first_value(row, "run_id"),
        "case_id": first_value(row, "case_id"),
        "landscape": first_value(row, "landscape"),
        "instance_name": first_value(row, "instance_name", "instance_id", "landscape", "folder_name"),
        "instance_group": first_value(row, "instance_group", "instance_type", "folder_name"),
        "method": method,
        "objective_type": objective_type(row),
        "risk_measure": first_value(row, "risk_measure"),
        "alpha": first_value(row, "alpha"),
        "cvar_beta": first_value(row, "cvar_beta"),
        "cvar_lambda": first_value(row, "cvar_lambda"),
        "seed": first_value(row, "seed", "split_seed"),
        "train_count": first_value(row, "train_count", "train_scenario_count"),
        "test_count": first_value(row, "test_count", "test_scenario_count"),
        "time_limit_sec": first_value(row, "time_limit_sec", "time_limit_seconds", "time_limit"),
        "threads": first_value(row, "threads"),
        "solver_status": solver_status,
        "termination_reason": first_value(row, "termination_reason", "benders_termination_reason"),
        "has_incumbent": bool01(has_incumbent),
        "objective_value": numeric_or_blank(objective),
        "best_bound": numeric_or_blank(first_value(row, "best_bound")),
        "mip_gap": numeric_or_blank(first_value(row, "mip_gap", "solver_mip_gap")),
        "runtime_sec": numeric_or_blank(first_value(row, "runtime_sec", "runtime_seconds")),
        "wall_time_sec": numeric_or_blank(first_value(row, "wall_time_sec", "worker_runtime_seconds")),
        "num_nodes": nonnegative_or_blank(first_value(row, "num_nodes", "explored_nodes")),
        "num_iterations": nonnegative_or_blank(first_value(row, "num_iterations", "solver_iterations", "iterations")),
        "lp_relaxation_value": numeric_or_blank(lp_single),
        "lp_relaxation_value_initial": numeric_or_blank(lp_initial),
        "lp_relaxation_value_final": numeric_or_blank(lp_final),
        "lp_relaxation_improvement_abs": numeric_or_blank(first_value(
            row, "lp_relaxation_improvement_abs", "projected_llbi_root_bound_improvement_abs")),
        "lp_relaxation_improvement_pct": numeric_or_blank(first_value(
            row, "lp_relaxation_improvement_pct", "projected_llbi_root_bound_improvement_pct")),
        "uses_llbi": bool01(uses_standard_llbi or uses_coverage or uses_path or uses_projected),
        "uses_root_cuts": bool01(uses_root),
        "uses_coverage_llbi": bool01(uses_coverage),
        "uses_path_llbi": bool01(uses_path),
        "uses_projected_coverage_llbi": bool01(uses_projected_coverage),
        "uses_projected_path_llbi": bool01(uses_projected_path),
        "uses_projected_llbi_poly": bool01(uses_projected_poly),
        "uses_projected_llbi_exp": bool01(uses_projected_exp),
        "uses_combinatorial_benders": bool01(uses_combinatorial),
        "combinatorial_benders_scenario_order": scenario_order if uses_combinatorial else "",
        "uses_global_dominance": bool01(uses_global_dominance),
        "uses_conditional_zero_benefit_fixing": bool01(uses_conditional_fixing),
        "benders_lazy_cuts_added": cut_count(
            row, ("benders_lazy_cuts_added", "branch_benders_lazy_cuts_added"), "Branch-Benders" in method),
        "root_user_cuts_added": cut_count(
            row, ("root_user_cuts_added", "branch_benders_root_user_cuts_added"), uses_root),
        "llbi_cuts_added": cut_count(
            row, ("llbi_cuts_added", "lifted_lower_bound_cuts_added", "benders_lifted_lower_bound_count"),
            uses_standard_llbi),
        "coverage_llbi_cuts_added": cut_count(
            row, ("coverage_llbi_cuts_added", "projected_llbi_coverage_cuts_added"), uses_coverage),
        "path_llbi_cuts_added": cut_count(
            row, ("path_llbi_cuts_added", "projected_llbi_path_cuts_added"), uses_path),
        "projected_llbi_cuts_added": cut_count(
            row, ("projected_llbi_cuts_added",), uses_projected),
        "projected_llbi_total_nonzeros": cut_count(
            row, ("projected_llbi_total_nonzeros",), uses_projected),
        "combinatorial_benders_cuts_added": numeric_or_blank(combinatorial_total),
        "dominator_cuts_added": cut_count(
            row, ("dominator_cuts_added", "dominator_total_cuts"), any_bool(row, "dominator_cuts_enabled")),
        "separator_cuts_added": cut_count(
            row, ("separator_cuts_added",), any_bool(row, "separator_cuts_enabled")),
        "global_dominance_candidates_removed": cut_count(
            row, ("global_dominance_candidates_removed",), uses_global_dominance),
        "conditional_zero_benefit_fixings_attempted": cut_count(
            row, ("conditional_zero_benefit_fixings_attempted",), uses_conditional_fixing),
        "conditional_zero_benefit_fixings_applied": cut_count(
            row, ("conditional_zero_benefit_fixings_applied",), uses_conditional_fixing),
        "num_variables": nonnegative_or_blank(first_value(row, "num_variables")),
        "num_constraints": nonnegative_or_blank(first_value(row, "num_constraints")),
        "num_binary_variables": nonnegative_or_blank(first_value(row, "num_binary_variables")),
        "num_continuous_variables": nonnegative_or_blank(first_value(row, "num_continuous_variables")),
        "train_expected_burned_area": numeric_or_blank(first_value(row, "train_expected_burned_area")),
        "test_expected_burned_area": numeric_or_blank(first_value(row, "test_expected_burned_area")),
        "train_cvar_burned_area": numeric_or_blank(first_value(
            row, "train_cvar_burned_area", "train_empirical_cvar_burned_area")),
        "test_cvar_burned_area": numeric_or_blank(first_value(
            row, "test_cvar_burned_area", "test_empirical_cvar_burned_area")),
        "train_worst10_burned_area": numeric_or_blank(first_value(
            row, "train_worst10_burned_area", "train_worst_10pct_burned_area")),
        "test_worst10_burned_area": numeric_or_blank(first_value(
            row, "test_worst10_burned_area", "test_worst_10pct_burned_area")),
        "train_eval_runtime_sec": numeric_or_blank(first_value(
            row, "train_eval_runtime_sec", "train_evaluation_runtime_seconds")),
        "test_eval_runtime_sec": numeric_or_blank(first_value(
            row, "test_eval_runtime_sec", "test_evaluation_runtime_seconds")),
    }
    out.update(graph_ratio_columns("train", train_graph_ratios))
    out.update(graph_ratio_columns("test", test_graph_ratios))
    out.update(graph_ratio_columns("instance", instance_graph_ratios))
    return out


def write_compact_csv(path: Path, rows: list[dict[str, str]], *, default_experiment_id: str = "") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    compact_rows = [compact_row(row, default_experiment_id=default_experiment_id) for row in rows]
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=COMPACT_CSV_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(compact_rows)
