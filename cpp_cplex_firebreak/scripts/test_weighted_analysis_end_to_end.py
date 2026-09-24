#!/usr/bin/env python3
"""Phase 9B end-to-end test: the real Phase 8B smoke fixture merged through
Phase 9A, analyzed by the Phase 9B CLI, plus a synthetic multi-instance
fixture large enough to actually exercise (not skip) paired statistical
tests (section 33: "Add synthetic fixtures where the small smoke lacks
enough methods or instances for statistical testing").

Usage: python3 scripts/test_weighted_analysis_end_to_end.py
"""

from __future__ import annotations

import csv
import json
import tempfile
from pathlib import Path

import analyze_weighted_experiment_results as cli
import merge_weighted_experiment_results as merge_cli
import weighted_result_schema as schema
from weighted_analysis_test_helpers import check, make_dpv_row, make_exact_row, make_heuristic_row, report

ROOT = Path(__file__).resolve().parent.parent
SMOKE_ROOT = ROOT / "results" / "weighted_phase8b_smoke"


def _read_csv(path: Path) -> list:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def test_phase8b_smoke_end_to_end():
    if not SMOKE_ROOT.exists():
        check(True, f"skipping: {SMOKE_ROOT} not present in this checkout")
        return
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        merged_dir = tmp_path / "merged"
        rc = merge_cli.main(["--input-root", str(SMOKE_ROOT), "--output-dir", str(merged_dir), "--strict"])
        check(rc == 0, "Phase 9A merge over the real Phase 8B smoke fixture succeeds")

        analysis_dir = tmp_path / "analysis"
        rc2 = cli.main([
            "--merged-current-valid", str(merged_dir / "merged_current_valid.csv"),
            "--merged-all-attempts", str(merged_dir / "merged_all_attempts.csv"),
            "--output-dir", str(analysis_dir),
        ])
        check(rc2 == 0, "Phase 9B analysis over the merged Phase 8B smoke fixture succeeds")

        diagnostics = json.loads((analysis_dir / "analysis_diagnostics.json").read_text(encoding="utf-8"))
        check(diagnostics["input_rows"] == 12, "analysis diagnostics report the expected 12 input rows")
        check(diagnostics["comparison_groups"] == 3, "one comparison group per weight profile (3 profiles, 1 instance each)")
        check(diagnostics["groups_with_exact_reference"] == 3, "every group has an exact-FPP best-known reference")
        check(diagnostics["groups_with_certified_optimum"] == 3, "every group's best-known value is certified optimal in this tiny fixture")

        best_known = _read_csv(analysis_dir / "best_known" / "best_known_values.csv")
        methods_defining_best_known = {row["best_known_feasible_method"] for row in best_known}
        check(methods_defining_best_known.issubset({"FPP-SAA", "FPP-Branch-Benders-Combinatorial"}),
              "best-known values are defined only by exact FPP methods, never DPV-SAA or Static-DPV")

        gaps = _read_csv(analysis_dir / "gaps" / "row_level_gaps.csv")
        dpv_rows = [r for r in gaps if r["method"] == "DPV-SAA"]
        heuristic_rows = [r for r in gaps if r["method"] == "Static-DPV"]
        check(len(dpv_rows) == 3 and all(r["gap_to_best_known_feasible"] not in ("", None) for r in dpv_rows),
              "DPV-SAA rows receive an evaluator-based gap to best-known feasible")
        check(len(heuristic_rows) == 3 and all(r["gap_to_best_known_feasible"] not in ("", None) for r in heuristic_rows),
              "Static-DPV (heuristic) rows receive an evaluator-based gap to best-known feasible")
        check(all(r["objective_space"] == "dpv_optimization" for r in dpv_rows), "DPV-SAA classified as dpv_optimization")
        check(all(r["objective_space"] == "heuristic" for r in heuristic_rows), "Static-DPV classified as heuristic")

        oos_summary = _read_csv(analysis_dir / "summaries" / "out_of_sample_summary.csv")
        paired_summary = _read_csv(analysis_dir / "summaries" / "paired_reburn_summary.csv")
        oos_values = {(r["method"], r["weight_profile"]): r["mean_out_of_sample_value"] for r in oos_summary}
        paired_values = {(r["method"], r["weight_profile"]): r["mean_paired_reburn_value"] for r in paired_summary}
        common_keys = set(oos_values) & set(paired_values)
        check(len(common_keys) > 0, "OOS and paired summaries share at least one (method, profile) key to compare")
        check(any(oos_values[k] != paired_values[k] for k in common_keys),
              "OOS and paired-reburn summaries report differing values where the underlying data differs")

        profile_rows = _read_csv(analysis_dir / "profiles" / "runtime_performance_profile.csv")
        profiles_seen = {row["weight_profile"] for row in profile_rows}
        check(profiles_seen == {"homogeneous", "heterogeneous", "clustered"},
              "runtime performance profile rows are kept separate by weight profile")

        for table_name in ("table_exact_methods", "table_heuristic_dpv_methods", "table_out_of_sample",
                            "table_paired_reburn", "table_best_known", "table_statistical_comparisons"):
            check((analysis_dir / "tables" / f"{table_name}.csv").exists(), f"{table_name}.csv was produced")
            check((analysis_dir / "tables" / f"{table_name}.tex").exists(), f"{table_name}.tex was produced")


