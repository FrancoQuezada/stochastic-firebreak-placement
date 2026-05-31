#!/usr/bin/env python3
"""Shared configuration for the FPP risk-objective computational study."""

from __future__ import annotations

import json
import random
import re
import shlex
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PROJECT_DIR = Path(__file__).resolve().parents[2]
REPO_ROOT = PROJECT_DIR.parent

DEFAULT_LANDSCAPE = "Sub20"
DEFAULT_FULL_LANDSCAPES = ["Sub20", "Sub40"]
DEFAULT_OUTPUT_DIR = PROJECT_DIR / "results" / "computational_study_fpp_risk"
DEFAULT_FULL_OUTPUT_DIR = PROJECT_DIR / "results" / "computational_study_fpp_risk_full"
DEFAULT_ALPHAS = [0.01, 0.02, 0.03, 0.04, 0.05]
DEFAULT_LAMBDAS = [0.0, 0.5, 1.0]
DEFAULT_CASE_IDS = [0, 1, 2, 3, 4]
DEFAULT_TRAIN_COUNT = 100
DEFAULT_FULL_TRAIN_COUNTS = [100, 200, 300]
DEFAULT_TEST_COUNT = 100
DEFAULT_FULL_TEST_COUNT = 300
DEFAULT_SEED_BASE = 51000
DEFAULT_FULL_SEED_BASE = 61000
DEFAULT_TIME_LIMIT_SECONDS = 1800.0
DEFAULT_THREADS = 1
DEFAULT_MIP_GAP = 0.001
DEFAULT_CVAR_BETA = 0.9


@dataclass(frozen=True)
class RiskLevel:
    lambda_value: float
    lambda_token: str
    risk_measure: str
    cvar_beta: float | None
    cvar_lambda: float | None

    def cli_flags(self) -> list[str]:
        if self.risk_measure == "expected":
            return ["--risk-measure", "expected"]
        flags = [
            "--risk-measure",
            self.risk_measure,
            "--cvar-beta",
            format_float(self.cvar_beta if self.cvar_beta is not None else DEFAULT_CVAR_BETA),
        ]
        if self.risk_measure == "mean-cvar":
            flags.extend([
                "--cvar-lambda",
                format_float(self.cvar_lambda if self.cvar_lambda is not None else 0.5),
            ])
        return flags


@dataclass(frozen=True)
class HeuristicConfig:
    key: str
    initial_candidate_size: int
    activation_batch_size: int
    deactivation_batch_size: int
    min_active_size: int
    max_active_size: int
    deactivation_min_age: int
    reactivation_cooldown_rounds: int
    max_candidate_rounds: int
    tail_score_gamma: float = 0.5
    tail_protection_size: int = 20


@dataclass(frozen=True)
class MethodVariant:
    key: str
    family: str
    variant: str
    command: str
    label_prefix: str
    exact_baseline: bool = False
    restricted: bool = False
    heuristic: bool = False
    maintenance: bool = False
    score_mode: str = "generic"
    heuristic_config_key: str = "compact"
    supported_lambdas: tuple[float, ...] = (0.0, 0.5, 1.0)

    def label(self, risk: RiskLevel) -> str:
        return f"{self.label_prefix}-{risk.lambda_token}"


HEURISTIC_CONFIGS: dict[str, HeuristicConfig] = {
    "compact": HeuristicConfig(
        key="compact",
        initial_candidate_size=50,
        activation_batch_size=20,
        deactivation_batch_size=20,
        min_active_size=50,
        max_active_size=70,
        deactivation_min_age=1,
        reactivation_cooldown_rounds=1,
        max_candidate_rounds=2,
        tail_score_gamma=0.5,
        tail_protection_size=20,
    ),
    "wide": HeuristicConfig(
        key="wide",
        initial_candidate_size=100,
        activation_batch_size=20,
        deactivation_batch_size=20,
        min_active_size=100,
        max_active_size=120,
        deactivation_min_age=1,
        reactivation_cooldown_rounds=1,
        max_candidate_rounds=2,
        tail_score_gamma=0.5,
        tail_protection_size=20,
    ),
}


