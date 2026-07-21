#!/usr/bin/env python3
"""Merge worker CSVs for the FPP new_instances scaling experiment."""

from __future__ import annotations

import argparse
import csv
from collections import Counter
from pathlib import Path

from fpp_new_instances_scaling_compact_schema import compact_csv_path_for, write_compact_csv


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=Path("results/batch/fpp_new_instances_scaling"))
    parser.add_argument(
        "--allow-partial",
        action="store_true",
        help="Merge successful rows from completed worker CSVs even if some expected workers are missing or incomplete.",
    )
    parser.add_argument(
        "--output-csv",
        type=Path,
        default=None,
        help="Optional combined CSV path. Defaults to batch_results_all.csv, or batch_results_partial.csv with --allow-partial.",
    )
    return parser.parse_args()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


OMIT_FIELDS = {
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
    "split_dir",
    "output_dir",
    "output_csv",
    "output_json",
    "solution_dir",
    "run_id",
    "solver_command",
    "worker_status",
    "worker_return_code",
    "worker_started_at_epoch",
    "worker_finished_at_epoch",
    "worker_command",
    "worker_log",
    "configured_mip_gap",
    "solver_mip_gap",
    "global_dominance_enabled",
    "global_dominance_candidates_removed",
    "global_dominance_equivalence_classes",
    "global_dominance_precompute_time_sec",
    "conditional_zero_benefit_enabled",
    "conditional_zero_benefit_fixings_attempted",
    "conditional_zero_benefit_fixings_applied",
    "conditional_zero_benefit_time_sec",
    "branch_benders_use_root_user_cuts",
    "branch_benders_root_user_cuts_added",
    "branch_benders_root_user_cut_rounds",
    "branch_benders_root_user_cut_max_violation",
    "restricted_candidate_enabled",
    "restricted_candidate_exact_mode",
    "restricted_candidate_initial_active_count",
    "restricted_candidate_final_active_count",
    "restricted_candidate_final_active_fraction",
    "restricted_candidate_eventually_activated_all",
    "restricted_candidate_rounds",
    "restricted_candidate_cut_pool_size",
    "restricted_candidate_heuristic_mode_enabled",
    "restricted_candidate_stopped_before_full_activation",
    "restricted_candidate_global_optimality_certified",
    "formulation",
    "dominator_cuts_enabled",
    "separator_cuts_enabled",
    "greedy_warm_start_enabled",
    "local_search_enabled",
    "compact_node_count",
    "eligible_node_count",
    "total_observed_scenario_nodes",
    "total_scenario_arcs",
    "separator_cuts_added",
    "separator_min_cut_calls",
    "separator_callback_invocations",
    "separator_duplicate_cuts_skipped",
    "separator_large_cuts_skipped",
    "separator_time_sec",
    "dominator_cuts_added",
    "dominator_aggregate_cuts_added",
    "dominator_individual_cuts_added",
    "dominator_dag_scenarios",
    "dominator_fallback_scenarios",
    "dominator_preprocessing_time_sec",
    "heuristic_time_sec",
    "heuristic_objective",
    "heuristic_exact_evaluations",
    "heuristic_selected_count",
    "evaluator_objective",
    "evaluator_abs_diff",
    "evaluator_rel_diff",
    "validation_status",
    "train_cvar_burned_area",
    "test_cvar_burned_area",
    "selected_firebreaks",
    "warm_start_used",
    "mip_start_accepted",
    "warm_start_source",
    "warm_start_valid_nodes",
    "warm_start_ignored_nodes",
    "warm_start_notes",
}

def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields: list[str] = []
    for row in rows:
        for field in row:
            if field in OMIT_FIELDS:
                continue
            if field not in fields:
                fields.append(field)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def as_float(value: str) -> float:
    try:
        return float(value)
    except ValueError:
        return 0.0


def as_int(value: str) -> int:
    try:
        return int(float(value))
    except ValueError:
        return 0


def sort_key(row: dict[str, str]) -> tuple:
    return (
        row.get("instance_id", ""),
        as_int(row.get("train_count", row.get("train_scenario_count", "0"))),
        as_float(row.get("alpha", "0")),
        row.get("case_id", ""),
        row.get("objective_family", ""),
        row.get("method_family", ""),
        row.get("method", ""),
        row.get("task_id", ""),
    )


