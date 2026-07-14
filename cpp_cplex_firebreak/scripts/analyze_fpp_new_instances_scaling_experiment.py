#!/usr/bin/env python3
"""Analyze the FPP new_instances scaling experiment outputs."""

from __future__ import annotations

import argparse
import csv
import math
import os
from collections import Counter, defaultdict
from pathlib import Path

from fpp_new_instances_scaling_compact_schema import default_analysis_input_csv


DEFAULT_RESULTS_DIR = "results/batch/fpp_new_instances_scaling"


def is_missing(value):
    return value is None or value == "" or str(value).lower() in {"nan", "na", "none"}


def as_float(value):
    if is_missing(value):
        return math.nan
    try:
        return float(value)
    except ValueError:
        return math.nan


def as_int(value):
    if is_missing(value):
        return 0
    try:
        return int(float(value))
    except ValueError:
        return 0


def bool_text(value):
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def fmt(value):
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return ""
    if isinstance(value, float):
        return f"{value:.10g}"
    return value


def avg(values):
    finite = [v for v in values if math.isfinite(v)]
    return sum(finite) / len(finite) if finite else math.nan


def ratio(num, den):
    if not math.isfinite(num) or not math.isfinite(den) or abs(den) <= 1e-12:
        return math.nan
    return num / den


def rel_diff(new, base):
    if not math.isfinite(new) or not math.isfinite(base):
        return math.nan
    return 100.0 * (new - base) / max(1.0, abs(base))


def objective_family(row):
    obj_type = row.get("objective_type", "").strip().lower()
    if obj_type == "cvar":
        return "CVaR"
    if obj_type in {"mean_cvar", "mean-cvar", "meancvar"}:
        return "MeanCVaR"
    if obj_type == "expected":
        return "Expected"
    risk = row.get("risk_measure", "").strip().lower()
    method = row.get("method", "")
    if risk == "cvar" or "-CVaR" in method:
        return "CVaR"
    if risk in {"mean-cvar", "meancvar"} or "-MeanCVaR" in method:
        return "MeanCVaR"
    return "Expected"


def projected_family(row):
    method = row.get("method", "")
    value = row.get("projected_llbi_family", "").strip().lower()
    if value in {"coverage", "path"}:
        return value
    if "ProjectedCoverageLLBI" in method:
        return "coverage"
    if "ProjectedPathLLBI" in method:
        return "path"
    return "none"


def projected_variant(row):
    method = row.get("method", "")
    value = row.get("projected_llbi_variant", row.get("projected_llbi_strategy", "")).strip().lower()
    if value in {"poly", "exp"}:
        return value
    if "-poly" in method:
        return "poly"
    if "-exp" in method:
        return "exp"
    return "none"


def method_family(row):
    method = row.get("method", "")
    fpp_mode = row.get("fpp_mode", "")
    if method.startswith("FPP-SAA"):
        return "FPP-SAA-fpp_base" if fpp_mode in {"", "fpp_base"} else f"FPP-SAA-{fpp_mode}"
    if "Combinatorial" in method:
        return "FPP-Branch-Benders-Combinatorial"
    if "ProjectedCoverageLLBI-poly" in method:
        return "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts"
    if "ProjectedPathLLBI-poly" in method:
        return "FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts"
    if "ProjectedCoverageLLBI-exp" in method:
        return "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts"
    if "ProjectedPathLLBI-exp" in method:
        return "FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts"
    if "LLBI-RootCuts" in method:
        return "FPP-Branch-Benders-LLBI-RootCuts"
    if "RootCuts" in method:
        return "FPP-Branch-Benders-RootCuts"
    return method