METHOD_VARIANTS: tuple[MethodVariant, ...] = (
    MethodVariant(
        key="fpp-saa",
        family="direct-mip",
        variant="direct-cplex-mip",
        command="run-fpp-saa-oos",
        label_prefix="FPP-SAA",
        exact_baseline=True,
    ),
    MethodVariant(
        key="fpp-bb",
        family="branch-benders",
        variant="exact-callback",
        command="run-fpp-branch-benders-oos",
        label_prefix="FPP-Branch-Benders",
        exact_baseline=True,
    ),
    MethodVariant(
        key="restricted-generic-no-maintenance",
        family="restricted-heuristic",
        variant="generic-no-maintenance",
        command="run-fpp-restricted-branch-benders-oos",
        label_prefix="FPP-Restricted-Heuristic-Generic-NoMaintenance",
        restricted=True,
        heuristic=True,
        maintenance=False,
        score_mode="generic",
    ),
    MethodVariant(
        key="restricted-generic-maintenance",
        family="restricted-heuristic",
        variant="generic-maintenance",
        command="run-fpp-restricted-branch-benders-oos",
        label_prefix="FPP-Restricted-Heuristic-Generic-Maintenance",
        restricted=True,
        heuristic=True,
        maintenance=True,
        score_mode="generic",
    ),
    MethodVariant(
        key="restricted-tailblend-maintenance",
        family="restricted-heuristic",
        variant="tailblend-maintenance",
        command="run-fpp-restricted-branch-benders-oos",
        label_prefix="FPP-Restricted-Heuristic-TailBlend-Maintenance",
        restricted=True,
        heuristic=True,
        maintenance=True,
        score_mode="cvar-tail-blend",
        # Phase 1S supports tail-blend only for pure CVaR, not mean-CVaR.
        supported_lambdas=(1.0,),
    ),
)


METHOD_FILTER_ALIASES: dict[str, tuple[str, ...]] = {
    "fpp-saa": ("fpp-saa",),
    "fpp-bb": ("fpp-bb",),
    "branch-benders": ("fpp-bb",),
    "restricted-generic": (
        "restricted-generic-no-maintenance",
        "restricted-generic-maintenance",
    ),
    "restricted-generic-no-maintenance": ("restricted-generic-no-maintenance",),
    "restricted-generic-maintenance": ("restricted-generic-maintenance",),
    "restricted-tailblend": ("restricted-tailblend-maintenance",),
    "restricted-tailblend-maintenance": ("restricted-tailblend-maintenance",),
    "restricted": (
        "restricted-generic-no-maintenance",
        "restricted-generic-maintenance",
        "restricted-tailblend-maintenance",
    ),
    "exact": ("fpp-saa", "fpp-bb"),
    "heuristics": (
        "restricted-generic-no-maintenance",
        "restricted-generic-maintenance",
        "restricted-tailblend-maintenance",
    ),
    "all": tuple(variant.key for variant in METHOD_VARIANTS),
}


def format_float(value: float) -> str:
    return f"{value:.12g}"


def alpha_token(alpha: float) -> str:
    return format_float(alpha).replace(".", "p").replace("-", "m")


def lambda_token(lambda_value: float) -> str:
    if abs(lambda_value) <= 1.0e-12:
        return "lambda0"
    if abs(lambda_value - 0.5) <= 1.0e-12:
        return "lambda05"
    if abs(lambda_value - 1.0) <= 1.0e-12:
        return "lambda1"
    return "lambda" + format_float(lambda_value).replace(".", "p").replace("-", "m")


def parse_float_list(value: str) -> list[float]:
    if not value:
        return []
    return [float(piece.strip()) for piece in value.split(",") if piece.strip()]


