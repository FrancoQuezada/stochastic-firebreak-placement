#!/usr/bin/env python3
"""Phase 9A canonical merge CLI for weighted-landscape experiment results.

Reads modern Phase 8B per-worker CSV rows (`batch_results_worker_*.csv`,
each optionally paired with per-task solver/paired-reburn-evaluation JSON
files) and, optionally, explicit legacy CSV files, normalizes every row into
the canonical schema (weighted_result_schema.py), validates and
deduplicates them (weighted_result_merge.py), and writes four deterministic
output artifacts:

  merged_all_attempts.csv   every parsed attempt + its validation classification
  merged_current_valid.csv one selected valid row per logical run
  merged_invalid.csv        invalid/conflicting/incomplete rows with reasons
  merge_diagnostics.json    counts and provenance for the whole merge

This script never runs a solver, never regenerates a weight map, and never
computes a final performance summary -- it only merges and validates data
that already exists on disk.

Usage:
    python3 scripts/merge_weighted_experiment_results.py \\
        --input-root results/weighted_phase8b_smoke \\
        --output-dir results/weighted_phase8b_smoke/merged \\
        --strict
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import weighted_result_merge as merge
import weighted_result_normalize as norm
import weighted_result_schema as schema

ROOT = Path(__file__).resolve().parent.parent

_OUTPUT_FIELD_ORDER = tuple(schema.CANONICAL_FIELD_ORDER) + tuple(schema.VALIDATION_META_FIELD_ORDER)


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--input-root", type=Path, action="append", default=[],
        help="Directory to search recursively for batch_results_worker_*.csv. Repeatable.",
    )
    parser.add_argument(
        "--input-csv", type=Path, action="append", default=[],
        help="Explicit modern worker CSV path to include. Repeatable.",
    )
    parser.add_argument(
        "--legacy-csv", type=Path, action="append", default=[],
        help="Explicit legacy (pre-8A/8B or pre-weighted) CSV path to include. Repeatable.",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--strict", action="store_true",
        help="Exit non-zero if any modern (non-legacy) row fails validation.",
    )
    parser.add_argument(
        "--allow-legacy", action="store_true",
        help="Migrate legacy rows (from --legacy-csv, or legacy-shaped rows found under --input-root) "
             "into valid_legacy_migrated. Without this flag, legacy-shaped rows are rejected as invalid_schema.",
    )
    parser.add_argument(
        "--include-failed", action="store_true",
        help="Include worker rows with a non-zero worker_return_code (classified incomplete) in the merge. "
             "Without this flag they are skipped at discovery time.",
    )
    parser.add_argument(
        "--prefer-json", dest="prefer_json", action="store_true", default=True,
        help="Prefer per-task JSON over the worker CSV when both exist for one run (default: on).",
    )
    parser.add_argument("--no-prefer-json", dest="prefer_json", action="store_false")
    parser.add_argument(
        "--schema-version", type=str, default=None,
        help="Reject any row whose detected schema version is not exactly this value.",
    )
    parser.add_argument(
        "--validate-paired", dest="validate_paired", action="store_true", default=True,
        help="Enforce paired-reburn evaluation completeness for rows with paired_evaluation_enabled=true (default: on).",
    )
    parser.add_argument("--no-validate-paired", dest="validate_paired", action="store_false")
    return parser.parse_args(argv)


def _read_csv_rows(path: Path) -> list:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def discover_worker_csvs(root: Path) -> list:
    return sorted(root.rglob("batch_results_worker_*.csv"))


def _passes_failed_filter(raw_row: dict, include_failed: bool) -> bool:
    if include_failed:
        return True
    return_code = (raw_row.get("worker_return_code") or "").strip()
    return return_code in ("", "0")


def collect_records(args: argparse.Namespace) -> list:
    csv_paths: list = []
    for root in args.input_root:
        csv_paths.extend(discover_worker_csvs(root))
    csv_paths.extend(args.input_csv)

    records: list = []
    for path in sorted(set(csv_paths)):
        for raw_row in _read_csv_rows(path):
            if not _passes_failed_filter(raw_row, args.include_failed):
                continue
            records.append(norm.normalize_row(
                raw_row, source_file=str(path), source_format="worker_csv", prefer_json=args.prefer_json,
            ))

    for path in sorted(set(args.legacy_csv)):
        for raw_row in _read_csv_rows(path):
            if not _passes_failed_filter(raw_row, args.include_failed):
                continue
            records.append(norm.normalize_row(
                raw_row, source_file=str(path), source_format="legacy_csv", prefer_json=False,
            ))

    return records


def _stringify(value) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, (list, tuple)):
        return ",".join(str(v) for v in value)
    return str(value)


def write_records_csv(path: Path, records: list) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=list(_OUTPUT_FIELD_ORDER), extrasaction="ignore")
        writer.writeheader()
        for record in records:
            writer.writerow({name: _stringify(record.get(name)) for name in _OUTPUT_FIELD_ORDER})


def main(argv=None) -> int:
    args = parse_args(argv)
    if not args.input_root and not args.input_csv and not args.legacy_csv:
        print("error: at least one of --input-root/--input-csv/--legacy-csv is required", file=sys.stderr)
        return 2

    records = collect_records(args)
    if not records:
        print("error: no input rows discovered", file=sys.stderr)
        return 2

    result = merge.build_merge_outputs(
        records,
        schema_version_filter=args.schema_version,
        allow_legacy=args.allow_legacy,
        validate_paired=args.validate_paired,
    )

    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_records_csv(args.output_dir / "merged_all_attempts.csv", result["all_attempts"])
    write_records_csv(args.output_dir / "merged_current_valid.csv", result["current_valid"])
    write_records_csv(args.output_dir / "merged_invalid.csv", result["invalid"])

    diagnostics = dict(result["diagnostics"])
    diagnostics["input_files"] = [str(p) for p in sorted(set(
        list(args.input_csv) + list(args.legacy_csv)
        + [p for root in args.input_root for p in discover_worker_csvs(root)]
    ))]
    diagnostics["invalid_source_files"] = sorted({
        r["source_file"] for r in result["invalid"] if r.get("source_file")
    })
    (args.output_dir / "merge_diagnostics.json").write_text(
        json.dumps(diagnostics, indent=2, sort_keys=True) + "\n", encoding="utf-8",
    )

    print(f"Parsed {diagnostics['parsed_rows']} rows from {len(diagnostics['input_files'])} file(s).")
    print(f"  valid={diagnostics['valid_rows']} legacy_migrated={diagnostics['legacy_migrated_rows']} "
          f"invalid={diagnostics['invalid_rows']} incomplete={diagnostics['incomplete_rows']}")
    print(f"  logical_runs={diagnostics['logical_runs']} current_valid_runs={diagnostics['current_valid_runs']}")
    print(f"Wrote {args.output_dir / 'merged_all_attempts.csv'}")
    print(f"Wrote {args.output_dir / 'merged_current_valid.csv'}")
    print(f"Wrote {args.output_dir / 'merged_invalid.csv'}")
    print(f"Wrote {args.output_dir / 'merge_diagnostics.json'}")

    if args.strict:
        strict_invalid = sum(
            1 for r in result["invalid"] if r.get("migration_status") == "modern"
        )
        if strict_invalid:
            print(f"error: --strict is set and {strict_invalid} modern row(s) are invalid", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
