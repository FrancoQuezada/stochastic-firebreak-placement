#!/usr/bin/env python3
"""Generate command manifests for the FPP risk-objective computational study."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Iterable

import fpp_risk_study_config as cfg


GROUP_FILES = {
    "fpp-saa": "commands_fpp_saa.sh",
    "fpp-bb": "commands_fpp_branch_benders.sh",
    "restricted-generic-no-maintenance": "commands_restricted_generic_no_maintenance.sh",
    "restricted-generic-maintenance": "commands_restricted_generic_maintenance.sh",
    "restricted-tailblend-maintenance": "commands_restricted_tailblend_maintenance.sh",
}

MANIFEST_FIELDS = [
    "run_id",
    "method_key",
    "method_family",
    "method_variant",
    "method_label",
    "landscape",
    "alpha",
    "lambda",
    "lambda_token",
    "risk_measure",
    "cvar_beta",
    "cvar_lambda",
    "case_id",
    "seed",
    "split_key",
    "split_file",
    "train_count",
    "test_count",
    "train_ids",
    "test_ids",
    "time_limit",
    "threads",
    "mip_gap",
    "heuristic_config",
    "initial_candidate_size",
    "candidate_activation_policy",
    "candidate_activation_batch_size",
    "max_candidate_rounds",
    "candidate_maintenance_policy",
    "candidate_deactivation_batch_size",
    "candidate_min_active_size",
    "candidate_max_active_size",
    "candidate_deactivation_min_age",
    "candidate_reactivation_cooldown_rounds",
    "candidate_score_mode",
    "candidate_tail_score_gamma",
    "candidate_tail_protection_size",
    "restricted_heuristic_mode",
    "exact_baseline",
    "json_path",
    "log_path",
    "solution_json_path",
    "solution_csv_path",
    "output_csv_path",
    "command",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate FPP risk-objective computational-study command manifests.")
    parser.add_argument("--landscape", default=cfg.DEFAULT_LANDSCAPE)
    parser.add_argument("--landscapes", default=None,
                        help="Comma-separated landscapes. When provided, overrides --landscape.")
    parser.add_argument("--alphas", default=",".join(cfg.format_float(v) for v in cfg.DEFAULT_ALPHAS))
    parser.add_argument("--lambdas", default=",".join(cfg.format_float(v) for v in cfg.DEFAULT_LAMBDAS))
    parser.add_argument("--train-count", type=int, default=cfg.DEFAULT_TRAIN_COUNT)
    parser.add_argument("--train-counts", default=None,
                        help="Comma-separated train scenario counts. When provided, overrides --train-count.")
    parser.add_argument("--test-count", type=int, default=cfg.DEFAULT_TEST_COUNT)
    parser.add_argument("--case-ids", default=",".join(str(v) for v in cfg.DEFAULT_CASE_IDS))
    parser.add_argument("--seed-base", type=int, default=cfg.DEFAULT_SEED_BASE)
    parser.add_argument("--time-limit", type=float, default=cfg.DEFAULT_TIME_LIMIT_SECONDS)
    parser.add_argument("--threads", type=int, default=cfg.DEFAULT_THREADS)
    parser.add_argument("--mip-gap", type=float, default=cfg.DEFAULT_MIP_GAP)
    parser.add_argument("--cvar-beta", type=float, default=cfg.DEFAULT_CVAR_BETA)
    parser.add_argument("--forest-path", type=Path, default=None)
    parser.add_argument("--results-path", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path, default=cfg.DEFAULT_OUTPUT_DIR)
    parser.add_argument("--methods", default="all",
                        help="Comma-separated filters: all,fpp-saa,fpp-bb,restricted-generic,restricted-tailblend,restricted.")
    parser.add_argument("--heuristic-config", choices=sorted(cfg.HEURISTIC_CONFIGS),
                        default="compact")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def comma_join_ints(values: Iterable[int]) -> str:
    return ",".join(str(value) for value in values)


def normalize_path(path: Path) -> str:
    return str(path.resolve())


def load_or_create_split(
    *,
    landscape: str,
    results_path: Path,
    split_dir: Path,
    train_count: int,
    test_count: int,
    case_id: int,
    seed: int,
    dry_run: bool) -> tuple[list[int], list[int], Path, str]:
    key = cfg.split_key(landscape, train_count, test_count, case_id, seed)
    split_path = split_dir / f"{key}.json"
    if split_path.exists():
        payload = cfg.read_split_file(split_path)
        return list(payload["train_ids"]), list(payload["test_ids"]), split_path, key

    scenario_ids = cfg.available_scenario_ids(results_path)
    train_ids, test_ids = cfg.generate_split_ids(
        scenario_ids, seed, train_count, test_count)
    if not dry_run:
        cfg.write_split_files(
            split_dir,
            landscape=landscape,
            train_count=train_count,
            test_count=test_count,
            case_id=case_id,
            seed=seed,
            train_ids=train_ids,
            test_ids=test_ids)
    return train_ids, test_ids, split_path, key


def restricted_flags(
    variant: cfg.MethodVariant,
    heuristic_config: cfg.HeuristicConfig,
    risk: cfg.RiskLevel) -> tuple[list[str], dict[str, str]]:
    maintenance_policy = "benders-coefficients" if variant.maintenance else "none"
    flags = [
        "--restricted-heuristic-mode",
        "--initial-candidate-policy",
        "burn-frequency",
        "--initial-candidate-size",
        str(heuristic_config.initial_candidate_size),
        "--candidate-activation-policy",
        "benders-coefficients",
        "--candidate-activation-batch-size",
        str(heuristic_config.activation_batch_size),
        "--max-candidate-rounds",
        str(heuristic_config.max_candidate_rounds),
        "--candidate-maintenance-policy",
        maintenance_policy,
        "--stop-after-candidate-rounds",
        str(heuristic_config.max_candidate_rounds),
    ]
    if variant.maintenance:
        flags.extend([
            "--candidate-deactivation-batch-size",
            str(heuristic_config.deactivation_batch_size),
            "--candidate-min-active-size",
            str(heuristic_config.min_active_size),
            "--candidate-max-active-size",
            str(heuristic_config.max_active_size),
            "--candidate-deactivation-min-age",
            str(heuristic_config.deactivation_min_age),
            "--candidate-reactivation-cooldown-rounds",
            str(heuristic_config.reactivation_cooldown_rounds),
            "--candidate-score-mode",
            variant.score_mode,
        ])
    if variant.score_mode == "cvar-tail-blend":
        flags.extend([
            "--candidate-tail-score-gamma",
            cfg.format_float(heuristic_config.tail_score_gamma),
            "--candidate-tail-protection-size",
            str(heuristic_config.tail_protection_size),
        ])
    metadata = {
        "initial_candidate_size": str(heuristic_config.initial_candidate_size),
        "candidate_activation_policy": "benders-coefficients",
        "candidate_activation_batch_size": str(heuristic_config.activation_batch_size),
        "max_candidate_rounds": str(heuristic_config.max_candidate_rounds),
        "candidate_maintenance_policy": maintenance_policy,
        "candidate_deactivation_batch_size": str(heuristic_config.deactivation_batch_size if variant.maintenance else ""),
        "candidate_min_active_size": str(heuristic_config.min_active_size if variant.maintenance else ""),
        "candidate_max_active_size": str(heuristic_config.max_active_size if variant.maintenance else ""),
        "candidate_deactivation_min_age": str(heuristic_config.deactivation_min_age if variant.maintenance else ""),
        "candidate_reactivation_cooldown_rounds": str(heuristic_config.reactivation_cooldown_rounds if variant.maintenance else ""),
        "candidate_score_mode": variant.score_mode if variant.maintenance else "",
        "candidate_tail_score_gamma": cfg.format_float(heuristic_config.tail_score_gamma)
        if variant.score_mode == "cvar-tail-blend" else "",
        "candidate_tail_protection_size": str(heuristic_config.tail_protection_size)
        if variant.score_mode == "cvar-tail-blend" else "",
    }
    if variant.score_mode == "cvar-tail-blend" and risk.risk_measure != "cvar":
        raise ValueError("cvar-tail-blend command generation is only supported for pure CVaR in Phase 1T.")
    return flags, metadata


def build_row(
    *,
    variant: cfg.MethodVariant,
    risk: cfg.RiskLevel,
    heuristic_config: cfg.HeuristicConfig,
    landscape: str,
    alpha: float,
    case_id: int,
    seed: int,
    split_key: str,
    split_file: Path,
    train_ids: list[int],
    test_ids: list[int],
    forest_path: Path,
    results_path: Path,
    layout: dict[str, Path],
    time_limit: float,
    threads: int,
    mip_gap: float,
    run_id_train_count: int | None = None) -> dict[str, str]:
    run_id = cfg.run_id_for(
        landscape, alpha, risk, case_id, variant, heuristic_config,
        train_count=run_id_train_count)
    json_path = layout["json"] / f"{run_id}.json"
    log_path = layout["logs"] / f"{run_id}.log"
    solution_json_path = layout["solutions"] / f"{run_id}_solution.json"
    solution_csv_path = layout["solutions"] / f"{run_id}_solution.csv"
    output_csv_path = layout["raw_csv"] / f"{run_id}.csv"

    tokens: list[str] = [
        "./build_gpp/firebreak_cpp",
        variant.command,
        "--landscape",
        landscape,
        "--forest-path",
        normalize_path(forest_path),
        "--results-path",
        normalize_path(results_path),
        "--train-ids",
        comma_join_ints(train_ids),
        "--test-ids",
        comma_join_ints(test_ids),
        "--alpha",
        cfg.format_float(alpha),
        "--run-id",
        run_id,
        "--time-limit",
        cfg.format_float(time_limit),
        "--mip-gap",
        cfg.format_float(mip_gap),
        "--threads",
        str(threads),
    ]
    tokens.extend(risk.cli_flags())
    tokens.extend([
        "--output-json",
        normalize_path(json_path),
        "--output-csv",
        normalize_path(output_csv_path),
        "--solution-json",
        normalize_path(solution_json_path),
        "--solution-csv",
        normalize_path(solution_csv_path),
    ])

    restricted_metadata = {
        "initial_candidate_size": "",
        "candidate_activation_policy": "",
        "candidate_activation_batch_size": "",
        "max_candidate_rounds": "",
        "candidate_maintenance_policy": "",
        "candidate_deactivation_batch_size": "",
        "candidate_min_active_size": "",
        "candidate_max_active_size": "",
        "candidate_deactivation_min_age": "",
        "candidate_reactivation_cooldown_rounds": "",
        "candidate_score_mode": "",
        "candidate_tail_score_gamma": "",
        "candidate_tail_protection_size": "",
    }
    if variant.restricted:
        flags, restricted_metadata = restricted_flags(variant, heuristic_config, risk)
        tokens.extend(flags)

    command = cfg.shell_join(tokens) + f" > {cfg.shell_join([normalize_path(log_path)])} 2>&1"
    return {
        "run_id": run_id,
        "method_key": variant.key,
        "method_family": variant.family,
        "method_variant": variant.variant,
        "method_label": variant.label(risk),
        "landscape": landscape,
        "alpha": cfg.format_float(alpha),
        "lambda": cfg.format_float(risk.lambda_value),
        "lambda_token": risk.lambda_token,
        "risk_measure": risk.risk_measure,
        "cvar_beta": cfg.format_float(risk.cvar_beta) if risk.cvar_beta is not None else "",
        "cvar_lambda": cfg.format_float(risk.cvar_lambda) if risk.cvar_lambda is not None else "",
        "case_id": str(case_id),
        "seed": str(seed),
        "split_key": split_key,
        "split_file": normalize_path(split_file),
        "train_count": str(len(train_ids)),
        "test_count": str(len(test_ids)),
        "train_ids": cfg.csv_join_ints(train_ids),
        "test_ids": cfg.csv_join_ints(test_ids),
        "time_limit": cfg.format_float(time_limit),
        "threads": str(threads),
        "mip_gap": cfg.format_float(mip_gap),
        "heuristic_config": heuristic_config.key if variant.restricted else "",
        "restricted_heuristic_mode": "true" if variant.heuristic else "false",
        "exact_baseline": "true" if variant.exact_baseline else "false",
        "json_path": normalize_path(json_path),
        "log_path": normalize_path(log_path),
        "solution_json_path": normalize_path(solution_json_path),
        "solution_csv_path": normalize_path(solution_csv_path),
        "output_csv_path": normalize_path(output_csv_path),
        "command": command,
        **restricted_metadata,
    }


def write_command_file(path: Path, commands: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(commands) + ("\n" if commands else ""), encoding="utf-8")
    mode = path.stat().st_mode
    path.chmod(mode | 0o755)


def write_outputs(
    rows: list[dict[str, str]],
    skipped: list[dict[str, str]],
    layout: dict[str, Path]) -> None:
    commands_dir = layout["commands"]
    commands_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = commands_dir / "full_command_manifest.csv"
    with manifest_path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=MANIFEST_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    all_commands = [row["command"] for row in rows]
    write_command_file(commands_dir / "full_command_manifest.sh", all_commands)
    write_command_file(commands_dir / "commands_all.sh", all_commands)

    for variant_key, filename in GROUP_FILES.items():
        commands = [row["command"] for row in rows if row["method_key"] == variant_key]
        write_command_file(commands_dir / filename, commands)

    exact_commands = [row["command"] for row in rows if row["exact_baseline"] == "true"]
    heuristic_commands = [row["command"] for row in rows if row["restricted_heuristic_mode"] == "true"]
    write_command_file(commands_dir / "commands_all_exact.sh", exact_commands)
    write_command_file(commands_dir / "commands_all_heuristics.sh", heuristic_commands)

    for landscape in sorted({row["landscape"] for row in rows}):
        landscape_commands = [row["command"] for row in rows if row["landscape"] == landscape]
        write_command_file(commands_dir / f"commands_{landscape}.sh", landscape_commands)
        heuristic_landscape_commands = [
            row["command"] for row in rows
            if row["landscape"] == landscape and row["restricted_heuristic_mode"] == "true"
        ]
        write_command_file(
            commands_dir / f"commands_heuristics_{landscape}.sh",
            heuristic_landscape_commands)

    for lambda_token in sorted({row["lambda_token"] for row in rows}):
        lambda_commands = [row["command"] for row in rows if row["lambda_token"] == lambda_token]
        exact_lambda_commands = [
            row["command"] for row in rows
            if row["lambda_token"] == lambda_token and row["exact_baseline"] == "true"
        ]
        write_command_file(commands_dir / f"commands_{lambda_token}.sh", lambda_commands)
        write_command_file(commands_dir / f"commands_exact_{lambda_token}.sh", exact_lambda_commands)

    skipped_path = commands_dir / "skipped_variants.csv"
    with skipped_path.open("w", newline="", encoding="utf-8") as out:
        fieldnames = ["landscape", "alpha", "lambda", "case_id", "method_key", "reason"]
        writer = csv.DictWriter(out, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(skipped)

    group_counts = {
        "all": len(rows),
        "exact": len(exact_commands),
        "heuristics": len(heuristic_commands),
    }
    for variant_key in GROUP_FILES:
        group_counts[variant_key] = sum(1 for row in rows if row["method_key"] == variant_key)
    for landscape in sorted({row["landscape"] for row in rows}):
        group_counts[f"landscape:{landscape}"] = sum(1 for row in rows if row["landscape"] == landscape)
    for lambda_token in sorted({row["lambda_token"] for row in rows}):
        group_counts[f"lambda:{lambda_token}"] = sum(1 for row in rows if row["lambda_token"] == lambda_token)
    summary = {
        "command_count": len(rows),
        "skipped_count": len(skipped),
        "group_counts": group_counts,
        "manifest_path": str(manifest_path),
        "skipped_path": str(skipped_path),
    }
    (commands_dir / "manifest_summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


def main() -> int:
    args = parse_args()
    alphas = cfg.parse_float_list(args.alphas)
    lambdas = cfg.parse_float_list(args.lambdas)
    case_ids = cfg.parse_int_list(args.case_ids)
    landscapes = cfg.parse_string_list(args.landscapes) if args.landscapes else [args.landscape]
    train_counts = cfg.parse_int_list(args.train_counts) if args.train_counts else [args.train_count]
    if not alphas or not lambdas or not case_ids:
        raise SystemExit("--alphas, --lambdas, and --case-ids must be nonempty.")
    if not landscapes:
        raise SystemExit("--landscape/--landscapes must be nonempty.")
    if not train_counts or any(value <= 0 for value in train_counts) or args.test_count <= 0:
        raise SystemExit("--train-count/--train-counts and --test-count must be positive.")
    if args.time_limit <= 0:
        raise SystemExit("--time-limit must be positive.")
    if args.threads <= 0:
        raise SystemExit("--threads must be positive.")
    if len(landscapes) > 1 and (args.forest_path is not None or args.results_path is not None):
        raise SystemExit("--forest-path and --results-path are only supported with one landscape.")

    variants = cfg.selected_variants(args.methods)
    heuristic_config = cfg.HEURISTIC_CONFIGS[args.heuristic_config]
    output_dir = args.output_dir
    layout = cfg.output_layout(output_dir) if args.dry_run else cfg.ensure_output_layout(output_dir)
    include_train_count_in_run_id = len(train_counts) > 1

    rows: list[dict[str, str]] = []
    skipped: list[dict[str, str]] = []
    for landscape in landscapes:
        forest_path = args.forest_path or cfg.default_forest_path(landscape)
        results_path = args.results_path or cfg.default_results_path(landscape)
        for train_count in train_counts:
            for alpha in alphas:
                for lambda_value in lambdas:
                    risk = cfg.risk_level_for_lambda(lambda_value, args.cvar_beta)
                    for case_id in case_ids:
                        seed = cfg.seed_for_case(args.seed_base, case_id)
                        train_ids, test_ids, split_file, split_key = load_or_create_split(
                            landscape=landscape,
                            results_path=results_path,
                            split_dir=layout["splits"],
                            train_count=train_count,
                            test_count=args.test_count,
                            case_id=case_id,
                            seed=seed,
                            dry_run=args.dry_run)
                        for variant in variants:
                            if not cfg.variant_supported_for_lambda(variant, risk.lambda_value):
                                skipped.append({
                                    "landscape": landscape,
                                    "alpha": cfg.format_float(alpha),
                                    "lambda": cfg.format_float(risk.lambda_value),
                                    "case_id": str(case_id),
                                    "method_key": variant.key,
                                    "reason": "unsupported lambda/risk mode for this variant",
                                })
                                continue
                            rows.append(build_row(
                                variant=variant,
                                risk=risk,
                                heuristic_config=heuristic_config,
                                landscape=landscape,
                                alpha=alpha,
                                case_id=case_id,
                                seed=seed,
                                split_key=split_key,
                                split_file=split_file,
                                train_ids=train_ids,
                                test_ids=test_ids,
                                forest_path=forest_path,
                                results_path=results_path,
                                layout=layout,
                                time_limit=args.time_limit,
                                threads=args.threads,
                                mip_gap=args.mip_gap,
                                run_id_train_count=train_count if include_train_count_in_run_id else None))

    expected = cfg.expected_command_count(
        alphas, lambdas, case_ids, args.methods,
        landscapes=landscapes,
        train_counts=train_counts)
    if len(rows) != expected:
        raise RuntimeError(f"Internal command-count mismatch: produced {len(rows)}, expected {expected}.")
    if len({row["run_id"] for row in rows}) != len(rows):
        raise RuntimeError("Generated run IDs are not unique.")

    print(f"Output directory: {output_dir}")
    print(f"Landscapes: {','.join(landscapes)}")
    print(f"Train counts: {','.join(str(value) for value in train_counts)}")
    print(f"Methods: {args.methods}")
    print(f"Commands generated: {len(rows)}")
    print(f"Skipped unsupported variants: {len(skipped)}")
    print(f"Default Phase 1T grid expected commands: {cfg.expected_command_count(cfg.DEFAULT_ALPHAS, cfg.DEFAULT_LAMBDAS, cfg.DEFAULT_CASE_IDS, 'all')}")
    print(f"Phase 1V full-grid expected commands: {cfg.expected_command_count([0.01, 0.02, 0.03], [0.0, 0.5, 1.0], cfg.DEFAULT_CASE_IDS, 'all', landscapes=cfg.DEFAULT_FULL_LANDSCAPES, train_counts=cfg.DEFAULT_FULL_TRAIN_COUNTS)}")
    print("Group counts:")
    for key in ["fpp-saa", "fpp-bb", "restricted-generic-no-maintenance",
                "restricted-generic-maintenance", "restricted-tailblend-maintenance"]:
        print(f"  {key}: {sum(1 for row in rows if row['method_key'] == key)}")

    if args.dry_run:
        print("Dry run only; no command files were written.")
        if rows:
            print("First command:")
            print(rows[0]["command"])
        return 0

    write_outputs(rows, skipped, layout)
    print(f"Wrote manifest: {layout['commands'] / 'full_command_manifest.csv'}")
    print(f"Wrote grouped command files under: {layout['commands']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