def parse_int_list(value: str) -> list[int]:
    if not value:
        return []
    return [int(piece.strip()) for piece in value.split(",") if piece.strip()]


def parse_string_list(value: str) -> list[str]:
    if not value:
        return []
    return [piece.strip() for piece in value.split(",") if piece.strip()]


def risk_level_for_lambda(lambda_value: float, cvar_beta: float = DEFAULT_CVAR_BETA) -> RiskLevel:
    if abs(lambda_value) <= 1.0e-12:
        return RiskLevel(
            lambda_value=0.0,
            lambda_token="lambda0",
            risk_measure="expected",
            cvar_beta=None,
            cvar_lambda=0.0,
        )
    if abs(lambda_value - 0.5) <= 1.0e-12:
        return RiskLevel(
            lambda_value=0.5,
            lambda_token="lambda05",
            risk_measure="mean-cvar",
            cvar_beta=cvar_beta,
            cvar_lambda=0.5,
        )
    if abs(lambda_value - 1.0) <= 1.0e-12:
        return RiskLevel(
            lambda_value=1.0,
            lambda_token="lambda1",
            risk_measure="cvar",
            cvar_beta=cvar_beta,
            cvar_lambda=1.0,
        )
    raise ValueError("Supported study lambdas are 0.0, 0.5, and 1.0.")


def output_layout(output_dir: Path = DEFAULT_OUTPUT_DIR) -> dict[str, Path]:
    return {
        "root": output_dir,
        "commands": output_dir / "commands",
        "json": output_dir / "json",
        "logs": output_dir / "logs",
        "solutions": output_dir / "solutions",
        "splits": output_dir / "splits",
        "summaries": output_dir / "summaries",
        "tables": output_dir / "tables",
        "profiles": output_dir / "profiles",
        "monitoring": output_dir / "monitoring",
        "checkpoints": output_dir / "checkpoints",
        "runner_logs": output_dir / "logs" / "runner",
        "raw_csv": output_dir / "tables" / "raw_runner_csv",
    }


def ensure_output_layout(output_dir: Path = DEFAULT_OUTPUT_DIR) -> dict[str, Path]:
    layout = output_layout(output_dir)
    for path in layout.values():
        path.mkdir(parents=True, exist_ok=True)
    return layout


def default_forest_path(landscape: str = DEFAULT_LANDSCAPE) -> Path:
    return REPO_ROOT / "sample_test" / "data" / "CanadianFBP" / landscape


def default_results_path(landscape: str = DEFAULT_LANDSCAPE) -> Path:
    return REPO_ROOT / "sample_test" / landscape


def available_scenario_ids(results_path: Path) -> list[int]:
    messages_dir = results_path / "Messages"
    pattern = re.compile(r"^MessagesFile([0-9]+)\.csv$")
    ids: list[int] = []
    for path in messages_dir.glob("MessagesFile*.csv"):
        match = pattern.match(path.name)
        if match:
            ids.append(int(match.group(1)))
    ids = sorted(ids)
    if not ids:
        raise RuntimeError(f"No scenario message files found under {messages_dir}")
    return ids


def seed_for_case(seed_base: int, case_id: int) -> int:
    return seed_base + case_id


def split_key(landscape: str, train_count: int, test_count: int, case_id: int, seed: int) -> str:
    return f"{landscape}_train{train_count}_test{test_count}_case{case_id}_seed{seed}"


def generate_split_ids(
    scenario_ids: Iterable[int],
    seed: int,
    train_count: int,
    test_count: int) -> tuple[list[int], list[int]]:
    ids = sorted(scenario_ids)
    if train_count + test_count > len(ids):
        raise RuntimeError(
            f"Requested train_count + test_count = {train_count + test_count}, "
            f"but only {len(ids)} scenarios are available.")
    shuffled = list(ids)
    random.Random(seed).shuffle(shuffled)
    train_ids = sorted(shuffled[:train_count])
    test_ids = sorted(shuffled[train_count: train_count + test_count])
    if set(train_ids).intersection(test_ids):
        raise RuntimeError("Generated train/test split is not disjoint.")
    return train_ids, test_ids