def enrich(row):
    out = dict(row)
    for target, sources in [
        ("instance_id", ("instance_name", "landscape")),
        ("instance_type", ("instance_group",)),
        ("objective_in_sample", ("objective_value",)),
        ("runtime_seconds", ("runtime_sec",)),
        ("train_scenario_count", ("train_count",)),
        ("test_scenario_count", ("test_count",)),
        ("branch_benders_lazy_cuts_added", ("benders_lazy_cuts_added",)),
        ("branch_benders_root_user_cuts_added", ("root_user_cuts_added",)),
        ("benders_lifted_lower_bound_count", ("llbi_cuts_added",)),
        ("train_worst_10pct_burned_area", ("train_worst10_burned_area",)),
        ("test_worst_10pct_burned_area", ("test_worst10_burned_area",)),
        ("train_evaluation_runtime_seconds", ("train_eval_runtime_sec",)),
        ("test_evaluation_runtime_seconds", ("test_eval_runtime_sec",)),
    ]:
        if not is_missing(out.get(target)):
            continue
        for source in sources:
            if not is_missing(row.get(source)):
                out[target] = row.get(source, "")
                break
    out["objective_family"] = objective_family(row)
    out["method_family"] = method_family(row)
    out["projected_llbi_family_reconstructed"] = projected_family(row)
    out["projected_llbi_variant_reconstructed"] = projected_variant(row)
    out["use_root_user_cuts_reconstructed"] = str(
        bool_text(row.get("branch_benders_use_root_user_cuts", "")) or row.get("method", "").endswith("-RootCuts")
    ).lower()
    method_tokens = set(row.get("method", "").split("-"))
    out["use_lifted_lower_bounds_reconstructed"] = str(
        bool_text(row.get("benders_use_lifted_lower_bounds", "")) or "LLBI" in method_tokens
    ).lower()
    out["use_projected_llbi_reconstructed"] = str(projected_family(row) != "none").lower()
    out["use_combinatorial_benders_reconstructed"] = str(
        bool_text(row.get("combinatorial_benders_enabled", "")) or "Combinatorial" in row.get("method", "")
    ).lower()
    return out


def train_count(row):
    return row.get("train_scenario_count", row.get("train_count", ""))


def controlled_key(row):
    return (
        row.get("instance_id", ""),
        row.get("alpha", ""),
        train_count(row),
        row.get("case_id", ""),
    )


def cross_alpha_key(row):
    return (
        row.get("instance_id", ""),
        train_count(row),
        row.get("case_id", ""),
    )


def comparison_key(row):
    return (
        row.get("instance_id", ""),
        row["objective_family"],
        row.get("alpha", ""),
        train_count(row),
        row.get("case_id", ""),
    )


def status_bucket(row):
    status = row.get("solver_status", "").lower()
    obj = as_float(row.get("objective_in_sample"))
    if "optimal" in status:
        return "optimal"
    if "time" in status and math.isfinite(obj):
        return "time_limited_with_incumbent"
    if math.isfinite(obj) and not any(x in status for x in ["fail", "infeasible", "unbounded", "no feasible"]):
        return "incumbent_other"
    return "failed_or_no_incumbent"


def metric(row, *names):
    for name in names:
        if name in row:
            return as_float(row.get(name))
    return math.nan


SUMMARY_METRICS = [
    ("avg_objective_value", ("objective_in_sample", "objective_value")),
    ("avg_best_bound", ("best_bound",)),
    ("avg_mip_gap", ("mip_gap",)),
    ("avg_runtime", ("runtime_seconds", "runtime_sec")),
    ("avg_train_expected_burned_area", ("train_expected_burned_area",)),
    ("avg_test_expected_burned_area", ("test_expected_burned_area",)),
    ("avg_train_worst10_burned_area", ("train_worst_10pct_burned_area", "train_worst10_burned_area")),
    ("avg_test_worst10_burned_area", ("test_worst_10pct_burned_area", "test_worst10_burned_area")),
    ("avg_lazy_benders_cuts_added", ("branch_benders_lazy_cuts_added", "benders_lazy_cuts_added")),
    ("avg_root_user_cuts_added", ("branch_benders_root_user_cuts_added", "root_user_cuts_added")),
    ("avg_standard_llbi_cut_count", ("benders_lifted_lower_bound_count", "llbi_cuts_added")),
    ("avg_projected_llbi_cut_count", ("projected_llbi_cuts_added",)),
    ("avg_root_separation_time", ("branch_benders_root_user_cut_total_time_sec", "projected_llbi_total_time_sec")),
    ("avg_subproblem_solving_time", ("branch_benders_subproblem_time_sec", "benders_subproblem_time_sec")),
]


