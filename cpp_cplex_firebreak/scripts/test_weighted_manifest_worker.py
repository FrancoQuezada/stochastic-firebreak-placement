#!/usr/bin/env python3
"""Phase 8B self-test for the weighted manifest worker.

Covers the worker-level mandatory cases: missing weight map fails per-row before solve
(without crashing the whole worker), resume skips valid completed rows, retry-failed
reruns only failed rows while preserving the logical run_id, a malformed/invalid result
is rerun without needing --retry-failed, one canonical weight-map hash is reported
identically across optimization/out-of-sample/paired-reburn stages, selected firebreaks
transfer to the reburn instance by original Cell2Fire ID with zero missing, and a legacy
manifest row (no weight metadata) resolves and executes as homogeneous.

Requires build_gpp/firebreak_cpp to be built (skips with a clear message otherwise).

Usage: python3 scripts/test_weighted_manifest_worker.py
"""

from __future__ import annotations

import csv
import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN_PATH = ROOT / "scripts" / "generate_fpp_new_instances_scaling_manifests.py"
WORKER_PATH = ROOT / "scripts" / "run_fpp_new_instances_scaling_manifest_worker.py"
BINARY = ROOT / "build_gpp" / "firebreak_cpp"


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_csv(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def generate_manifest(out_dir: Path, methods: list[str], *, weight_profile: str = "homogeneous",
                      generate_missing: bool = True, paired_reburn: bool = True) -> Path:
    methods_file = out_dir / "methods.txt"
    methods_file.write_text("\n".join(methods) + "\n", encoding="utf-8")
    cmd = [
        sys.executable, str(GEN_PATH),
        "--output-dir", str(out_dir / "manifest"),
        "--instance-filter", "new20x20",
        "--train-counts", "8",
        "--alphas", "0.02",
        "--num-cases", "1",
        "--test-count", "4",
        "--training-pool-min", "1", "--training-pool-max", "20",
        "--test-pool-min", "21", "--test-pool-max", "24",
        "--method-file", str(methods_file),
        "--time-limit", "15",
        "--mip-gap", "0.05",
        "--weight-profiles", weight_profile,
        "--weight-replicates", "0",
        "--weight-registry", str(out_dir / "weight_maps"),
        "--binary", str(BINARY),
    ]
    if generate_missing:
        cmd.append("--generate-missing-weight-maps")
    if paired_reburn:
        cmd.append("--paired-reburn-evaluation")
    subprocess.run(cmd, check=True, cwd=str(ROOT), capture_output=True, text=True)
    return out_dir / "manifest" / "manifests" / "worker_000_manifest.csv"


def generate_legacy_manifest(out_dir: Path, methods: list[str]) -> Path:
    """No --weight-registry at all: legacy homogeneous mode, no weight flags."""
    methods_file = out_dir / "methods.txt"
    methods_file.write_text("\n".join(methods) + "\n", encoding="utf-8")
    cmd = [
        sys.executable, str(GEN_PATH),
        "--output-dir", str(out_dir / "manifest"),
        "--instance-filter", "new20x20",
        "--train-counts", "8",
        "--alphas", "0.02",
        "--num-cases", "1",
        "--test-count", "4",
        "--training-pool-min", "1", "--training-pool-max", "20",
        "--test-pool-min", "21", "--test-pool-max", "24",
        "--method-file", str(methods_file),
        "--time-limit", "15",
        "--mip-gap", "0.05",
    ]
    subprocess.run(cmd, check=True, cwd=str(ROOT), capture_output=True, text=True)
    return out_dir / "manifest" / "manifests" / "worker_000_manifest.csv"


def run_worker(manifest: Path, *, extra_args: list[str] | None = None) -> subprocess.CompletedProcess:
    cmd = [
        sys.executable, str(WORKER_PATH),
        "--worker-id", "worker_000",
        "--manifest", str(manifest),
        "--binary", str(BINARY),
    ]
    cmd.extend(extra_args or [])
    return subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)


def worker_csv_path(manifest: Path) -> Path:
    return manifest.parent.parent / "workers" / "worker_000" / "batch_results_worker_000.csv"


def prepare_worker_dirs(manifest: Path) -> None:
    worker_dir = manifest.parent.parent / "workers" / "worker_000"
    for sub in ("logs", "json", "solutions"):
        (worker_dir / sub).mkdir(parents=True, exist_ok=True)


def test_missing_weight_map_fails_per_row_not_whole_worker() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_manifest(out_dir, ["FPP-SAA", "Static-DPV"])
        prepare_worker_dirs(manifest)
        weight_maps_dir = out_dir / "weight_maps"
        moved = out_dir / "weight_maps_moved"
        shutil.move(str(weight_maps_dir), str(moved))
        try:
            result = run_worker(manifest)
        finally:
            shutil.move(str(moved), str(weight_maps_dir))
        assert "Traceback" not in result.stdout and "Traceback" not in result.stderr, (
            "worker crashed instead of recording a per-row failure:\n" + result.stderr)
        rows = read_csv(worker_csv_path(manifest))
        assert len(rows) == 2, f"expected 2 recorded rows, got {len(rows)}"
        for row in rows:
            assert row["failure_stage"] == "weight_map_loading", row
        print("missing weight map fails per-row without crashing worker: OK")


