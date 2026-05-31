#!/usr/bin/env python3
"""Prepare or validate shared Sub20 splits for the alpha 0.01/0.02 FPP study."""

from __future__ import annotations

import argparse
import json
import random
import re
from pathlib import Path


LANDSCAPE = "Sub20"
DEFAULT_RESULTS_DIR = Path("results/batch/sub20_alpha001_002_fpp_exact_tl300")
PREVIOUS_SPLIT_DIR = Path("results/batch/sub20_100train_900test_5cases_alpha002_003_tl300/splits")
RESULTS_PATH = Path("../sample_test/Sub20")
TRAIN_COUNT = 100
TEST_COUNT = 900
SEED_BASE = 20260601
NUM_CASES = 5


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results-dir", type=Path, default=DEFAULT_RESULTS_DIR)
    parser.add_argument("--previous-split-dir", type=Path, default=PREVIOUS_SPLIT_DIR)
    parser.add_argument("--results-path", type=Path, default=RESULTS_PATH)
    parser.add_argument("--force-new", action="store_true")
    return parser.parse_args()


def scenario_ids(results_path: Path) -> list[int]:
    messages_dir = results_path / "Messages"
    if not messages_dir.is_dir():
        raise RuntimeError(f"Missing Messages directory: {messages_dir}")
    ids: list[int] = []
    pattern = re.compile(r"^MessagesFile([0-9]+)\.csv$")
    for path in messages_dir.iterdir():
        match = pattern.match(path.name)
        if match:
            ids.append(int(match.group(1)))
    ids = sorted(set(ids))
    if len(ids) < TRAIN_COUNT + TEST_COUNT:
        raise RuntimeError(
            f"Sub20 has {len(ids)} scenario files; need at least {TRAIN_COUNT + TEST_COUNT}.")
    return ids


def read_ids(path: Path) -> list[int]:
    text = path.read_text(encoding="utf-8")
    tokens = re.split(r"[\s,;]+", text.strip())
    return [int(token) for token in tokens if token]


