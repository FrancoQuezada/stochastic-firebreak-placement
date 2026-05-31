#!/usr/bin/env python3
"""Run generated FPP risk-study commands with optional parallelism."""

from __future__ import annotations

import argparse
import csv
import shlex
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

import fpp_risk_study_config as cfg


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run commands from an FPP risk-study manifest or command file.")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--manifest", type=Path)
    source.add_argument("--command-file", type=Path)
    parser.add_argument("--parallel", type=int, default=1)
    parser.add_argument("--skip-existing", action="store_true")
    parser.add_argument("--only-missing", action="store_true",
                        help="Alias for --skip-existing; retained for explicit rerun workflows.")
    parser.add_argument("--max-runs", type=int, default=0)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--method-filter", default=None,
                        help="Comma-separated substrings matched against method metadata in manifest mode.")
    parser.add_argument("--runner-log", type=Path, default=None)
    return parser.parse_args()


def extract_output_json(command: str) -> str:
    command_part = command.split(">", 1)[0].strip()
    try:
        tokens = shlex.split(command_part)
    except ValueError:
        return ""
    for index, token in enumerate(tokens):
        if token == "--output-json" and index + 1 < len(tokens):
            return tokens[index + 1]
    return ""


def load_manifest(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def load_command_file(path: Path) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for index, raw in enumerate(path.read_text(encoding="utf-8").splitlines()):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        json_path = extract_output_json(line)
        rows.append({
            "run_id": f"command_{index}",
            "method_key": "",
            "method_family": "",
            "method_variant": "",
            "json_path": json_path,
            "command": line,
        })
    return rows


def row_matches_filter(row: dict[str, str], filter_text: str | None) -> bool:
    if not filter_text:
        return True
    haystack = " ".join([
        row.get("method_key", ""),
        row.get("method_family", ""),
        row.get("method_variant", ""),
        row.get("method_label", ""),
    ]).lower()
    return any(piece.strip().lower() in haystack for piece in filter_text.split(",") if piece.strip())


def should_skip(row: dict[str, str], skip_existing: bool) -> bool:
    if not skip_existing:
        return False
    json_path = row.get("json_path") or extract_output_json(row.get("command", ""))
    return bool(json_path) and Path(json_path).exists()


def run_one(row: dict[str, str]) -> dict[str, str]:
    command = row["command"]
    started = time.time()
    completed = subprocess.run(command, shell=True, cwd=cfg.PROJECT_DIR)
    ended = time.time()
    status = "ok" if completed.returncode == 0 else "failed"
    return {
        "run_id": row.get("run_id", ""),
        "method_key": row.get("method_key", ""),
        "status": status,
        "return_code": str(completed.returncode),
        "runtime_seconds": f"{ended - started:.6f}",
        "json_path": row.get("json_path", ""),
        "command": command,
    }


def write_runner_log(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = ["run_id", "method_key", "status", "return_code", "runtime_seconds", "json_path", "command"]
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    if args.parallel <= 0:
        raise SystemExit("--parallel must be positive.")
    rows = load_manifest(args.manifest) if args.manifest else load_command_file(args.command_file)
    rows = [row for row in rows if row_matches_filter(row, args.method_filter)]
    skip_existing = args.skip_existing or args.only_missing
    queued: list[dict[str, str]] = []
    skipped = 0
    for row in rows:
        if should_skip(row, skip_existing):
            skipped += 1
            continue
        queued.append(row)
    if args.max_runs > 0:
        queued = queued[:args.max_runs]

    print(f"Loaded rows: {len(rows)}")
    print(f"Skipped existing outputs: {skipped}")
    print(f"Queued commands: {len(queued)}")
    print(f"Parallel workers: {args.parallel}")
    if args.dry_run:
        for row in queued[:10]:
            print(row["command"])
        if len(queued) > 10:
            print(f"... {len(queued) - 10} more commands")
        return 0

    if not queued:
        print("No commands queued; runner log was not modified.")
        return 0

    results: list[dict[str, str]] = []
    with ThreadPoolExecutor(max_workers=args.parallel) as executor:
        futures = [executor.submit(run_one, row) for row in queued]
        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            print(f"{result['status']}: {result['run_id']} ({result['runtime_seconds']}s)")

    if args.runner_log:
        log_path = args.runner_log
    else:
        source = args.manifest if args.manifest else args.command_file
        output_root = source.parent.parent if source.parent.name == "commands" else cfg.DEFAULT_OUTPUT_DIR
        source_stem = source.stem
        log_path = output_root / "logs" / "runner" / f"{source_stem}_runner_log.csv"
    write_runner_log(log_path, results)
    failures = sum(1 for row in results if row["status"] != "ok")
    print(f"Runner log: {log_path}")
    print(f"Failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