def test_resume_skips_completed_valid_rows() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_manifest(out_dir, ["FPP-SAA", "Static-DPV"])
        prepare_worker_dirs(manifest)
        first = run_worker(manifest)
        assert first.returncode == 0, first.stdout + first.stderr
        second = run_worker(manifest)
        assert second.returncode == 0, second.stdout + second.stderr
        assert "SKIP complete worker_000_task_000" in second.stdout
        assert "SKIP complete worker_000_task_001" in second.stdout
        assert "START" not in second.stdout
        print("resume skips completed valid rows without re-launching solver: OK")


def test_retry_failed_reruns_only_failed_row_same_run_id() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_manifest(out_dir, ["Static-DPV"])
        prepare_worker_dirs(manifest)
        first = run_worker(manifest)
        assert first.returncode == 0, first.stdout + first.stderr

        csv_path = worker_csv_path(manifest)
        rows = read_csv(csv_path)
        original_run_id = rows[0]["run_id"]
        rows[0]["worker_status"] = "failed"
        rows[0]["worker_return_code"] = "1"
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)

        without_retry = run_worker(manifest)
        assert "SKIP failed" in without_retry.stdout
        assert without_retry.returncode != 0

        with_retry = run_worker(manifest, extra_args=["--retry-failed"])
        assert with_retry.returncode == 0, with_retry.stdout + with_retry.stderr
        assert "attempt 2" in with_retry.stdout

        final_rows = read_csv(csv_path)
        assert final_rows[0]["run_id"] == original_run_id, "retry must preserve the logical run_id"
        assert final_rows[0]["attempt"] == "2"
        assert final_rows[0]["resume_action"] == "retry_failed"
        print("retry-failed reruns only the failed row, preserving run_id: OK")


def test_malformed_result_reruns_without_retry_flag() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_manifest(out_dir, ["Static-DPV"])
        prepare_worker_dirs(manifest)
        first = run_worker(manifest)
        assert first.returncode == 0, first.stdout + first.stderr

        csv_path = worker_csv_path(manifest)
        rows = read_csv(csv_path)
        rows[0]["run_id"] = "stale-mismatched-run-id"
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)

        second = run_worker(manifest)
        assert second.returncode == 0, second.stdout + second.stderr
        assert "START worker_000_task_000" in second.stdout, (
            "a malformed/mismatched result must rerun without --retry-failed")
        print("malformed result reruns without --retry-failed: OK")


def test_one_hash_through_all_stages_and_firebreak_transfer() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_manifest(out_dir, ["FPP-SAA"])
        prepare_worker_dirs(manifest)
        result = run_worker(manifest)
        assert result.returncode == 0, result.stdout + result.stderr
        row = read_csv(worker_csv_path(manifest))[0]

        expected_hash = row["weight_map_hash"]
        assert expected_hash, "manifest row missing weight_map_hash"
        for field in ("optimization_weight_map_hash", "out_of_sample_weight_map_hash",
                     "paired_reburn_weight_map_hash"):
            assert row[field] == expected_hash, f"{field} = {row[field]!r} != {expected_hash!r}"

        assert row["paired_reburn_status"] == "ok", row.get("paired_reburn_status")
        assert row["paired_selected_firebreak_count"] == row["paired_selected_firebreaks_mapped"]
        assert row["paired_selected_firebreaks_missing"] == "0"
        assert row["paired_selected_mapping_status"] == "ok"
        assert row["selected_firebreak_original_ids"], "original firebreak IDs not recorded"
        print("one weight-map hash across optimization/OOS/paired stages + firebreak transfer: OK")


def test_heuristic_execution_status_not_optimal() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_manifest(out_dir, ["Static-DPV"])
        prepare_worker_dirs(manifest)
        result = run_worker(manifest)
        assert result.returncode == 0, result.stdout + result.stderr
        row = read_csv(worker_csv_path(manifest))[0]
        assert row["solver_status"] == "NotApplicable", row["solver_status"]
        assert row["execution_status"] == "heuristic_completed", row["execution_status"]
        print("heuristic execution reports NotApplicable + heuristic_completed: OK")


def test_legacy_homogeneous_row_executes() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        out_dir = Path(tmp)
        manifest = generate_legacy_manifest(out_dir, ["Static-DPV"])
        prepare_worker_dirs(manifest)
        manifest_rows = read_csv(manifest)
        assert manifest_rows[0]["weight_profile"] == "homogeneous"
        assert not manifest_rows[0]["weight_map_path"], "legacy row must not name a weight map"

        result = run_worker(manifest)
        assert result.returncode == 0, result.stdout + result.stderr
        row = read_csv(worker_csv_path(manifest))[0]
        assert row["worker_return_code"] == "0"
        print("legacy homogeneous manifest row (no weight map) executes successfully: OK")


def main() -> int:
    if not BINARY.exists():
        print(f"SKIPPED: {BINARY} not built.")
        return 0
    test_missing_weight_map_fails_per_row_not_whole_worker()
    test_resume_skips_completed_valid_rows()
    test_retry_failed_reruns_only_failed_row_same_run_id()
    test_malformed_result_reruns_without_retry_flag()
    test_one_hash_through_all_stages_and_firebreak_transfer()
    test_heuristic_execution_status_not_optimal()
    test_legacy_homogeneous_row_executes()
    print("All weighted manifest worker self-tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