def write_ids(path: Path, ids: list[int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(str(value) for value in ids) + "\n", encoding="utf-8")


def cxx_split_paths(split_dir: Path, case_index: int, seed: int) -> tuple[Path, Path]:
    prefix = f"{LANDSCAPE}_seed{seed}_train{TRAIN_COUNT}_test{TEST_COUNT}_case{case_index}"
    return split_dir / f"{prefix}_train.csv", split_dir / f"{prefix}_test.csv"


def case_pattern(case_index: int) -> re.Pattern[str]:
    return re.compile(rf"(^|[^0-9])case0?{case_index}([^0-9]|$)")


def find_split_pair(split_dir: Path, case_index: int, seed: int | None = None) -> tuple[Path, Path] | None:
    if seed is not None:
        train_path, test_path = cxx_split_paths(split_dir, case_index, seed)
        if train_path.exists() and test_path.exists():
            return train_path, test_path

    if not split_dir.is_dir():
        return None
    pattern = case_pattern(case_index)
    train_candidates = sorted(
        path for path in split_dir.iterdir()
        if path.is_file()
        and pattern.search(path.name)
        and re.search(r"_train(_ids)?\.(csv|txt)$", path.name)
    )
    for train_path in train_candidates:
        test_names = [
            train_path.name.replace("_train_ids.", "_test_ids."),
            train_path.name.replace("_train.", "_test."),
        ]
        for test_name in test_names:
            test_path = train_path.with_name(test_name)
            if test_path.exists():
                return train_path, test_path

    json_candidates = sorted(
        path for path in split_dir.iterdir()
        if path.is_file() and path.suffix == ".json" and pattern.search(path.name)
    )
    for json_path in json_candidates:
        payload = json.loads(json_path.read_text(encoding="utf-8"))
        train_ids = payload.get("train_ids")
        test_ids = payload.get("test_ids")
        if isinstance(train_ids, list) and isinstance(test_ids, list):
            generated_train = split_dir / f"{json_path.stem}_train_ids.txt"
            generated_test = split_dir / f"{json_path.stem}_test_ids.txt"
            if generated_train.exists() and generated_test.exists():
                return generated_train, generated_test
    return None


def validate_ids(
    train_ids: list[int],
    test_ids: list[int],
    available: set[int],
    *,
    context: str,
) -> None:
    if len(train_ids) != TRAIN_COUNT:
        raise RuntimeError(f"{context}: train split has {len(train_ids)} IDs; expected {TRAIN_COUNT}.")
    if len(test_ids) != TEST_COUNT:
        raise RuntimeError(f"{context}: test split has {len(test_ids)} IDs; expected {TEST_COUNT}.")
    if len(set(train_ids)) != len(train_ids):
        raise RuntimeError(f"{context}: train split contains duplicates.")
    if len(set(test_ids)) != len(test_ids):
        raise RuntimeError(f"{context}: test split contains duplicates.")
    if set(train_ids).intersection(test_ids):
        raise RuntimeError(f"{context}: train and test splits overlap.")
    missing = sorted((set(train_ids) | set(test_ids)) - available)
    if missing:
        raise RuntimeError(f"{context}: split contains IDs not present in Sub20: {missing[:10]}.")


def validate_split_dir(split_dir: Path, available_ids: list[int]) -> dict[int, tuple[Path, Path]] | None:
    if not split_dir.is_dir():
        return None
    available = set(available_ids)
    pairs: dict[int, tuple[Path, Path]] = {}
    for case_index in range(NUM_CASES):
        pair = find_split_pair(split_dir, case_index, SEED_BASE + case_index)
        if pair is None:
            pair = find_split_pair(split_dir, case_index, None)
        if pair is None:
            return None
        train_path, test_path = pair
        validate_ids(
            read_ids(train_path),
            read_ids(test_path),
            available,
            context=f"{split_dir} case{case_index:02d}",
        )
        pairs[case_index] = pair
    return pairs


def generate_split_ids(available_ids: list[int], seed: int) -> tuple[list[int], list[int]]:
    shuffled = list(sorted(available_ids))
    random.Random(seed).shuffle(shuffled)
    train_ids = sorted(shuffled[:TRAIN_COUNT])
    test_ids = sorted(shuffled[TRAIN_COUNT:TRAIN_COUNT + TEST_COUNT])
    return train_ids, test_ids


def generate_new_splits(split_dir: Path, available_ids: list[int]) -> dict[int, tuple[Path, Path]]:
    available = set(available_ids)
    pairs: dict[int, tuple[Path, Path]] = {}
    split_dir.mkdir(parents=True, exist_ok=True)
    for case_index in range(NUM_CASES):
        seed = SEED_BASE + case_index
        train_ids, test_ids = generate_split_ids(available_ids, seed)
        validate_ids(train_ids, test_ids, available, context=f"generated case{case_index:02d}")
        train_path, test_path = cxx_split_paths(split_dir, case_index, seed)
        write_ids(train_path, train_ids)
        write_ids(test_path, test_ids)
        pairs[case_index] = (train_path, test_path)
    return pairs


def write_config(
    results_dir: Path,
    *,
    active_split_dir: Path,
    source: str,
    pairs: dict[int, tuple[Path, Path]],
) -> None:
    config_path = results_dir / "splits" / "shared_split_config.json"
    config_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "landscape": LANDSCAPE,
        "train_count": TRAIN_COUNT,
        "test_count": TEST_COUNT,
        "num_cases": NUM_CASES,
        "seed_base": SEED_BASE,
        "source": source,
        "active_split_dir": str(active_split_dir),
        "cases": {
            f"case{case_index:02d}": {
                "seed": SEED_BASE + case_index,
                "train_path": str(train_path),
                "test_path": str(test_path),
            }
            for case_index, (train_path, test_path) in pairs.items()
        },
    }
    config_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    marker_path = results_dir / "splits" / "ACTIVE_SPLIT_DIR.txt"
    marker_path.write_text(str(active_split_dir) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    available_ids = scenario_ids(args.results_path)

    if not args.force_new:
        previous_pairs = validate_split_dir(args.previous_split_dir, available_ids)
        if previous_pairs is not None:
            write_config(
                args.results_dir,
                active_split_dir=args.previous_split_dir,
                source="reused_previous",
                pairs=previous_pairs,
            )
            print(f"Reusing validated previous split directory: {args.previous_split_dir}")
            return 0

    new_split_dir = args.results_dir / "splits"
    existing_pairs = validate_split_dir(new_split_dir, available_ids)
    if existing_pairs is not None:
        write_config(
            args.results_dir,
            active_split_dir=new_split_dir,
            source="existing_new_directory",
            pairs=existing_pairs,
        )
        print(f"Using existing split directory: {new_split_dir}")
        return 0

    pairs = generate_new_splits(new_split_dir, available_ids)
    write_config(
        args.results_dir,
        active_split_dir=new_split_dir,
        source="generated_new",
        pairs=pairs,
    )
    print(f"Generated shared splits under: {new_split_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