def _build_synthetic_multi_instance_fixture(n_instances: int = 10) -> list:
    """Many instances under one (weight_profile, risk_measure) stratum so
    method-pair statistics have enough paired observations to actually run,
    not skip."""
    rows = []
    for i in range(n_instances):
        instance_id = f"synthetic{i}"
        base = 100.0 + i * 3.0
        rows.append(make_exact_row(
            f"{instance_id}_saa", value=base, best_bound=base, runtime=1.0 + 0.1 * i,
            method="FPP-SAA", instance_id=instance_id, canonical_landscape_id=instance_id,
            in_sample_scenario_ids=[i], out_of_sample_scenario_ids=[100 + i],
            weighted_fpp_expected_out_of_sample=base * 1.5,
            weighted_fpp_expected_paired_reburn=base * 1.6,
        ))
        rows.append(make_exact_row(
            f"{instance_id}_bb", value=base, best_bound=base, runtime=2.0 + 0.15 * i,
            method="FPP-Branch-Benders-Combinatorial", instance_id=instance_id, canonical_landscape_id=instance_id,
            in_sample_scenario_ids=[i], out_of_sample_scenario_ids=[100 + i],
            weighted_fpp_expected_out_of_sample=base * 1.4,
            weighted_fpp_expected_paired_reburn=base * 1.55,
        ))
        rows.append(make_dpv_row(
            f"{instance_id}_dpv", surrogate_value=base * 20, evaluator_value=base * 6.0, runtime=0.5 + 0.05 * i,
            instance_id=instance_id, canonical_landscape_id=instance_id,
            in_sample_scenario_ids=[i], out_of_sample_scenario_ids=[100 + i],
            weighted_fpp_expected_out_of_sample=base * 6.5,
            weighted_fpp_expected_paired_reburn=base * 6.6,
        ))
        rows.append(make_heuristic_row(
            f"{instance_id}_static", evaluator_value=base * 4.0, runtime=0.1,
            instance_id=instance_id, canonical_landscape_id=instance_id,
            in_sample_scenario_ids=[i], out_of_sample_scenario_ids=[100 + i],
            weighted_fpp_expected_out_of_sample=base * 4.2,
            weighted_fpp_expected_paired_reburn=base * 4.3,
        ))
    return rows


def test_synthetic_fixture_runs_real_statistics():
    """Bypasses the merge CLI entirely (these rows are already in the
    canonical shape) and calls the analysis modules directly, exactly as
    analyze_weighted_experiment_results.py's main() does internally."""
    import weighted_analysis_core as core
    import weighted_analysis_statistics as stats_mod

    rows = [core.enrich_with_comparison_objective(r) for r in _build_synthetic_multi_instance_fixture(10)]
    result = core.run_core_analysis(rows)
    analyzed = result["rows"]

    comparisons = stats_mod.run_family_comparisons(
        analyzed, metric="in_sample_weighted_fpp_objective",
        group_key_fn=lambda r: schema.comparison_group_key(r),
        metric_field_fn=lambda r: r.get("fpp_comparison_objective_in_sample"),
        stratum_fn=lambda r: (r.get("weight_profile"), r.get("risk_measure")),
        min_pairs=6,
    )
    run = [c for c in comparisons if not c["skipped"]]
    check(len(run) > 0, "with 10 synthetic instances, at least one method-pair statistical comparison actually runs (not skipped)")
    check(any(c["test_name"] in ("wilcoxon_signed_rank", "sign_test") for c in run),
          "a real test_name (Wilcoxon or sign test) is reported for the comparisons that ran")
    check(all(c["adjusted_p_value"] is not None for c in run), "every run comparison receives a Holm-adjusted p-value")


def main() -> int:
    test_phase8b_smoke_end_to_end()
    test_synthetic_fixture_runs_real_statistics()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