def combinatorial_cut_count(row):
    if not is_missing(row.get("combinatorial_benders_cuts_added")):
        return as_int(row.get("combinatorial_benders_cuts_added"))
    return (
        as_int(row.get("combinatorial_benders_integer_cuts_added"))
        + as_int(row.get("combinatorial_benders_fractional_cuts_added"))
        + as_int(row.get("combinatorial_benders_initial_cuts_added"))
    )


def summarize(rows, keys):
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(k, "") for k in keys)].append(row)

    summary = []
    for key_values, group in sorted(groups.items()):
        out = {k: v for k, v in zip(keys, key_values)}
        buckets = Counter(status_bucket(row) for row in group)
        out["runs"] = len(group)
        out["solved_to_optimality"] = buckets.get("optimal", 0)
        out["time_limited_with_incumbent"] = buckets.get("time_limited_with_incumbent", 0)
        out["failed_or_without_incumbent"] = buckets.get("failed_or_no_incumbent", 0)
        out["status_counts"] = ";".join(
            f"{k}:{v}" for k, v in sorted(Counter(row.get("solver_status", "") for row in group).items()))
        for name, source_names in SUMMARY_METRICS:
            out[name] = fmt(avg([metric(row, *source_names) for row in group]))
        out["avg_combinatorial_cut_count"] = fmt(avg([float(combinatorial_cut_count(row)) for row in group]))
        summary.append(out)
    return summary