def write_split_files(
    split_dir: Path,
    *,
    landscape: str,
    train_count: int,
    test_count: int,
    case_id: int,
    seed: int,
    train_ids: list[int],
    test_ids: list[int]) -> Path:
    split_dir.mkdir(parents=True, exist_ok=True)
    key = split_key(landscape, train_count, test_count, case_id, seed)
    json_path = split_dir / f"{key}.json"
    payload = {
        "split_key": key,
        "landscape": landscape,
        "train_count": train_count,
        "test_count": test_count,
        "case_id": case_id,
        "seed": seed,
        "train_ids": train_ids,
        "test_ids": test_ids,
    }
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (split_dir / f"{key}_train_ids.txt").write_text(
        "\n".join(str(value) for value in train_ids) + "\n",
        encoding="utf-8")
    (split_dir / f"{key}_test_ids.txt").write_text(
        "\n".join(str(value) for value in test_ids) + "\n",
        encoding="utf-8")
    return json_path


def read_split_file(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def expand_method_filters(filters: str | None) -> set[str]:
    if not filters:
        return set(METHOD_FILTER_ALIASES["all"])
    expanded: set[str] = set()
    for raw in filters.split(","):
        key = raw.strip()
        if not key:
            continue
        if key not in METHOD_FILTER_ALIASES:
            known = ", ".join(sorted(METHOD_FILTER_ALIASES))
            raise ValueError(f"Unknown method filter '{key}'. Known filters: {known}")
        expanded.update(METHOD_FILTER_ALIASES[key])
    return expanded


def selected_variants(filters: str | None) -> list[MethodVariant]:
    selected = expand_method_filters(filters)
    return [variant for variant in METHOD_VARIANTS if variant.key in selected]


def variant_supported_for_lambda(variant: MethodVariant, lambda_value: float) -> bool:
    return any(abs(lambda_value - supported) <= 1.0e-12 for supported in variant.supported_lambdas)


def expected_command_count(
    alphas: Iterable[float],
    lambdas: Iterable[float],
    case_ids: Iterable[int],
    method_filters: str | None = None,
    landscapes: Iterable[str] | None = None,
    train_counts: Iterable[int] | None = None) -> int:
    variants = selected_variants(method_filters)
    landscape_values = list(landscapes) if landscapes is not None else [DEFAULT_LANDSCAPE]
    train_count_values = list(train_counts) if train_counts is not None else [DEFAULT_TRAIN_COUNT]
    count = 0
    for _landscape in landscape_values:
        for _train_count in train_count_values:
            for _alpha in alphas:
                for lambda_value in lambdas:
                    for _case in case_ids:
                        count += sum(1 for variant in variants if variant_supported_for_lambda(variant, lambda_value))
    return count


def run_id_for(
    landscape: str,
    alpha: float,
    risk: RiskLevel,
    case_id: int,
    variant: MethodVariant,
    heuristic_config: HeuristicConfig,
    train_count: int | None = None) -> str:
    config_suffix = ""
    if variant.restricted:
        config_suffix = f"_{heuristic_config.key}"
    train_suffix = f"_t{train_count}" if train_count is not None else ""
    return (
        f"fpp_risk_{landscape.lower()}{train_suffix}_a{alpha_token(alpha)}_"
        f"{risk.lambda_token}_case{case_id}_{variant.key.replace('-', '_')}{config_suffix}"
    )


def shell_join(tokens: Iterable[str | Path | float | int]) -> str:
    return " ".join(shlex.quote(str(token)) for token in tokens)


def csv_join_ints(values: Iterable[int]) -> str:
    return ";".join(str(value) for value in values)


def selected_firebreaks_key(value: object) -> str:
    if isinstance(value, list):
        return csv_join_ints(int(v) for v in value)
    if value is None:
        return ""
    return str(value)
