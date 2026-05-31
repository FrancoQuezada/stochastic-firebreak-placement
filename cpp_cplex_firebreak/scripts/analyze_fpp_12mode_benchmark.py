#!/usr/bin/env python3
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path


ROOT = Path("results/batch")
INPUTS = [
    ROOT / "fpp_12mode_benchmark_alpha001" / "batch_results.csv",
    ROOT / "fpp_12mode_benchmark_alpha002" / "batch_results.csv",
    ROOT / "fpp_12mode_benchmark_alpha003" / "batch_results.csv",
]

MODES = [
    "fpp_base",
    "fpp_base_greedy",
    "fpp_base_dominator",
    "fpp_base_separator",
    "fpp_base_dominator_separator",
    "fpp_base_dominator_separator_greedy",
    "fpp_cut",
    "fpp_cut_greedy",
    "fpp_cut_dominator",
    "fpp_cut_separator",
    "fpp_cut_dominator_separator",
    "fpp_cut_dominator_separator_greedy",
]

STRENGTHENING_PAIRS = [
    ("base -> base_dominator", "fpp_base", "fpp_base_dominator", "dominator"),
    ("base -> base_separator", "fpp_base", "fpp_base_separator", "separator"),
    ("base -> base_dominator_separator", "fpp_base", "fpp_base_dominator_separator", "dominator_separator"),
    ("cut -> cut_dominator", "fpp_cut", "fpp_cut_dominator", "dominator"),
    ("cut -> cut_separator", "fpp_cut", "fpp_cut_separator", "separator"),
    ("cut -> cut_dominator_separator", "fpp_cut", "fpp_cut_dominator_separator", "dominator_separator"),
]


def to_float(value):
    if value is None or value == "":
        return math.nan
    try:
        return float(value)
    except ValueError:
        return math.nan


def to_int(value, default=0):
    if value is None or value == "":
        return default
    try:
        return int(float(value))
    except ValueError:
        return default


def fmt(value):
    if value is None or not math.isfinite(value):
        return ""
    return f"{value:.8f}"


def avg(values):
    clean = [v for v in values if math.isfinite(v)]
    return sum(clean) / len(clean) if clean else math.nan


def med(values):
    clean = [v for v in values if math.isfinite(v)]
    return statistics.median(clean) if clean else math.nan


def norm_solution(value):
    if not value:
        return ()
    try:
        return tuple(sorted(int(x) for x in value.split(";") if x != ""))
    except ValueError:
        return tuple(sorted(x for x in value.split(";") if x != ""))


def read_rows():
    rows = []
    for path in INPUTS:
        if not path.exists():
            raise SystemExit(f"Missing benchmark CSV: {path}")
        with path.open(newline="") as f:
            for row in csv.DictReader(f):
                row["_source_csv"] = str(path)
                row["_alpha"] = to_float(row.get("alpha"))
                row["_case_id"] = to_int(row.get("case_id"), -1)
                row["_mode"] = row.get("fpp_mode", "")
                row["_objective"] = to_float(row.get("objective_in_sample"))
                row["_best_bound"] = to_float(row.get("best_bound"))
                row["_mip_gap"] = to_float(row.get("mip_gap"))
                row["_runtime"] = to_float(row.get("runtime_seconds"))
                row["_explored_nodes"] = to_float(row.get("explored_nodes"))
                row["_heuristic_objective"] = to_float(row.get("heuristic_objective"))
                row["_heuristic_time"] = to_float(row.get("heuristic_time_sec"))
                row["_dominator_cuts"] = to_float(row.get("dominator_cuts_added"))
                row["_separator_cuts"] = to_float(row.get("separator_cuts_added"))
                row["_separator_calls"] = to_float(row.get("separator_min_cut_calls"))
                row["_separator_callbacks"] = to_float(row.get("separator_callback_invocations"))
                row["_eval_abs_diff"] = to_float(row.get("evaluator_abs_diff"))
                row["_eval_rel_diff"] = to_float(row.get("evaluator_rel_diff"))
                row["_test_expected"] = to_float(row.get("test_expected_burned_area"))
                row["_solution"] = norm_solution(row.get("selected_firebreaks", ""))
                rows.append(row)
    return rows


def rows_by_key(rows):
    by_key = defaultdict(dict)
    for row in rows:
        by_key[(row["_alpha"], row["_case_id"])][row["_mode"]] = row
    return by_key