def write_csv(path, rows, fieldnames=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if fieldnames is None:
        fieldnames = []
        seen = set()
        for row in rows:
            for key in row:
                if key not in seen:
                    seen.add(key)
                    fieldnames.append(key)
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def parse_ids(text):
    if is_missing(text):
        return []
    return [int(token) for token in re_split_ids(text)]


def re_split_ids(text):
    import re
    return [token for token in re.split(r"[\s,;]+", str(text).strip()) if token]


def read_manifest(results_dir):
    path = os.path.join(results_dir, "manifests", "full_task_manifest.csv")
    if not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def validate_splits(rows, manifest_rows, allow_partial=False):
    report = []
    ok = True

    has_split_lists = any("train_ids" in row or "test_ids" in row for row in rows)
    groups = defaultdict(list)
    for row in rows:
        groups[controlled_key(row)].append(row)
    if has_split_lists:
        for key, group in sorted(groups.items()):
            train_ids = {row.get("train_ids", "") for row in group}
            test_ids = {row.get("test_ids", "") for row in group}
            if len(train_ids) != 1 or len(test_ids) != 1:
                ok = False
                report.append(f"SPLIT MISMATCH {key}: train_versions={len(train_ids)} test_versions={len(test_ids)}")
        if ok:
            report.append(f"PASS controlled split validation for {len(groups)} (instance_id, alpha, train_count, case_id) blocks.")

        cross_alpha = defaultdict(list)
        for row in rows:
            cross_alpha[cross_alpha_key(row)].append(row)
        for key, group in sorted(cross_alpha.items()):
            train_ids = {row.get("train_ids", "") for row in group}
            test_ids = {row.get("test_ids", "") for row in group}
            if len(train_ids) != 1 or len(test_ids) != 1:
                ok = False
                report.append(f"CROSS-ALPHA SPLIT MISMATCH {key}: train_versions={len(train_ids)} test_versions={len(test_ids)}")
        if ok:
            report.append(f"PASS cross-alpha split reuse validation for {len(cross_alpha)} (instance_id, train_count, case_id) blocks.")

        by_instance = defaultdict(list)
        for row in rows:
            by_instance[row.get("instance_id", "")].append(row)
        for instance_id, group in sorted(by_instance.items()):
            test_versions = {row.get("test_ids", "") for row in group}
            if len(test_versions) != 1:
                ok = False
                report.append(f"FIXED-OOS MISMATCH {instance_id}: test_versions={len(test_versions)}")
                continue
            first = group[0]
            expected = list(range(as_int(first.get("test_pool_min")), as_int(first.get("test_pool_max")) + 1))
            actual = parse_ids(next(iter(test_versions)))
            if expected and actual != expected:
                ok = False
                report.append(
                    f"FIXED-OOS RANGE MISMATCH {instance_id}: actual_count={len(actual)} expected_count={len(expected)}")
        if ok:
            report.append(f"PASS fixed OOS test-set validation for {len(by_instance)} instances.")
    else:
        report.append(
            "SKIP split-list validation: compact input omits train_ids/test_ids; "
            "manifest split paths remain the source of deterministic split reuse."
        )

    expected_methods = len({row.get("method", "") for row in manifest_rows}) if manifest_rows else len({row.get("method", "") for row in rows})
    block_counts = Counter(controlled_key(row) for row in rows)
    incomplete = {k: v for k, v in block_counts.items() if v != expected_methods}
    if incomplete:
        if not allow_partial:
            ok = False
        report.append(
            f"{'WARN' if allow_partial else 'BLOCK METHOD COUNT MISMATCH'} expected_methods={expected_methods}: " +
            "; ".join(f"{k}:{v}" for k, v in sorted(incomplete.items())[:20])
        )
    else:
        report.append(f"PASS method-panel validation with {expected_methods} methods per controlled block.")

    if manifest_rows:
        expected_blocks = {controlled_key(row) for row in manifest_rows}
        observed_blocks = set(block_counts)
        missing_blocks = sorted(expected_blocks - observed_blocks)
        if missing_blocks:
            if not allow_partial:
                ok = False
            report.append(
                f"{'WARN' if allow_partial else 'MISSING CONTROLLED BLOCKS'} "
                f"missing_blocks={len(missing_blocks)}: " +
                "; ".join(str(key) for key in missing_blocks[:20])
            )
        elif expected_blocks:
            report.append(f"PASS all {len(expected_blocks)} manifest controlled blocks are present.")

    return ok, report, expected_methods


def row_values(row):
    return {
        "objective_value": as_float(row.get("objective_in_sample")),
        "runtime": metric(row, "runtime_seconds", "runtime_sec"),
        "mip_gap": as_float(row.get("mip_gap")),
        "test_expected_burned_area": as_float(row.get("test_expected_burned_area")),
        "test_worst10_burned_area": metric(row, "test_worst_10pct_burned_area", "test_worst10_burned_area"),
    }


def comparison_row(prefix, lhs, rhs):
    lval = row_values(lhs)
    rval = row_values(rhs)
    out = dict(prefix)
    out.update({
        "lhs_method": lhs.get("method", ""),
        "rhs_method": rhs.get("method", ""),
        "lhs_method_family": lhs.get("method_family", ""),
        "rhs_method_family": rhs.get("method_family", ""),
        "objective_difference_lhs_minus_rhs": fmt(lval["objective_value"] - rval["objective_value"]),
        "objective_relative_difference_percent": fmt(rel_diff(lval["objective_value"], rval["objective_value"])),
        "runtime_difference_lhs_minus_rhs": fmt(lval["runtime"] - rval["runtime"]),
        "runtime_ratio_lhs_over_rhs": fmt(ratio(lval["runtime"], rval["runtime"])),
        "mip_gap_difference_lhs_minus_rhs": fmt(lval["mip_gap"] - rval["mip_gap"]),
        "test_expected_difference_lhs_minus_rhs": fmt(
            lval["test_expected_burned_area"] - rval["test_expected_burned_area"]
        ),
        "test_worst10_difference_lhs_minus_rhs": fmt(
            lval["test_worst10_burned_area"] - rval["test_worst10_burned_area"]
        ),
    })
    return out


def build_method_vs_saa(rows):
    baselines = {}
    for row in rows:
        if row["method_family"] == "FPP-SAA-fpp_base":
            baselines[comparison_key(row)] = row
    out = []
    for row in rows:
        key = comparison_key(row)
        base = baselines.get(key)
        if base is None or row is base:
            continue
        out.append(comparison_row({
            "instance_id": row.get("instance_id", ""),
            "instance_type": row.get("instance_type", ""),
            "objective_family": row["objective_family"],
            "alpha": row.get("alpha", ""),
            "train_count": train_count(row),
            "case_id": row.get("case_id", ""),
            "baseline": "FPP-SAA-fpp_base",
        }, row, base))
    return out


def build_projected_comparison(rows):
    indexed = {}
    for row in rows:
        fam = projected_family(row)
        var = projected_variant(row)
        if fam != "none" and var != "none":
            indexed[(comparison_key(row), fam, var)] = row
    pairs = [
        ("coverage_exp_vs_path_exp", ("coverage", "exp"), ("path", "exp")),
    ]
    out = []
    for key in sorted({k for (k, _fam, _var) in indexed}):
        for label, lhs_key, rhs_key in pairs:
            lhs = indexed.get((key, *lhs_key))
            rhs = indexed.get((key, *rhs_key))
            if lhs is None or rhs is None:
                continue
            instance_id, objective, alpha, train, case_id = key
            out.append(comparison_row({
                "comparison": label,
                "instance_id": instance_id,
                "instance_type": lhs.get("instance_type", ""),
                "objective_family": objective,
                "alpha": alpha,
                "train_count": train,
                "case_id": case_id,
            }, lhs, rhs))
    return out


def build_rootcuts_impact(rows):
    root_baselines = {}
    for row in rows:
        if row["method_family"] == "FPP-Branch-Benders-RootCuts":
            root_baselines[comparison_key(row)] = row
    out = []
    for row in rows:
        if row["method_family"] in {"FPP-SAA-fpp_base", "FPP-Branch-Benders-RootCuts"}:
            continue
        base = root_baselines.get(comparison_key(row))
        if base is None:
            continue
        out.append(comparison_row({
            "instance_id": row.get("instance_id", ""),
            "instance_type": row.get("instance_type", ""),
            "objective_family": row["objective_family"],
            "alpha": row.get("alpha", ""),
            "train_count": train_count(row),
            "case_id": row.get("case_id", ""),
            "baseline": "FPP-Branch-Benders-RootCuts",
        }, row, base))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--input-csv")
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="Write summaries even if some manifest blocks are missing or incomplete.",
    )
    args = parser.parse_args()

    input_csv = args.input_csv or str(default_analysis_input_csv(Path(args.results_dir), args.allow_partial))
    if not os.path.exists(input_csv):
        raise SystemExit(f"Missing input CSV: {input_csv}")

    with open(input_csv, newline="") as f:
        rows = [enrich(row) for row in csv.DictReader(f)]

    manifest_rows = read_manifest(args.results_dir)
    ok, validation_lines, expected_methods = validate_splits(rows, manifest_rows, allow_partial=args.allow_partial)
    validation_lines.append(f"Allow partial: {str(args.allow_partial).lower()}")
    validation_lines.append(f"Expected methods per block: {expected_methods}")
    validation_lines.append(f"Total rows: {len(rows)}")
    validation_lines.append(f"Input CSV: {input_csv}")

    output_dir = args.results_dir
    summary_fields = [
        "instance_id", "instance_type", "objective_family", "alpha", "train_scenario_count",
        "method_family", "method", "projected_llbi_family_reconstructed",
        "projected_llbi_variant_reconstructed", "runs", "solved_to_optimality",
        "time_limited_with_incumbent", "failed_or_without_incumbent", "status_counts",
    ] + [name for name, _ in SUMMARY_METRICS] + ["avg_combinatorial_cut_count"]

    write_csv(
        os.path.join(output_dir, "summary_by_instance_objective_alpha_traincount_method.csv"),
        summarize(rows, [
            "instance_id", "instance_type", "objective_family", "alpha", "train_scenario_count",
            "method_family", "method", "projected_llbi_family_reconstructed",
            "projected_llbi_variant_reconstructed",
        ]),
        summary_fields,
    )
    write_csv(
        os.path.join(output_dir, "summary_by_instance_alpha_traincount_method.csv"),
        summarize(rows, [
            "instance_id", "instance_type", "alpha", "train_scenario_count",
            "method_family", "method", "projected_llbi_family_reconstructed",
            "projected_llbi_variant_reconstructed",
        ]),
    )
    write_csv(os.path.join(output_dir, "method_vs_fpp_saa_base.csv"), build_method_vs_saa(rows))
    write_csv(os.path.join(output_dir, "projected_llbi_comparison.csv"), build_projected_comparison(rows))
    write_csv(os.path.join(output_dir, "rootcuts_llbi_impact.csv"), build_rootcuts_impact(rows))

    report_path = os.path.join(output_dir, "validation_report.txt")
    with open(report_path, "w") as f:
        f.write("\n".join(validation_lines) + "\n")
    print(f"Wrote analysis outputs under {output_dir}")
    if not ok:
        raise SystemExit("Controlled split validation failed; see validation_report.txt")


if __name__ == "__main__":
    main()
