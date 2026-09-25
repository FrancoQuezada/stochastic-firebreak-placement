#!/usr/bin/env python3
"""Lightweight tests for the dedicated new20x20 manuscript campaign."""

from __future__ import annotations

import csv
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
GENERATOR = ROOT / "scripts" / "generate_fpp_new_instances_scaling_manifests.py"
PROCESSOR = ROOT / "scripts" / "process_fpp_new20x20_manuscript_campaign.py"
METHODS = ROOT / "config" / "fpp_new20x20_manuscript_methods.txt"


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


campaign = load_module(PROCESSOR, "new20x20_manuscript_campaign")
worker = load_module(
    ROOT / "scripts" / "run_fpp_new_instances_scaling_manifest_worker.py",
    "new20x20_manifest_worker",
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def generate_full_manifest(output: Path) -> list[dict[str, str]]:
    command = [
        sys.executable, str(GENERATOR),
        "--output-dir", str(output),
        "--instance-filter", "new20x20",
        "--train-counts", "100,200,400,800",
        "--alphas", "0.01,0.02,0.03",
        "--test-count", "1000",
        "--training-pool-min", "1", "--training-pool-max", "9000",
        "--test-pool-min", "9001", "--test-pool-max", "10000",
        "--num-cases", "30",
        "--seed-base", "20260529",
        "--time-limit", "1800",
        "--mip-gap", "0.001",
        "--threads", "1",
        "--method-file", str(METHODS),
        "--cvar-beta", "0.9",
        "--mean-cvar-lambda", "0.5",
        "--projected-llbi-root-rounds", "100",
        "--projected-llbi-max-cuts-per-round", "100",
        "--projected-llbi-violation-tolerance", "1e-3",
        "--projected-llbi-cut-density-limit", "0",
        "--projected-poly-max-cuts", "100000",
        "--paired-reburn-evaluation",
    ]
    subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
    return read_csv(output / "manifests" / "full_task_manifest.csv")


def test_exact_manifest_and_partial_gate() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        output = Path(temporary) / "campaign"
        rows = generate_full_manifest(output)
        assert len(rows) == 8640
        assert len({row["run_id"] for row in rows}) == 8640
        assert {row["instance_id"] for row in rows} == {"new20x20"}
        assert not any(row["instance_id"] == "new20x20_reburn" for row in rows)
        assert {int(row["train_count"]) for row in rows} == {100, 200, 400, 800}
        assert {row["alpha"] for row in rows} == {"0.01", "0.02", "0.03"}
        assert {row["objective_family"] for row in rows} == {"Expected", "CVaR", "MeanCVaR"}
        assert {row["method"] for row in rows} == set(campaign.EXPECTED_METHODS)
        assert not any("EtaDesc" in row["method"] for row in rows)
        assert all(row["paired_reburn_instance_id"] == "new20x20_reburn" for row in rows)
        assert all(row["paired_evaluation_enabled"] == "true" for row in rows)
        assert all(row["weight_profile"] == "homogeneous" and not row["weight_map_path"] for row in rows)

        blocks: dict[tuple[str, str, str], set[str]] = {}
        split_versions: dict[tuple[str, str], set[str]] = {}
        for row in rows:
            blocks.setdefault((row["train_count"], row["alpha"], row["case_id"]), set()).add(row["method"])
            split_versions.setdefault((row["train_count"], row["case_id"]), set()).add(row["train_split_path"])
        assert len(blocks) == 360
        assert all(len(methods) == 24 for methods in blocks.values())
        assert all(len(paths) == 1 for paths in split_versions.values())
        test_ids = campaign.parse_ids(Path(rows[0]["test_split_path"]).read_text(encoding="utf-8"))
        assert test_ids == list(range(9001, 10001))

        errors, _ = campaign.validate_manifest(rows, exact_design=True)
        assert errors == [], errors
        errors, _ = campaign.validate_manifest(rows[:-1], exact_design=True)
        assert any("8639 tasks" in error for error in errors)

        manifest_only = subprocess.run(
            [sys.executable, str(PROCESSOR), "--results-dir", str(output), "--manifest-only"],
            cwd=ROOT, capture_output=True, text=True)
        assert manifest_only.returncode == 0, manifest_only.stdout + manifest_only.stderr
        assert len(read_csv(output / "method_configuration.csv")) == 24

        strict_missing = subprocess.run(
            [sys.executable, str(PROCESSOR), "--results-dir", str(output)],
            cwd=ROOT, capture_output=True, text=True)
        assert strict_missing.returncode != 0
        assert "missing 8640 expected worker result rows" in (
            output / "manuscript_validation_report.txt").read_text(encoding="utf-8")

        partial = subprocess.run(
            [sys.executable, str(PROCESSOR), "--results-dir", str(output), "--allow-partial"],
            cwd=ROOT, capture_output=True, text=True)
        assert partial.returncode == 0, partial.stdout + partial.stderr
        summary = read_csv(output / "manuscript_summary.csv")
        assert len(summary) == 4 * 3 * 3 * 8
        assert all(row["expected_runs"] == "30" and row["observed_runs"] == "0" for row in summary)


def base_manifest_row(solution_dir: Path, objective: str) -> dict[str, str]:
    method = {
        "Expected": "FPP-SAA",
        "CVaR": "FPP-SAA-CVaR",
        "MeanCVaR": "FPP-SAA-MeanCVaR",
    }[objective]
    return {
        "task_id": "worker_000_task_000",
        "run_id": f"run_{objective}",
        "case_id": "case00",
        "seed": "20260632",
        "instance_id": "new20x20",
        "declared_cells": "400",
        "train_count": "3",
        "alpha": "0.01",
        "objective_family": objective,
        "risk_measure": {"Expected": "expected", "CVaR": "cvar", "MeanCVaR": "mean-cvar"}[objective],
        "cvar_beta": "0.9",
        "cvar_lambda": "0.5" if objective == "MeanCVaR" else "1.0",
        "method": method,
        "time_limit": "30",
        "mip_gap": "0.05",
        "threads": "1",
        "solution_dir": str(solution_dir),
    }


def successful_worker_row(manifest: dict[str, str], objective_value: float) -> dict[str, str]:
    return {
        "run_id": manifest["run_id"],
        "worker_return_code": "0",
        "worker_status": "ok",
        "solver_status": "Optimal",
        "solver_status_code": "1",
        "objective_in_sample": str(objective_value),
        "best_bound": str(objective_value),
        "mip_gap": "0",
        "runtime_seconds": "1.5",
        "worker_runtime_seconds": "2",
        "train_expected_burned_area": "10",
        "train_empirical_cvar_burned_area": "30",
        "train_worst_10pct_burned_area": "30",
        "test_expected_burned_area": "12",
        "test_empirical_cvar_burned_area": "32",
        "test_worst_10pct_burned_area": "32",
        "paired_reburn_train_expected_burned_area": "14",
        "paired_reburn_train_empirical_cvar_90pct_burned_area": "34",
        "paired_reburn_train_worst_10pct_burned_area": "34",
        "paired_reburn_train_scenario_count": "3",
        "paired_reburn_status": "ok",
        "paired_reburn_instance_resolved": "new20x20_reburn",
        "paired_selected_firebreaks_mapped": "2",
        "paired_selected_firebreaks_missing": "0",
        "selected_firebreak_original_ids": "4,2",
        "objective_validation_passed": "true",
        "attempt": "1",
    }


def test_objective_values_solutions_and_failure_retention() -> None:
    assert campaign.evaluated_objective("Expected", 10.0, 30.0, 0.25) == 10.0
    assert campaign.evaluated_objective("CVaR", 10.0, 30.0, 0.25) == 30.0
    assert campaign.evaluated_objective("MeanCVaR", 10.0, 30.0, 0.25) == 15.0

    with tempfile.TemporaryDirectory() as temporary:
        solution_dir = Path(temporary)
        (solution_dir / "worker_000_task_000.csv").write_text("4,2\n", encoding="utf-8")
        args = type("Args", (), {"experiment_id": "test"})()
        for objective, expected in (("Expected", 10.0), ("CVaR", 30.0), ("MeanCVaR", 20.0)):
            manifest = base_manifest_row(solution_dir, objective)
            raw = successful_worker_row(manifest, expected)
            errors: list[str] = []
            row, solution = campaign.result_row(args, manifest, raw, errors)
            assert errors == [], errors
            assert float(row["train_eval_objective_value"]) == expected
            assert solution is not None
            assert solution["selected_firebreak_ids"] == "2;4"
            assert solution["selected_firebreak_count"] == "2"

        manifest = base_manifest_row(solution_dir, "Expected")
        failed = {
            "run_id": manifest["run_id"],
            "worker_return_code": "1",
            "worker_status": "failed",
            "failure_stage": "solver_execution",
            "failure_type": "nonzero_exit",
            "attempt": "2",
        }
        errors = []
        row, solution = campaign.result_row(args, manifest, failed, errors)
        assert errors == []
        assert row["execution_status"] == "failed"
        assert row["has_incumbent"] == "false"
        assert row["objective_value"] == ""
        assert solution is None


def test_summary_uses_30_paired_cases_and_compact_resume() -> None:
    manifest = []
    rows = []
    for index in range(30):
        manifest.append({
            "train_count": "100", "alpha": "0.01", "method": "FPP-SAA",
        })
        rows.append({
            "N": "100", "alpha": "0.01", "objective_type": "Expected",
            "method": "FPP-SAA", "case_id": f"case{index:02d}",
            "has_incumbent": "true", "execution_status": "completed",
            "solver_status": "Optimal", "termination_reason": "optimal",
            "runtime_sec": str(index + 1), "mip_gap": "0", "upper_bound": "10",
            "lower_bound": "10", "train_eval_objective_value": "10",
            "oos_eval_objective_value": "11", "reburn_eval_objective_value": "12",
            "oos_expected_burned_area": "11", "oos_cvar_burned_area": "20",
            "reburn_expected_burned_area": "12", "reburn_cvar_burned_area": "21",
        })
    summary = campaign.build_summary(manifest, rows)
    assert len(summary) == 1
    assert summary[0]["expected_runs"] == "30"
    assert summary[0]["observed_runs"] == "30"
    assert summary[0]["runs_solved_to_proven_optimality"] == "30"
    assert summary[0]["optimality_rate"] == "1"
    assert float(summary[0]["mean_runtime_sec"]) == 15.5

    with tempfile.TemporaryDirectory() as temporary:
        solution_dir = Path(temporary)
        (solution_dir / "task.csv").write_text("1\n", encoding="utf-8")
        compact = {
            "task_id": "task",
            "run_id": "run",
            "solution_dir": str(solution_dir),
            "worker_return_code": "0",
            "solver_status": "Optimal",
            "objective_validation_passed": "true",
            "compact_success_output": "true",
            "compact_success_cleanup_status": "complete",
            "solver_result_json_status": "deleted",
            "solver_row_csv_status": "deleted",
            "paired_evaluation_enabled": "true",
            "paired_reburn_status": "ok",
            "paired_reburn_eval_json_status": "deleted",
        }
        assert worker.row_complete_and_valid(compact)


def main() -> int:
    test_exact_manifest_and_partial_gate()
    test_objective_values_solutions_and_failure_retention()
    test_summary_uses_30_paired_cases_and_compact_resume()
    print("All new20x20 manuscript campaign tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