def logical_key(row: dict[str, str]) -> tuple[str, ...]:
    # Phase 8B: the weight dimension (profile/replicate/map hash) is part of a row's
    # logical identity. Without it, the same base row solved under multiple weight
    # profiles/replicates would collide and appear as duplicate logical rows.
    return (
        row.get("instance_id", ""),
        row.get("landscape", ""),
        row.get("alpha", ""),
        row.get("train_count", row.get("train_scenario_count", "")),
        row.get("test_count", row.get("test_scenario_count", "")),
        row.get("case_id", ""),
        row.get("objective_family", ""),
        row.get("method", ""),
        row.get("fpp_mode", ""),
        row.get("weight_profile", ""),
        row.get("weight_replicate", ""),
        row.get("weight_map_hash", ""),
    )


def main() -> int:
    args = parse_args()
    manifest_dir = args.results_dir / "manifests"
    full_manifest = manifest_dir / "full_task_manifest.csv"
    if not full_manifest.exists():
        raise SystemExit(f"Missing manifest: {full_manifest}")
    manifest_rows = read_csv(full_manifest)
    expected_total = len(manifest_rows)
    expected_by_worker = Counter(row["worker_id"] for row in manifest_rows)
    expected_methods = len({row.get("method", "") for row in manifest_rows})

    merged: list[dict[str, str]] = []
    report: list[str] = []
    missing_workers: list[str] = []
    incomplete_workers: list[str] = []
    failed_rows = 0
    for worker_id in sorted(expected_by_worker):
        worker_csv = args.results_dir / "workers" / worker_id / f"batch_results_{worker_id}.csv"
        if not worker_csv.exists():
            if args.allow_partial:
                missing_workers.append(worker_id)
                report.append(f"{worker_id}: missing")
                continue
            raise SystemExit(f"Missing worker CSV: {worker_csv}")
        rows = read_csv(worker_csv)
        expected = expected_by_worker[worker_id]
        # Note: workers are no longer required to match the GLOBAL distinct-method count
        # (`expected_methods`). Under Phase 8B capability filtering, different weight
        # profiles can legitimately surface different surviving-method counts for the
        # same instance/train/alpha/case group (e.g. a combinatorial+LLBI combination
        # filtered out only for non-homogeneous profiles), so per-worker row counts are
        # validated against the manifest's own per-worker count below instead.
        complete_rows = [row for row in rows if row.get("worker_return_code") == "0"]
        complete = len(complete_rows)
        failed_rows += len(rows) - complete
        if len(rows) != expected or complete != expected:
            if args.allow_partial:
                incomplete_workers.append(worker_id)
                report.append(f"{worker_id}: {complete}/{expected} successful rows")
                merged.extend(complete_rows)
                continue
            if len(rows) != expected:
                raise SystemExit(f"{worker_csv} has {len(rows)} rows; expected {expected}.")
            raise SystemExit(f"{worker_csv} has {complete}/{expected} successful rows.")
        report.append(f"{worker_id}: {len(rows)} rows")
        merged.extend(complete_rows)

    if len(merged) != expected_total and not args.allow_partial:
        raise SystemExit(f"Merged {len(merged)} rows; expected {expected_total}.")
    counts = Counter(logical_key(row) for row in merged)
    duplicates = [key for key, count in counts.items() if count > 1]
    if duplicates:
        raise SystemExit(f"Duplicate logical rows found: {len(duplicates)}")

    merged.sort(key=sort_key)
    if args.output_csv is not None:
        combined_csv = args.output_csv
    elif args.allow_partial:
        combined_csv = args.results_dir / "batch_results_partial.csv"
    else:
        combined_csv = args.results_dir / "batch_results_all.csv"
    write_csv(combined_csv, merged)
    compact_csv = compact_csv_path_for(combined_csv, args.results_dir, args.allow_partial)
    write_compact_csv(compact_csv, merged, default_experiment_id=args.results_dir.name)
    report_path = args.results_dir / ("merge_report_partial.txt" if args.allow_partial else "merge_report.txt")
    report_path.write_text(
        "\n".join([
            f"merged_rows={len(merged)}",
            f"expected_rows={expected_total}",
            f"workers_expected={len(expected_by_worker)}",
            f"workers_merged={len(expected_by_worker) - len(missing_workers)}",
            f"missing_workers={len(missing_workers)}",
            f"incomplete_workers={len(incomplete_workers)}",
            f"failed_or_unmerged_rows={failed_rows}",
            f"methods_per_worker={expected_methods}",
            f"allow_partial={str(args.allow_partial).lower()}",
            f"combined_csv={combined_csv}",
            f"compact_csv={compact_csv}",
            "",
            *report,
            "",
        ]),
        encoding="utf-8",
    )
    print(f"Merged {len(merged)} rows into {combined_csv}")
    print(f"Wrote compact results to {compact_csv}")
    if args.allow_partial:
        print(f"Partial merge report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