def enrich_against_base(rows):
    by_key = rows_by_key(rows)
    best_incumbent = {}
    for key, modes in by_key.items():
        objectives = [r["_objective"] for r in modes.values() if math.isfinite(r["_objective"])]
        best_incumbent[key] = min(objectives) if objectives else math.nan

    for row in rows:
        key = (row["_alpha"], row["_case_id"])
        base = by_key[key].get("fpp_base")
        if not base:
            row["_final_bound_improvement"] = math.nan
            row["_gap_reduction"] = math.nan
            row["_bound_closure"] = math.nan
            row["_objective_diff_vs_base"] = math.nan
            row["_test_expected_diff_vs_base"] = math.nan
            row["_runtime_ratio_vs_base"] = math.nan
            row["_same_solution_as_base"] = False
            continue
        row["_final_bound_improvement"] = row["_best_bound"] - base["_best_bound"]
        row["_gap_reduction"] = base["_mip_gap"] - row["_mip_gap"]
        denom = best_incumbent[key] - base["_best_bound"]
        row["_bound_closure"] = (
            row["_final_bound_improvement"] / denom
            if math.isfinite(denom) and abs(denom) > 1.0e-9
            else math.nan
        )
        row["_objective_diff_vs_base"] = row["_objective"] - base["_objective"]
        row["_test_expected_diff_vs_base"] = row["_test_expected"] - base["_test_expected"]
        row["_runtime_ratio_vs_base"] = (
            row["_runtime"] / base["_runtime"]
            if math.isfinite(base["_runtime"]) and base["_runtime"] > 0.0
            else math.nan
        )
        row["_same_solution_as_base"] = row["_solution"] == base["_solution"]


def is_optimal(row):
    return "Optimal" in row.get("solver_status", "")


def is_time_limit(row):
    status = row.get("solver_status", "").lower()
    return "time" in status or "abort" in status


