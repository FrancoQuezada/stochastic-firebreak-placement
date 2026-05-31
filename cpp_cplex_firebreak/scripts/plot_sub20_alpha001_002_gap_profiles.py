#!/usr/bin/env python3
"""Create optimality-gap profiles for selected Sub20 FPP methods."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.ticker import PercentFormatter


DEFAULT_CSV = Path(
    "results/batch/sub20_alpha001_002_fpp_exact_tl300/combined/batch_results_all.csv")
DEFAULT_OUTPUT_DIR = Path(
    "results/batch/sub20_alpha001_002_fpp_exact_tl300/combined/plots")
TARGET_GAP = 0.001

OBJECTIVES = [
    ("Expected", "expected"),
    ("CVaR", "cvar"),
    ("MeanCVaR", "meancvar"),
]

METHOD_SPECS = [
    ("FPP-SAA base", "FPP-SAA", "fpp_base", "None", "false", "false"),
    ("FPP-SAA cut", "FPP-SAA", "fpp_cut", "None", "false", "false"),
    ("FPP-Branch-and-Benders", "FPP-Branch-Benders", "", "None", "false", "false"),
    ("FPP-Branch-and-Benders-LLBI", "FPP-Branch-Benders", "", "LLBI", "false", "false"),
    ("FPP-Branch-and-Benders-LLBI-RootCuts", "FPP-Branch-Benders", "", "LLBI", "true", "false"),
    ("FPP-Branch-Benders-Combinatorial", "FPP-Branch-Benders-Combinatorial", "", "Combinatorial", "false", "true"),
]

COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b"]
LINESTYLES = ["-", "-", "-", "--", "-.", "-"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def bool_text(value: str) -> str:
    return "true" if value.strip().lower() in {"true", "1", "yes", "on"} else "false"


def method_family_matches(actual: str, expected: str) -> bool:
    if actual == expected:
        return True
    aliases = {
        "FPP-Branch-Benders": {"FPP-Branch-and-Benders"},
        "FPP-Branch-Benders-Combinatorial": {"FPP-Branch-and-Benders-Combinatorial"},
    }
    return actual in aliases.get(expected, set())


def row_matches(row: dict[str, str], spec: tuple[str, str, str, str, str, str]) -> bool:
    _label, family, fpp_mode, lb_config, root_cuts, combinatorial = spec
    return (
        method_family_matches(row.get("method_family", ""), family)
        and row.get("fpp_mode", "") == fpp_mode
        and row.get("lb_config", "") == lb_config
        and bool_text(row.get("use_root_cuts", "")) == root_cuts
        and bool_text(row.get("is_combinatorial", "")) == combinatorial
    )


def instance_key(row: dict[str, str]) -> tuple[str, str, str, str]:
    return (
        row.get("landscape", ""),
        row.get("case_id", ""),
        f"{float(row.get('alpha', 'nan')):.2f}",
        row.get("seed", ""),
    )


def computed_gap(row: dict[str, str]) -> float:
    try:
        objective = float(row.get("objective_in_sample", ""))
        best_bound = float(row.get("best_bound", ""))
    except ValueError:
        return math.inf
    if not math.isfinite(objective) or not math.isfinite(best_bound):
        return math.inf
    return abs(objective - best_bound) / max(1.0, abs(objective))


def status(row: dict[str, str]) -> str:
    return row.get("solver_status") or row.get("status") or ""


def selected_rows(
    rows: list[dict[str, str]],
    objective_family: str,
) -> dict[str, dict[tuple[str, str, str, str], dict[str, str]]]:
    objective_rows = [row for row in rows if row.get("objective_family") == objective_family]
    selected: dict[str, dict[tuple[str, str, str, str], dict[str, str]]] = {}
    for spec in METHOD_SPECS:
        label = spec[0]
        by_instance: dict[tuple[str, str, str, str], dict[str, str]] = {}
        for row in objective_rows:
            if not row_matches(row, spec):
                continue
            key = instance_key(row)
            if key in by_instance:
                raise RuntimeError(f"Duplicate row for {objective_family}, {label}, {key}")
            by_instance[key] = row
        selected[label] = by_instance
    return selected


def gap_profile_values(gaps: list[float], x_max: float) -> tuple[list[float], list[float]]:
    n = len(gaps)
    finite = sorted(gap for gap in gaps if math.isfinite(gap))
    unique = sorted(set([0.0, TARGET_GAP, *finite, x_max]))
    x_values: list[float] = []
    y_values: list[float] = []
    for x in unique:
        if x < 0.0:
            continue
        x_values.append(x)
        y_values.append(sum(gap <= x + 1.0e-12 for gap in finite) / n)
    return x_values, y_values


def build_profiles(
    rows_by_method: dict[str, dict[tuple[str, str, str, str], dict[str, str]]],
) -> tuple[dict[str, list[float]], list[tuple[str, str, str, str]], list[dict[str, str]]]:
    instances = sorted({
        key
        for by_instance in rows_by_method.values()
        for key in by_instance
    })
    if not instances:
        raise RuntimeError("No matching instances were found.")

    gaps_by_method: dict[str, list[float]] = {label: [] for label in rows_by_method}
    diagnostics: list[dict[str, str]] = []
    for key in instances:
        for label, by_instance in rows_by_method.items():
            row = by_instance.get(key)
            gap = computed_gap(row) if row is not None else math.inf
            gaps_by_method[label].append(gap)
            diagnostics.append({
                "landscape": key[0],
                "case_id": key[1],
                "alpha": key[2],
                "seed": key[3],
                "method_label": label,
                "objective_in_sample": "" if row is None else row.get("objective_in_sample", ""),
                "best_bound": "" if row is None else row.get("best_bound", ""),
                "configured_mip_gap_column": "" if row is None else row.get("mip_gap", ""),
                "computed_relative_gap": "inf" if not math.isfinite(gap) else f"{gap:.12g}",
                "computed_relative_gap_percent": "inf" if not math.isfinite(gap) else f"{100.0 * gap:.12g}",
                "status": "" if row is None else status(row),
            })
    return gaps_by_method, instances, diagnostics


def plot_objective(
    objective_family: str,
    slug: str,
    gaps_by_method: dict[str, list[float]],
    instance_count: int,
    output_dir: Path,
) -> None:
    finite = [
        gap
        for gaps in gaps_by_method.values()
        for gap in gaps
        if math.isfinite(gap)
    ]
    if not finite:
        raise RuntimeError(f"No finite gaps for {objective_family}.")
    x_max = max(TARGET_GAP, max(finite)) * 1.08
    if x_max <= TARGET_GAP * 1.5:
        x_max = TARGET_GAP * 1.5

    fig, ax = plt.subplots(figsize=(9.5, 6.0))
    for index, spec in enumerate(METHOD_SPECS):
        label = spec[0]
        gaps = gaps_by_method[label]
        finite_count = sum(math.isfinite(gap) for gap in gaps)
        x_values, y_values = gap_profile_values(gaps, x_max)
        ax.step(
            [100.0 * x for x in x_values],
            y_values,
            where="post",
            label=f"{label} ({finite_count}/{instance_count})",
            color=COLORS[index % len(COLORS)],
            linestyle=LINESTYLES[index % len(LINESTYLES)],
            linewidth=2.0,
        )

    ax.axvline(
        100.0 * TARGET_GAP,
        color="#333333",
        linestyle=":",
        linewidth=1.2,
        label="Target gap 0.1%",
    )
    ax.set_xlim(0.0, 100.0 * x_max)
    ax.set_ylim(0.0, 1.02)
    ax.xaxis.set_major_formatter(PercentFormatter(xmax=100.0))
    ax.set_xlabel("Optimality gap threshold")
    ax.set_ylabel("Fraction of instances solved")
    ax.set_title(f"Sub20 Optimality-Gap Profile - {objective_family} Objective")
    ax.grid(True, linestyle=":", linewidth=0.7, alpha=0.75)
    ax.legend(loc="lower right", fontsize=8.2, frameon=True)
    fig.text(
        0.01,
        0.01,
        "Gap computed from combined CSV as abs(objective_in_sample - best_bound) / max(1, abs(objective_in_sample)).",
        fontsize=8.0,
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))

    output_dir.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_dir / f"performance_profile_{slug}_optimality_gap.png", dpi=220)
    fig.savefig(output_dir / f"performance_profile_{slug}_optimality_gap.pdf")
    plt.close(fig)


def write_diagnostics(path: Path, rows: list[dict[str, str]]) -> None:
    fields = [
        "objective_family",
        "landscape",
        "case_id",
        "alpha",
        "seed",
        "method_label",
        "objective_in_sample",
        "best_bound",
        "configured_mip_gap_column",
        "computed_relative_gap",
        "computed_relative_gap_percent",
        "status",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    rows = read_rows(args.csv)
    all_diagnostics: list[dict[str, str]] = []
    for objective_family, slug in OBJECTIVES:
        rows_by_method = selected_rows(rows, objective_family)
        gaps_by_method, instances, diagnostics = build_profiles(rows_by_method)
        for row in diagnostics:
            row["objective_family"] = objective_family
        all_diagnostics.extend(diagnostics)
        plot_objective(objective_family, slug, gaps_by_method, len(instances), args.output_dir)

    diagnostics_path = args.output_dir / "performance_profile_optimality_gap_values.csv"
    write_diagnostics(diagnostics_path, all_diagnostics)
    print(f"Wrote optimality-gap profiles to {args.output_dir}")
    print(f"Wrote computed gap diagnostics to {diagnostics_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
