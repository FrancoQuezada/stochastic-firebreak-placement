#!/usr/bin/env python3
"""Generate deterministic shared train/test splits for the FPP risk study."""

from __future__ import annotations

import argparse
from pathlib import Path

import fpp_risk_study_config as cfg


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate shared Sub20 FPP risk-objective train/test splits.")
    parser.add_argument("--landscape", default=cfg.DEFAULT_LANDSCAPE)
    parser.add_argument("--alphas", default=",".join(cfg.format_float(v) for v in cfg.DEFAULT_ALPHAS),
                        help="Accepted for grid symmetry; splits do not depend on alpha.")
    parser.add_argument("--lambdas", default=",".join(cfg.format_float(v) for v in cfg.DEFAULT_LAMBDAS),
                        help="Accepted for grid symmetry; splits do not depend on lambda.")
    parser.add_argument("--train-count", type=int, default=cfg.DEFAULT_TRAIN_COUNT)
    parser.add_argument("--test-count", type=int, default=cfg.DEFAULT_TEST_COUNT)
    parser.add_argument("--case-ids", default=",".join(str(v) for v in cfg.DEFAULT_CASE_IDS))
    parser.add_argument("--seed-base", type=int, default=cfg.DEFAULT_SEED_BASE)
    parser.add_argument("--results-path", type=Path, default=None)
    parser.add_argument("--output-dir", type=Path, default=cfg.DEFAULT_OUTPUT_DIR)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    case_ids = cfg.parse_int_list(args.case_ids)
    if args.train_count <= 0 or args.test_count <= 0:
        raise SystemExit("--train-count and --test-count must be positive.")
    if not case_ids:
        raise SystemExit("--case-ids must include at least one case.")

    results_path = args.results_path or cfg.default_results_path(args.landscape)
    scenario_ids = cfg.available_scenario_ids(results_path)
    layout = cfg.output_layout(args.output_dir)
    split_dir = layout["splits"]

    print(f"Landscape: {args.landscape}")
    print(f"Results path: {results_path}")
    print(f"Available scenarios: {len(scenario_ids)}")
    print(f"Split directory: {split_dir}")
    print("Splits are shared across alpha/lambda values for each case.")

    for case_id in case_ids:
        seed = cfg.seed_for_case(args.seed_base, case_id)
        key = cfg.split_key(args.landscape, args.train_count, args.test_count, case_id, seed)
        train_ids, test_ids = cfg.generate_split_ids(
            scenario_ids, seed, args.train_count, args.test_count)
        json_path = split_dir / f"{key}.json"
        if args.dry_run:
            print(
                f"DRY RUN {key}: train={len(train_ids)} test={len(test_ids)} "
                f"first_train={train_ids[:5]} first_test={test_ids[:5]}")
        else:
            written = cfg.write_split_files(
                split_dir,
                landscape=args.landscape,
                train_count=args.train_count,
                test_count=args.test_count,
                case_id=case_id,
                seed=seed,
                train_ids=train_ids,
                test_ids=test_ids)
            print(f"Wrote {written}")
        if not args.dry_run and not json_path.exists():
            raise RuntimeError(f"Expected split file was not created: {json_path}")

    print(f"Generated split definitions: {len(case_ids)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