def write_csv(path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def summary_rows(rows, by_alpha=True):
    grouped = defaultdict(list)
    for row in rows:
        key = (row["_alpha"], row["_mode"]) if by_alpha else ("all", row["_mode"])
        grouped[key].append(row)

    output = []
    mode_order = {mode: i for i, mode in enumerate(MODES)}
    for key in sorted(grouped, key=lambda k: (k[0], mode_order.get(k[1], 999))):
        alpha, mode = key
        group = grouped[key]
        output.append({
            "alpha": "" if alpha == "all" else fmt(alpha),
            "fpp_mode": mode,
            "cases": len(group),
            "optimal_solves": sum(1 for r in group if is_optimal(r)),
            "time_limit_solves": sum(1 for r in group if is_time_limit(r)),
            "avg_objective": fmt(avg([r["_objective"] for r in group])),
            "avg_best_bound": fmt(avg([r["_best_bound"] for r in group])),
            "avg_mip_gap": fmt(avg([r["_mip_gap"] for r in group])),
            "avg_runtime": fmt(avg([r["_runtime"] for r in group])),
            "median_runtime": fmt(med([r["_runtime"] for r in group])),
            "avg_explored_nodes": fmt(avg([r["_explored_nodes"] for r in group])),
            "avg_final_bound_improvement_vs_fpp_base": fmt(avg([r["_final_bound_improvement"] for r in group])),
            "avg_gap_reduction_vs_fpp_base": fmt(avg([r["_gap_reduction"] for r in group])),
            "avg_runtime_ratio_vs_fpp_base": fmt(avg([r["_runtime_ratio_vs_base"] for r in group])),
            "avg_objective_diff_vs_fpp_base": fmt(avg([r["_objective_diff_vs_base"] for r in group])),
            "avg_test_expected_diff_vs_fpp_base": fmt(avg([r["_test_expected_diff_vs_base"] for r in group])),
            "avg_bound_closure_relative_to_base": fmt(avg([r["_bound_closure"] for r in group])),
            "avg_heuristic_objective": fmt(avg([r["_heuristic_objective"] for r in group])),
            "avg_heuristic_time": fmt(avg([r["_heuristic_time"] for r in group])),
            "avg_dominator_cuts": fmt(avg([r["_dominator_cuts"] for r in group])),
            "avg_separator_cuts": fmt(avg([r["_separator_cuts"] for r in group])),
            "avg_separator_min_cut_calls": fmt(avg([r["_separator_calls"] for r in group])),
            "avg_separator_callback_invocations": fmt(avg([r["_separator_callbacks"] for r in group])),
            "avg_evaluator_abs_diff": fmt(avg([r["_eval_abs_diff"] for r in group])),
            "max_evaluator_abs_diff": fmt(max([r["_eval_abs_diff"] for r in group if math.isfinite(r["_eval_abs_diff"])], default=math.nan)),
            "avg_test_expected_burned_area": fmt(avg([r["_test_expected"] for r in group])),
            "same_solution_as_fpp_base_count": sum(1 for r in group if r["_same_solution_as_base"]),
        })
    return output


def mode_vs_base_rows(rows):
    fields = []
    for row in summary_rows(rows, by_alpha=True):
        fields.append({
            "alpha": row["alpha"],
            "fpp_mode": row["fpp_mode"],
            "runtime_ratio_vs_fpp_base": row["avg_runtime_ratio_vs_fpp_base"],
            "final_bound_improvement_vs_fpp_base": row["avg_final_bound_improvement_vs_fpp_base"],
            "gap_reduction_vs_fpp_base": row["avg_gap_reduction_vs_fpp_base"],
            "objective_diff_vs_fpp_base": row["avg_objective_diff_vs_fpp_base"],
            "test_expected_diff_vs_fpp_base": row["avg_test_expected_diff_vs_fpp_base"],
            "bound_closure_relative_to_base": row["avg_bound_closure_relative_to_base"],
            "same_solution_as_fpp_base_count": row["same_solution_as_fpp_base_count"],
        })
    return fields


def bound_impact_rows(rows):
    by_key = rows_by_key(rows)
    by_alpha_transition = defaultdict(list)
    for (alpha, case_id), modes in by_key.items():
        for label, source_mode, target_mode, cut_type in STRENGTHENING_PAIRS:
            source = modes.get(source_mode)
            target = modes.get(target_mode)
            if not source or not target:
                continue
            by_alpha_transition[(alpha, label, cut_type)].append((source, target))

    output = []
    for (alpha, label, cut_type), pairs in sorted(by_alpha_transition.items()):
        improvements = [t["_best_bound"] - s["_best_bound"] for s, t in pairs]
        gap_reductions = [s["_mip_gap"] - t["_mip_gap"] for s, t in pairs]
        runtime_ratios = [
            t["_runtime"] / s["_runtime"]
            for s, t in pairs
            if math.isfinite(s["_runtime"]) and s["_runtime"] > 0.0
        ]
        objective_diffs = [t["_objective"] - s["_objective"] for s, t in pairs]
        cuts_added = [
            t["_dominator_cuts"] + t["_separator_cuts"]
            if cut_type == "dominator_separator"
            else (t["_dominator_cuts"] if cut_type == "dominator" else t["_separator_cuts"])
            for _, t in pairs
        ]
        output.append({
            "alpha": fmt(alpha),
            "comparison": label,
            "cut_type": cut_type,
            "cases": len(pairs),
            "avg_final_bound_improvement": fmt(avg(improvements)),
            "avg_root_bound_improvement": "",
            "avg_gap_reduction": fmt(avg(gap_reductions)),
            "avg_runtime_ratio": fmt(avg(runtime_ratios)),
            "avg_objective_difference": fmt(avg(objective_diffs)),
            "cases_bound_improved": sum(1 for v in improvements if math.isfinite(v) and v > 1.0e-6),
            "cases_runtime_improved": sum(1 for v in runtime_ratios if math.isfinite(v) and v < 1.0),
            "avg_cuts_added": fmt(avg(cuts_added)),
        })
    return output


def validation_report(rows):
    by_key = rows_by_key(rows)
    lines = []
    mismatches = []
    evaluator_warnings = []
    for key, modes in sorted(by_key.items()):
        optimal = [r for r in modes.values() if is_optimal(r)]
        if optimal:
            ref = optimal[0]["_objective"]
            bad = [r for r in optimal if abs(r["_objective"] - ref) > 1.0e-5]
            if bad:
                mismatches.append((key, [(r["_mode"], r["_objective"]) for r in bad]))
        for row in modes.values():
            if (math.isfinite(row["_eval_abs_diff"]) and math.isfinite(row["_eval_rel_diff"]) and
                    row["_eval_abs_diff"] > 1.0e-5 and row["_eval_rel_diff"] > 1.0e-6):
                evaluator_warnings.append((key, row["_mode"], row["_eval_abs_diff"], row["_eval_rel_diff"]))

    separator_rows = [r for r in rows if "separator" in r["_mode"]]
    total_callbacks = sum(r["_separator_callbacks"] for r in separator_rows if math.isfinite(r["_separator_callbacks"]))
    total_cuts = sum(r["_separator_cuts"] for r in separator_rows if math.isfinite(r["_separator_cuts"]))

    lines.append(f"rows={len(rows)}")
    lines.append(f"objective_mismatch_groups={len(mismatches)}")
    lines.append(f"evaluator_validation_warnings={len(evaluator_warnings)}")
    lines.append(f"separator_callback_invocations_total={total_callbacks:.0f}")
    lines.append(f"separator_cuts_added_total={total_cuts:.0f}")
    if total_callbacks == 0 and separator_rows:
        lines.append("warning=separator-enabled modes had zero callback invocations across this benchmark")
    if mismatches:
        lines.append(f"objective_mismatches={mismatches}")
    if evaluator_warnings:
        lines.append(f"evaluator_warnings={evaluator_warnings}")
    lines.append("root_bound_available=false")
    lines.append("root_bound_note=root relaxation bounds were not exposed by the production solver path for this run")
    return "\n".join(lines) + "\n"


def main():
    rows = read_rows()
    enrich_against_base(rows)

    out_summary = ROOT / "fpp_12mode_benchmark_summary.csv"
    out_by_alpha = ROOT / "fpp_12mode_benchmark_summary_by_alpha.csv"
    out_vs_base = ROOT / "fpp_12mode_benchmark_mode_vs_base.csv"
    out_bound = ROOT / "fpp_12mode_benchmark_bound_impact.csv"
    out_root = ROOT / "fpp_12mode_benchmark_root_bounds.csv"
    out_validation = ROOT / "fpp_12mode_benchmark_validation_report.txt"

    summary_fields = [
        "alpha", "fpp_mode", "cases", "optimal_solves", "time_limit_solves",
        "avg_objective", "avg_best_bound", "avg_mip_gap", "avg_runtime",
        "median_runtime", "avg_explored_nodes",
        "avg_final_bound_improvement_vs_fpp_base", "avg_gap_reduction_vs_fpp_base",
        "avg_runtime_ratio_vs_fpp_base", "avg_objective_diff_vs_fpp_base",
        "avg_test_expected_diff_vs_fpp_base", "avg_bound_closure_relative_to_base",
        "avg_heuristic_objective", "avg_heuristic_time",
        "avg_dominator_cuts", "avg_separator_cuts",
        "avg_separator_min_cut_calls", "avg_separator_callback_invocations",
        "avg_evaluator_abs_diff", "max_evaluator_abs_diff",
        "avg_test_expected_burned_area", "same_solution_as_fpp_base_count",
    ]
    write_csv(out_summary, summary_fields, summary_rows(rows, by_alpha=False))
    write_csv(out_by_alpha, summary_fields, summary_rows(rows, by_alpha=True))

    vs_base_fields = [
        "alpha", "fpp_mode", "runtime_ratio_vs_fpp_base",
        "final_bound_improvement_vs_fpp_base", "gap_reduction_vs_fpp_base",
        "objective_diff_vs_fpp_base", "test_expected_diff_vs_fpp_base",
        "bound_closure_relative_to_base", "same_solution_as_fpp_base_count",
    ]
    write_csv(out_vs_base, vs_base_fields, mode_vs_base_rows(rows))

    bound_fields = [
        "alpha", "comparison", "cut_type", "cases",
        "avg_final_bound_improvement", "avg_root_bound_improvement",
        "avg_gap_reduction", "avg_runtime_ratio", "avg_objective_difference",
        "cases_bound_improved", "cases_runtime_improved", "avg_cuts_added",
    ]
    write_csv(out_bound, bound_fields, bound_impact_rows(rows))

    write_csv(out_root, ["root_bound_available", "note"], [{
        "root_bound_available": "false",
        "note": "Root bounds were not available from the production run-batch-oos solver path.",
    }])
    out_validation.write_text(validation_report(rows))

    print(f"Wrote {out_summary}")
    print(f"Wrote {out_by_alpha}")
    print(f"Wrote {out_vs_base}")
    print(f"Wrote {out_bound}")
    print(f"Wrote {out_root}")
    print(f"Wrote {out_validation}")


if __name__ == "__main__":
    main()
