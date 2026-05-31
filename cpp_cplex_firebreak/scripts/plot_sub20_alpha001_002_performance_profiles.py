#!/usr/bin/env python3
"""Create runtime performance profiles for selected Sub20 FPP methods."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt


DEFAULT_CSV = Path(
    "results/batch/sub20_alpha001_002_fpp_exact_tl300/combined/batch_results_all.csv")
DEFAULT_OUTPUT_DIR = Path(
    "results/batch/sub20_alpha001_002_fpp_exact_tl300/combined/plots")

OBJECTIVES = [
    ("Expected", "", "expected"),
    ("CVaR", "-CVaR", "cvar"),
    ("MeanCVaR", "-MeanCVaR", "meancvar"),
]

METHOD_SPECS = [
    {
        "label": "FPP-SAA base",
        "method_family": "FPP-SAA",
        "fpp_mode": "fpp_base",
        "lb_config": "None",
        "use_root_cuts": "false",
        "is_combinatorial": "false",
    },
    {
        "label": "FPP-SAA cut",
        "method_family": "FPP-SAA",
        "fpp_mode": "fpp_cut",
        "lb_config": "None",
        "use_root_cuts": "false",
        "is_combinatorial": "false",
    },
    {
        "label": "FPP-Branch-and-Benders",
        "method_family": "FPP-Branch-Benders",
        "fpp_mode": "",
        "lb_config": "None",
        "use_root_cuts": "false",
        "is_combinatorial": "false",
    },
    {
        "label": "FPP-Branch-and-Benders-LLBI",
        "method_family": "FPP-Branch-Benders",
        "fpp_mode": "",
        "lb_config": "LLBI",
        "use_root_cuts": "false",
        "is_combinatorial": "false",
    },
    {
        "label": "FPP-Branch-and-Benders-LLBI-RootCuts",
        "method_family": "FPP-Branch-Benders",
        "fpp_mode": "",
        "lb_config": "LLBI",
        "use_root_cuts": "true",
        "is_combinatorial": "false",
    },
    {
        "label": "FPP-Branch-Benders-Combinatorial",
        "method_family": "FPP-Branch-Benders-Combinatorial",
        "fpp_mode": "",
        "lb_config": "Combinatorial",
        "use_root_cuts": "false",
        "is_combinatorial": "true",
    },
]

COLORS = [
    "#1f77b4",
    "#ff7f0e",
    "#2ca02c",
    "#d62728",
    "#9467bd",
    "#8c564b",
]

LINESTYLES = ["-", "-", "-", "--", "-.", "-"]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument(
        "--success-mode",
        choices=["completed", "optimal"],
        default="completed",
        help=(
            "completed uses every row with return_code 0 and finite runtime; "
            "optimal counts only rows whose solver_status is Optimal."))
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as inp:
        return list(csv.DictReader(inp))


def is_true(value: str) -> str:
    return "true" if value.strip().lower() in {"true", "1", "yes", "on"} else "false"


def runtime(row: dict[str, str]) -> float | None:
    try:
        value = float(row.get("runtime_seconds", ""))
    except ValueError:
        return None
    if not math.isfinite(value) or value <= 0.0:
        return None
    return value


def status(row: dict[str, str]) -> str:
    return row.get("solver_status") or row.get("status") or ""


def successful(row: dict[str, str], success_mode: str) -> bool:
    if row.get("worker_return_code", "0") not in {"", "0"}:
        return False
    if runtime(row) is None:
        return False
    if success_mode == "optimal":
        return status(row).lower() == "optimal"
    return bool(status(row))


def method_family_matches(row: dict[str, str], spec_family: str) -> bool:
    actual = row.get("method_family", "")
    if actual == spec_family:
        return True
    aliases = {
        "FPP-Branch-Benders": {"FPP-Branch-and-Benders"},
        "FPP-Branch-Benders-Combinatorial": {"FPP-Branch-and-Benders-Combinatorial"},
    }
    return actual in aliases.get(spec_family, set())


def row_matches_spec(row: dict[str, str], spec: dict[str, str]) -> bool:
    if not method_family_matches(row, spec["method_family"]):
        return False
    if row.get("fpp_mode", "") != spec["fpp_mode"]:
        return False
    if row.get("lb_config", "") != spec["lb_config"]:
        return False
    if is_true(row.get("use_root_cuts", "")) != spec["use_root_cuts"]:
        return False
    if is_true(row.get("is_combinatorial", "")) != spec["is_combinatorial"]:
        return False
    return True


def instance_key(row: dict[str, str]) -> tuple[str, str, str, str]:
    return (
        row.get("landscape", ""),
        row.get("case_id", ""),
        f"{float(row.get('alpha', 'nan')):.2f}",
        row.get("seed", ""),
    )


def selected_rows_for_objective(
    rows: list[dict[str, str]],
    objective_family: str,
) -> dict[str, dict[tuple[str, str, str, str], dict[str, str]]]:
    selected: dict[str, dict[tuple[str, str, str, str], dict[str, str]]] = {}
    objective_rows = [row for row in rows if row.get("objective_family") == objective_family]
    for spec in METHOD_SPECS:
        by_instance: dict[tuple[str, str, str, str], dict[str, str]] = {}
        for row in objective_rows:
            if not row_matches_spec(row, spec):
                continue
            key = instance_key(row)
            if key in by_instance:
                raise RuntimeError(
                    f"Duplicate row for {objective_family}, {spec['label']}, {key}.")
            by_instance[key] = row
        selected[spec["label"]] = by_instance
    return selected


def compute_ratios(
    rows_by_method: dict[str, dict[tuple[str, str, str, str], dict[str, str]]],
    success_mode: str,
) -> tuple[
    dict[str, list[float]],
    list[dict[str, str]],
    list[tuple[str, str, str, str]],
]:
    all_instances = sorted({
        key
        for by_instance in rows_by_method.values()
        for key in by_instance
    })
    if not all_instances:
        raise RuntimeError("No matching rows found for performance profile.")

    ratios: dict[str, list[float]] = {label: [] for label in rows_by_method}
    diagnostic_rows: list[dict[str, str]] = []

    for key in all_instances:
        runtimes: dict[str, float] = {}
        for label, by_instance in rows_by_method.items():
            row = by_instance.get(key)
            if row is not None and successful(row, success_mode):
                value = runtime(row)
                if value is not None:
                    runtimes[label] = value
        best = min(runtimes.values()) if runtimes else math.inf
        for label, by_instance in rows_by_method.items():
            row = by_instance.get(key)
            value = runtime(row) if row is not None else None
            ok = row is not None and successful(row, success_mode) and math.isfinite(best)
            ratio = value / best if ok and value is not None else math.inf
            ratios[label].append(ratio)
            diagnostic_rows.append({
                "landscape": key[0],
                "case_id": key[1],
                "alpha": key[2],
                "seed": key[3],
                "method_label": label,
                "runtime_seconds": "" if value is None else f"{value:.10g}",
                "best_runtime_seconds": "" if not math.isfinite(best) else f"{best:.10g}",
                "runtime_ratio": "inf" if not math.isfinite(ratio) else f"{ratio:.10g}",
                "status": "" if row is None else status(row),
                "success": "true" if ok else "false",
            })
    return ratios, diagnostic_rows, all_instances


def profile_points(values: list[float], x_max: float) -> tuple[list[float], list[float]]:
    n = len(values)
    finite_values = sorted(value for value in values if math.isfinite(value))
    unique = sorted({round(value, 12) for value in finite_values if value >= 1.0})
    x_values = [1.0]
    y_values = [sum(value <= 1.0 + 1.0e-9 for value in finite_values) / n]
    for value in unique:
        if value <= 1.0 + 1.0e-9:
            continue
        x_values.append(value)
        y_values.append(sum(v <= value + 1.0e-9 for v in finite_values) / n)
    if x_values[-1] < x_max:
        x_values.append(x_max)
        y_values.append(y_values[-1])
    return x_values, y_values


def plot_objective(
    objective_family: str,
    slug: str,
    ratios: dict[str, list[float]],
    instance_count: int,
    output_dir: Path,
    success_mode: str,
) -> None:
    finite = [
        value
        for values in ratios.values()
        for value in values
        if math.isfinite(value)
    ]
    if not finite:
        raise RuntimeError(f"No finite ratios for {objective_family}.")
    x_max = max(2.0, max(finite) * 1.08)

    fig, ax = plt.subplots(figsize=(9.5, 6.0))
    for index, spec in enumerate(METHOD_SPECS):
        label = spec["label"]
        values = ratios[label]
        solved = sum(math.isfinite(value) for value in values)
        x_values, y_values = profile_points(values, x_max)
        ax.step(
            x_values,
            y_values,
            where="post",
            label=f"{label} ({solved}/{instance_count})",
            color=COLORS[index % len(COLORS)],
            linestyle=LINESTYLES[index % len(LINESTYLES)],
            linewidth=2.0,
        )

    ax.set_xscale("log", base=2)
    ax.set_xlim(1.0, x_max)
    ax.set_ylim(0.0, 1.02)
    ax.set_xlabel("Runtime ratio tau = method runtime / best runtime")
    ax.set_ylabel("Fraction of instance blocks solved within tau")
    ax.set_title(f"Sub20 Performance Profile - {objective_family} Objective")
    ax.grid(True, which="both", linestyle=":", linewidth=0.7, alpha=0.75)
    ax.legend(loc="lower right", fontsize=8.5, frameon=True)
    note = (
        f"Metric: runtime_seconds. Success mode: {success_mode}. "
        f"Instance blocks: {instance_count}."
    )
    fig.text(0.01, 0.01, note, fontsize=8.5)
    fig.tight_layout(rect=(0, 0.035, 1, 1))

    output_dir.mkdir(parents=True, exist_ok=True)
    png_path = output_dir / f"performance_profile_{slug}_runtime.png"
    pdf_path = output_dir / f"performance_profile_{slug}_runtime.pdf"
    fig.savefig(png_path, dpi=220)
    fig.savefig(pdf_path)
    plt.close(fig)


def write_diagnostics(path: Path, rows: list[dict[str, str]]) -> None:
    fields = [
        "objective_family",
        "landscape",
        "case_id",
        "alpha",
        "seed",
        "method_label",
        "runtime_seconds",
        "best_runtime_seconds",
        "runtime_ratio",
        "status",
        "success",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    rows = read_rows(args.csv)
    diagnostics: list[dict[str, str]] = []

    for objective_family, _suffix, slug in OBJECTIVES:
        selected = selected_rows_for_objective(rows, objective_family)
        ratios, objective_diagnostics, instances = compute_ratios(selected, args.success_mode)
        for row in objective_diagnostics:
            row["objective_family"] = objective_family
        diagnostics.extend(objective_diagnostics)
        plot_objective(
            objective_family,
            slug,
            ratios,
            len(instances),
            args.output_dir,
            args.success_mode,
        )

    diagnostic_path = args.output_dir / "performance_profile_runtime_ratios.csv"
    write_diagnostics(diagnostic_path, diagnostics)
    print(f"Wrote performance profiles to {args.output_dir}")
    print(f"Wrote ratio diagnostics to {diagnostic_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
