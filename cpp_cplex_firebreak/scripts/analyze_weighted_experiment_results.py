#!/usr/bin/env python3
"""Phase 9B analysis CLI: scientific comparison logic, best-known
references, optimality/heuristic gaps, performance profiles, statistical
comparisons, and publication-ready result tables over a Phase 9A
`merged_current_valid.csv`.

Reuses Phase 9A's normalization/validation/comparison-group infrastructure
end to end (weighted_result_schema.read_canonical_csv rejects any row that
did not itself pass Phase 9A validation) -- this script parses nothing
itself and defines no schema of its own.

No solver execution, no weight-map generation, no large production
experiment is launched here.

Usage:
    python3 scripts/analyze_weighted_experiment_results.py \\
        --merged-current-valid results/.../merged/merged_current_valid.csv \\
        --merged-all-attempts results/.../merged/merged_all_attempts.csv \\
        --output-dir results/.../analysis
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import weighted_analysis_core as core
import weighted_analysis_profiles as profiles_mod
import weighted_analysis_statistics as stats_mod
import weighted_analysis_summaries as summ
import weighted_analysis_tables as tables_mod
import weighted_result_schema as schema

ROW_LEVEL_GAP_COLUMNS = [
    "run_id", "method", "method_family", "objective_space", "weight_profile", "weight_replicate",
    "weight_map_hash", "risk_measure", "alpha", "cvar_beta", "mean_cvar_lambda", "budget",
    "instance_id", "canonical_landscape_id", "solver_status", "execution_status",
    "fpp_comparison_objective_in_sample", "fpp_comparison_objective_in_sample_source",
    "best_known_feasible_value", "best_known_feasible_method",
    "best_known_lower_bound", "best_known_lower_bound_method",
    "best_known_certification_gap", "best_known_certified_optimal",
    "gap_to_best_known_feasible", "gap_to_best_known_feasible_percent", "gap_to_best_known_feasible_flagged_negative",
    "gap_to_best_known_lower_bound", "gap_to_best_known_lower_bound_percent",
    "solver_gap", "solver_best_bound", "running_time_sec",
    "weighted_fpp_expected_out_of_sample", "weighted_fpp_expected_paired_reburn",
    "best_observed_out_of_sample_value", "out_of_sample_regret_to_best_observed", "out_of_sample_regret_to_best_observed_percent",
    "best_observed_paired_reburn_value", "paired_reburn_regret_to_best_observed", "paired_reburn_regret_to_best_observed_percent",
    "mean_burned_cells_in_sample", "mean_burned_cells_out_of_sample", "mean_burned_cells_paired_reburn",
]

WIN_TIE_LOSS_COLUMNS = ["metric", "method_a", "method_b", "method_a_wins", "ties", "method_b_wins", "paired_observations"]


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--merged-current-valid", type=Path, required=True)
    parser.add_argument("--merged-all-attempts", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--weight-profile", action="append", default=[])
    parser.add_argument("--risk-measure", action="append", default=[])
    parser.add_argument("--scenario-count", action="append", type=int, default=[])
    parser.add_argument("--method", action="append", default=[])
    parser.add_argument("--gap-tolerance", type=float, default=core.DEFAULT_GAP_TOLERANCE)
    parser.add_argument("--optimality-tolerance", type=float, default=core.DEFAULT_OPTIMALITY_TOLERANCE)
    parser.add_argument("--statistical-min-pairs", type=int, default=stats_mod.DEFAULT_MIN_PAIRS)
    parser.add_argument("--holm-correction", dest="holm_correction", action="store_true", default=True)
    parser.add_argument("--no-holm-correction", dest="holm_correction", action="store_false")
    parser.add_argument("--generate-plots", action="store_true", default=False)
    parser.add_argument("--generate-latex", dest="generate_latex", action="store_true", default=True)
    parser.add_argument("--no-generate-latex", dest="generate_latex", action="store_false")
    parser.add_argument("--strict", action="store_true", default=False,
                         help="Exit non-zero if any comparison group violates LB <= best-known + tolerance.")
    return parser.parse_args(argv)


def _apply_filters(records: list, args: argparse.Namespace) -> list:
    out = records
    if args.weight_profile:
        out = [r for r in out if r.get("weight_profile") in args.weight_profile]
    if args.risk_measure:
        out = [r for r in out if r.get("risk_measure") in args.risk_measure]
    if args.scenario_count:
        out = [r for r in out if r.get("train_scenario_count") in args.scenario_count]
    if args.method:
        out = [r for r in out if r.get("method") in args.method]
    return out


def _write_csv(path: Path, rows: list, columns: list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({c: row.get(c) for c in columns})


def _flatten_profile_rows(profiles: dict) -> list:
    out = []
    for _, data in sorted(profiles.items()):
        out.extend(data["rows"])
    return out


def main(argv=None) -> int:
    args = parse_args(argv)

    try:
        current_valid_raw = schema.read_canonical_csv(args.merged_current_valid, require_valid=True)
    except schema.CanonicalRowRejected as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    all_attempts_raw = []
    if args.merged_all_attempts is not None:
        all_attempts_raw = schema.read_canonical_csv(args.merged_all_attempts, require_valid=False)

    records = _apply_filters(current_valid_raw, args)
    if not records:
        print("error: no rows remain after applying filters", file=sys.stderr)
        return 2

    result = core.run_core_analysis(records, gap_tolerance=args.gap_tolerance, optimality_tolerance=args.optimality_tolerance)
    rows = result["rows"]
    comparison_groups = result["comparison_groups"]
    core_diag = result["diagnostics"]

    out = args.output_dir
    for sub in ("diagnostics", "best_known", "gaps", "summaries", "statistics", "profiles", "tables", "plots"):
        (out / sub).mkdir(parents=True, exist_ok=True)

    # -- summaries --------------------------------------------------------
    method_summary_rows = summ.method_summary(rows)
    # Profile-stratified by default (Phase 10 section 13); the aggregated
    # variant is a separate, explicitly-named artifact, never the default.
    exact_summary_rows = summ.exact_method_summary(rows)
    exact_summary_aggregated_rows = summ.exact_method_summary_aggregated(rows)
    heuristic_dpv_rows = summ.heuristic_dpv_summary(rows)
    heuristic_dpv_aggregated_rows = summ.heuristic_dpv_summary_aggregated(rows)
    oos_summary_rows = summ.out_of_sample_summary(rows)
    paired_summary_rows = summ.paired_reburn_summary(rows)
    physical_summary_rows = summ.physical_metric_summary(rows)
    win_tie_loss_rows = summ.all_win_tie_loss(rows, tolerance=args.gap_tolerance)
    replicate_level_rows = summ.replicate_level_method_summary(
        rows, metric_field="fpp_comparison_objective_in_sample", metric_name="fpp_comparison_objective_in_sample")
    replicate_aggregated_rows = summ.replicate_aggregated_method_summary(
        rows, metric_field="fpp_comparison_objective_in_sample", metric_name="fpp_comparison_objective_in_sample")

    _write_csv(out / "summaries" / "method_summary.csv", method_summary_rows,
               list(method_summary_rows[0].keys()) if method_summary_rows else [])
    _write_csv(out / "summaries" / "exact_method_summary.csv", exact_summary_rows,
               list(exact_summary_rows[0].keys()) if exact_summary_rows else [])
    _write_csv(out / "summaries" / "exact_method_summary_aggregated_across_profiles.csv", exact_summary_aggregated_rows,
               list(exact_summary_aggregated_rows[0].keys()) if exact_summary_aggregated_rows else [])
    _write_csv(out / "summaries" / "heuristic_dpv_summary.csv", heuristic_dpv_rows,
               list(heuristic_dpv_rows[0].keys()) if heuristic_dpv_rows else [])
    _write_csv(out / "summaries" / "heuristic_dpv_summary_aggregated_across_profiles.csv", heuristic_dpv_aggregated_rows,
               list(heuristic_dpv_aggregated_rows[0].keys()) if heuristic_dpv_aggregated_rows else [])
    _write_csv(out / "summaries" / "out_of_sample_summary.csv", oos_summary_rows,
               list(oos_summary_rows[0].keys()) if oos_summary_rows else [])
    _write_csv(out / "summaries" / "paired_reburn_summary.csv", paired_summary_rows,
               list(paired_summary_rows[0].keys()) if paired_summary_rows else [])
    _write_csv(out / "summaries" / "physical_metric_summary.csv", physical_summary_rows,
               list(physical_summary_rows[0].keys()) if physical_summary_rows else [])
    _write_csv(out / "summaries" / "win_tie_loss.csv", win_tie_loss_rows, WIN_TIE_LOSS_COLUMNS)
    _write_csv(out / "summaries" / "replicate_level_method_summary.csv", replicate_level_rows,
               list(replicate_level_rows[0].keys()) if replicate_level_rows else [])
    _write_csv(out / "summaries" / "replicate_aggregated_method_summary.csv", replicate_aggregated_rows,
               list(replicate_aggregated_rows[0].keys()) if replicate_aggregated_rows else [])

    # -- best-known / comparison groups / row-level gaps -------------------
    comparison_group_columns = [
        "canonical_landscape_id", "instance_family", "instance_id", "train_scenario_ids", "train_scenario_count",
        "weight_profile", "weight_replicate", "weight_map_hash", "risk_measure", "alpha", "cvar_beta",
        "mean_cvar_lambda", "budget", "group_size",
    ]
    _write_csv(out / "best_known" / "comparison_groups.csv", comparison_groups, comparison_group_columns)
    _write_csv(out / "best_known" / "best_known_values.csv", comparison_groups,
               list(comparison_groups[0].keys()) if comparison_groups else [])
    _write_csv(out / "gaps" / "row_level_gaps.csv", rows, ROW_LEVEL_GAP_COLUMNS)

    # -- statistics ---------------------------------------------------------
    statistical_rows = []
    families = [
        ("in_sample_weighted_fpp_objective", lambda r: schema.comparison_group_key(r),
         lambda r: r.get("fpp_comparison_objective_in_sample"), None),
        ("out_of_sample_weighted_objective", core.out_of_sample_comparison_key,
         lambda r: r.get(core.oos_metric_field(r)), None),
        ("paired_reburn_weighted_objective", core.paired_reburn_comparison_key,
         lambda r: r.get(core.paired_metric_field(r)), core.paired_eligible),
        ("runtime_among_mutually_successful_exact_runs", lambda r: schema.comparison_group_key(r),
         lambda r: r.get("running_time_sec"),
         lambda r: r.get("objective_space") == core.OBJECTIVE_SPACE_EXACT_FPP and schema.is_valid_exact_result(r)),
    ]
    for metric, group_key_fn, metric_field_fn, filter_fn in families:
        comparisons = stats_mod.run_family_comparisons(
            rows, metric=metric, group_key_fn=group_key_fn, metric_field_fn=metric_field_fn,
            stratum_fn=lambda r: (r.get("weight_profile"), r.get("risk_measure")),
            filter_fn=filter_fn, min_pairs=args.statistical_min_pairs, tolerance=args.gap_tolerance,
        )
        if not args.holm_correction:
            for c in comparisons:
                c["adjusted_p_value"] = c.get("raw_p_value")
        statistical_rows.extend(comparisons)
    _write_csv(out / "statistics" / "statistical_tests.csv", statistical_rows,
               tables_mod.TABLE_STATISTICAL_COMPARISONS_COLUMNS + ["skipped", "skip_reason"])

    # -- profiles -------------------------------------------------------
    runtime_profiles = profiles_mod.build_runtime_performance_profiles(rows, all_attempts_raw)
    quality_insample = profiles_mod.build_quality_profiles(rows, namespace="insample")
    quality_oos = profiles_mod.build_quality_profiles(rows, namespace="oos")
    quality_paired = profiles_mod.build_quality_profiles(rows, namespace="paired")
    cactus_rows = profiles_mod.build_cactus_coverage(rows)

    _write_csv(out / "profiles" / "runtime_performance_profile.csv", _flatten_profile_rows(runtime_profiles),
               ["weight_profile", "risk_measure", "instance_id", "weight_map_hash", "method",
                "runtime_sec", "best_runtime_sec", "runtime_ratio", "solver_status", "solved", "attempted"])
    _write_csv(out / "profiles" / "quality_profile_insample.csv", _flatten_profile_rows(quality_insample),
               ["weight_profile", "risk_measure", "namespace", "method", "run_id", "gap", "quality_ratio"])
    _write_csv(out / "profiles" / "quality_profile_oos.csv", _flatten_profile_rows(quality_oos),
               ["weight_profile", "risk_measure", "namespace", "method", "run_id", "gap", "quality_ratio"])
    _write_csv(out / "profiles" / "quality_profile_paired.csv", _flatten_profile_rows(quality_paired),
               ["weight_profile", "risk_measure", "namespace", "method", "run_id", "gap", "quality_ratio"])
    _write_csv(out / "profiles" / "cactus_coverage.csv", cactus_rows,
               ["method", "threshold", "fraction_within_threshold", "observations"])

    # -- publication tables ---------------------------------------------
    tables_written = tables_mod.write_all_tables(
        out / "tables",
        exact_methods=exact_summary_rows, heuristic_dpv=heuristic_dpv_rows,
        out_of_sample=oos_summary_rows, paired_reburn=paired_summary_rows,
        best_known=comparison_groups, statistical_comparisons=statistical_rows,
        generate_latex=args.generate_latex,
    )

    # -- plots (optional) -------------------------------------------------
    plots_written = []
    if args.generate_plots:
        import weighted_analysis_plots as plots_mod
        plots_written = (
            plots_mod.plot_runtime_performance_profiles(runtime_profiles, out / "plots")
            + plots_mod.plot_quality_profiles(quality_insample, out / "plots", namespace="insample")
            + plots_mod.plot_quality_profiles(quality_oos, out / "plots", namespace="oos")
            + plots_mod.plot_quality_profiles(quality_paired, out / "plots", namespace="paired")
        )

    # -- diagnostics -------------------------------------------------------
    statistical_comparisons_run = sum(1 for c in statistical_rows if not c.get("skipped"))
    statistical_comparisons_skipped = sum(1 for c in statistical_rows if c.get("skipped"))
    diagnostics = {
        "schema_version": schema.RESULT_SCHEMA_VERSION,
        "input_rows": len(current_valid_raw),
        "valid_rows": len(records),
        **core_diag,
        "statistical_comparisons_run": statistical_comparisons_run,
        "statistical_comparisons_skipped": statistical_comparisons_skipped,
        "statistical_comparisons_skip_reasons": sorted({c["skip_reason"] for c in statistical_rows if c.get("skipped")}),
        "scipy_available": stats_mod.SCIPY_AVAILABLE,
        "performance_profiles_generated": len(runtime_profiles),
        "quality_profiles_generated": len(quality_insample) + len(quality_oos) + len(quality_paired),
        "tables_generated": [str(p) for p in tables_written],
        "plots_generated": [str(p) for p in plots_written],
        "invalid_analysis_groups": len(core_diag.get("bound_consistency_violations", [])),
        "filters_applied": {
            "weight_profile": args.weight_profile, "risk_measure": args.risk_measure,
            "scenario_count": args.scenario_count, "method": args.method,
        },
        "gap_tolerance": args.gap_tolerance,
        "optimality_tolerance": args.optimality_tolerance,
        "statistical_min_pairs": args.statistical_min_pairs,
        "holm_correction": args.holm_correction,
    }
    (out / "analysis_diagnostics.json").write_text(json.dumps(diagnostics, indent=2, sort_keys=True, default=str) + "\n", encoding="utf-8")
    (out / "diagnostics" / "analysis_diagnostics.json").write_text(
        json.dumps(diagnostics, indent=2, sort_keys=True, default=str) + "\n", encoding="utf-8")

    print(f"Analyzed {len(records)} rows across {core_diag['comparison_groups']} comparison groups.")
    print(f"  groups_with_exact_reference={core_diag['groups_with_exact_reference']} "
          f"groups_without_exact_reference={core_diag['groups_without_exact_reference']}")
    print(f"  statistical_comparisons_run={statistical_comparisons_run} skipped={statistical_comparisons_skipped}")
    print(f"Wrote analysis outputs to {out}")

    if args.strict and core_diag.get("bound_consistency_violations"):
        print(f"error: --strict is set and {len(core_diag['bound_consistency_violations'])} "
              "comparison group(s) violate LB <= best-known + tolerance", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
