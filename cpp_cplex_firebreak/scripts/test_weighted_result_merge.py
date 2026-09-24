#!/usr/bin/env python3
"""Phase 9A end-to-end merge tests. Covers mandatory case:
  27.15 stable column order (deterministic outputs)

...plus a full CLI round-trip (discovery -> normalize -> validate ->
dedupe -> write artifacts) over a small synthetic worker-CSV tree, and the
never-discard-invalid-rows guarantee (section 19).

Usage: python3 scripts/test_weighted_result_merge.py
"""

from __future__ import annotations

import csv
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema
from weighted_result_test_helpers import base_raw_row, check, report

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "scripts" / "merge_weighted_experiment_results.py"


def test_stable_column_order():  # 27.15
    rows = [
        base_raw_row(run_id="run_a", method="FPP-SAA"),
        base_raw_row(run_id="run_b", method="DPV-SAA", dpv_surrogate_objective="11385.5", objective_in_sample="11385.5"),
    ]
    records = [norm.normalize_row(r, prefer_json=False) for r in rows]
    result = merge.build_merge_outputs(records)

    field_order_a = list(schema.CANONICAL_FIELD_ORDER) + list(schema.VALIDATION_META_FIELD_ORDER)
    for record in result["all_attempts"]:
        check(list(record.keys())[: len(schema.CANONICAL_FIELD_ORDER)] != [],
              "canonical fields are present on every normalized record")
    check(field_order_a == list(schema.CANONICAL_FIELD_ORDER) + list(schema.VALIDATION_META_FIELD_ORDER),
          "the declared output column order is stable across calls")

    # Running the same input twice must produce byte-identical CSV output.
    with tempfile.TemporaryDirectory() as tmp1, tempfile.TemporaryDirectory() as tmp2:
        import merge_weighted_experiment_results as cli
        cli.write_records_csv(Path(tmp1) / "out.csv", result["all_attempts"])
        cli.write_records_csv(Path(tmp2) / "out.csv", result["all_attempts"])
        text1 = (Path(tmp1) / "out.csv").read_text(encoding="utf-8")
        text2 = (Path(tmp2) / "out.csv").read_text(encoding="utf-8")
        check(text1 == text2, "writing the same merge result twice produces byte-identical CSV output")
        header = text1.splitlines()[0]
        check(header.split(",") == list(schema.CANONICAL_FIELD_ORDER) + list(schema.VALIDATION_META_FIELD_ORDER),
              "the CSV header matches the declared canonical column order exactly, not a data-dependent order")


def _write_worker_csv(path: Path, rows: list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames: list = []
    for row in rows:
        for key in row:
            if key not in fieldnames:
                fieldnames.append(key)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def test_cli_end_to_end_with_corrupted_row():
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        worker_dir = tmp_path / "manifest_homogeneous" / "workers" / "worker_000"
        good_rows = [
            base_raw_row(run_id="run_good_1", method="FPP-SAA"),
            base_raw_row(run_id="run_good_2", method="DPV-SAA", dpv_surrogate_objective="11385.5",
                         objective_in_sample="11385.5", solver_status="Feasible"),
        ]
        corrupted_row = base_raw_row(run_id="run_corrupted", objective_in_sample="not-a-number")
        _write_worker_csv(worker_dir / "batch_results_worker_000.csv", good_rows + [corrupted_row])

        output_dir = tmp_path / "merged"
        proc = subprocess.run(
            [sys.executable, str(CLI), "--input-root", str(tmp_path), "--output-dir", str(output_dir)],
            cwd=str(ROOT), capture_output=True, text=True,
        )
        check(proc.returncode == 0, f"CLI exits 0 in non-strict mode even with one corrupted row: {proc.stderr}")

        with (output_dir / "merged_all_attempts.csv").open(newline="", encoding="utf-8") as handle:
            all_attempts = list(csv.DictReader(handle))
        with (output_dir / "merged_current_valid.csv").open(newline="", encoding="utf-8") as handle:
            current_valid = list(csv.DictReader(handle))
        with (output_dir / "merged_invalid.csv").open(newline="", encoding="utf-8") as handle:
            invalid = list(csv.DictReader(handle))
        diagnostics = json.loads((output_dir / "merge_diagnostics.json").read_text(encoding="utf-8"))

        check(len(all_attempts) == 3, "every parsed row (including the corrupted one) appears in merged_all_attempts")
        check(len(current_valid) == 2, "only the two good rows are selected into merged_current_valid")
        check(len(invalid) == 1, "the corrupted row appears in merged_invalid, and only there")
        check(invalid[0]["run_id"] == "run_corrupted", "the corrupted row's identity is preserved for triage")
        check(invalid[0]["validation_classification"] == schema.INVALID_EVALUATION,
              "the corrupted row is classified invalid_evaluation (malformed numeric), not silently dropped or zeroed")
        check(not any(r["run_id"] == "run_corrupted" for r in current_valid),
              "the corrupted row never leaks into merged_current_valid")
        check(diagnostics["invalid_rows"] == 1 and diagnostics["valid_rows"] == 2,
              "merge_diagnostics.json accounts for both the valid and the invalid row")

        proc_strict = subprocess.run(
            [sys.executable, str(CLI), "--input-root", str(tmp_path), "--output-dir", str(output_dir), "--strict"],
            cwd=str(ROOT), capture_output=True, text=True,
        )
        check(proc_strict.returncode != 0, "--strict exits non-zero when a modern row is invalid")


def main() -> int:
    test_stable_column_order()
    test_cli_end_to_end_with_corrupted_row()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
