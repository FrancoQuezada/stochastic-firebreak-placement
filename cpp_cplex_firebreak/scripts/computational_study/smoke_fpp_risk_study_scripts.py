#!/usr/bin/env python3
"""Smoke-test the FPP risk-study script stack without solving models."""

from __future__ import annotations

import csv
import shutil
import subprocess
import sys
from pathlib import Path

import fpp_risk_study_config as cfg


SMOKE_OUTPUT_DIR = cfg.PROJECT_DIR / "results" / "computational_study_fpp_risk_smoke"


def run_command(tokens: list[str]) -> None:
    print("+ " + " ".join(tokens))
    subprocess.run(tokens, cwd=cfg.PROJECT_DIR, check=True)


def read_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    print("Import check: fpp_risk_study_config")
    require(cfg.expected_command_count([0.01], [0.0, 0.5, 1.0], [0], "all") == 13,
            "Tiny grid command count should be 13 with Phase 1T tail-blend support.")
    require(cfg.expected_command_count([0.01, 0.02, 0.03], [0.0, 0.5, 1.0],
                                       [0, 1, 2, 3, 4], "all",
                                       landscapes=["Sub20", "Sub40"],
                                       train_counts=[100, 200, 300]) == 1170,
            "Phase 1V full grid command count should be 1170.")

    if SMOKE_OUTPUT_DIR.exists():
        shutil.rmtree(SMOKE_OUTPUT_DIR)

    generator = "scripts/computational_study/generate_fpp_risk_command_manifest.py"
    run_command([
        sys.executable,
        generator,
        "--landscapes",
        "Sub20,Sub40",
        "--alphas",
        "0.01",
        "--lambdas",
        "0",
        "--train-counts",
        "100,200",
        "--case-ids",
        "0",
        "--dry-run",
    ])
    run_command([
        sys.executable,
        generator,
        "--alphas",
        "0.01",
        "--lambdas",
        "0,0.5,1",
        "--case-ids",
        "0",
        "--output-dir",
        str(SMOKE_OUTPUT_DIR),
    ])

    manifest_path = SMOKE_OUTPUT_DIR / "commands" / "full_command_manifest.csv"
    rows = read_manifest(manifest_path)
    require(len(rows) == 13, f"Expected 13 tiny manifest rows, found {len(rows)}.")
    require(len({row["run_id"] for row in rows}) == len(rows), "Run IDs are not unique.")
    require(len({row["split_key"] for row in rows}) == 1, "Tiny grid should reuse one shared split.")

    by_lambda = {value: [row for row in rows if row["lambda"] == value] for value in ["0", "0.5", "1"]}
    require(all(row["risk_measure"] == "expected" for row in by_lambda["0"]),
            "Lambda 0 rows must use expected risk measure.")
    require(all("--risk-measure expected" in row["command"] for row in by_lambda["0"]),
            "Lambda 0 commands must include --risk-measure expected.")
    require(all(row["risk_measure"] == "mean-cvar" for row in by_lambda["0.5"]),
            "Lambda 0.5 rows must use mean-cvar.")
    require(all("--risk-measure mean-cvar" in row["command"] and "--cvar-lambda 0.5" in row["command"]
                for row in by_lambda["0.5"]),
            "Lambda 0.5 commands must include mean-cvar and cvar-lambda flags.")
    require(all(row["risk_measure"] == "cvar" for row in by_lambda["1"]),
            "Lambda 1 rows must use cvar.")
    require(all("--risk-measure cvar" in row["command"] for row in by_lambda["1"]),
            "Lambda 1 commands must include --risk-measure cvar.")
    tailblend_rows = [row for row in rows if row["method_key"] == "restricted-tailblend-maintenance"]
    require(len(tailblend_rows) == 1, "Tail-blend should be emitted only for pure CVaR in Phase 1T.")
    require(tailblend_rows[0]["lambda"] == "1", "Tail-blend row should have lambda 1.")

    expected_files = [
        "full_command_manifest.sh",
        "commands_fpp_saa.sh",
        "commands_fpp_branch_benders.sh",
        "commands_restricted_generic_no_maintenance.sh",
        "commands_restricted_generic_maintenance.sh",
        "commands_restricted_tailblend_maintenance.sh",
        "commands_all_heuristics.sh",
        "commands_all_exact.sh",
        "commands_all.sh",
        "commands_Sub20.sh",
        "commands_lambda0.sh",
        "commands_exact_lambda0.sh",
        "commands_heuristics_Sub20.sh",
    ]
    for filename in expected_files:
        path = SMOKE_OUTPUT_DIR / "commands" / filename
        require(path.exists(), f"Missing command file: {path}")

    run_command([
        sys.executable,
        "scripts/computational_study/aggregate_fpp_risk_results.py",
        "--output-dir",
        str(SMOKE_OUTPUT_DIR),
    ])
    require((SMOKE_OUTPUT_DIR / "summaries" / "fpp_risk_run_summary.csv").exists(),
            "Aggregator did not write run summary.")
    run_command([
        sys.executable,
        "scripts/computational_study/monitor_fpp_risk_study.py",
        "--output-dir",
        str(SMOKE_OUTPUT_DIR),
        "--stage-name",
        "smoke",
    ])
    require((SMOKE_OUTPUT_DIR / "monitoring" / "stage_smoke_validation.json").exists(),
            "Monitor did not write validation JSON.")

    print("Smoke passed.")
    print(f"Tiny manifest rows: {len(rows)}")
    print(f"Tail-blend rows generated: {len(tailblend_rows)}")
    print(f"Output directory: {SMOKE_OUTPUT_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
