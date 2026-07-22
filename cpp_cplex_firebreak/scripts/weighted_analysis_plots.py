#!/usr/bin/env python3
"""Phase 9B plotting: performance profiles (runtime, exact-FPP methods) and
quality profiles (in-sample/OOS/paired, approximate/DPV/heuristic methods),
rendered only from already-validated Phase 9A/9B analysis outputs.

Uses matplotlib's non-interactive Agg backend. Every plot keeps its
underlying CSV data (written separately by weighted_analysis_profiles.py /
the CLI) -- a plot is never the only artifact for a given profile.
"""

from __future__ import annotations

import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import weighted_analysis_profiles as profiles_mod

_COLORS = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd", "#8c564b", "#e377c2", "#7f7f7f"]


def _slug(stratum: tuple) -> str:
    return "_".join(str(part).replace(" ", "-") for part in stratum)


def _stratum_caption(stratum: tuple, instance_count: int) -> str:
    weight_profile, risk_measure = stratum
    return f"weight profile: {weight_profile} | risk measure: {risk_measure} | instances: {instance_count}"


def _plot_step_curves(ratios_by_method: dict, *, x_label: str, y_label: str, title: str,
                       caption: str, output_dir: Path, filename_stem: str) -> list:
    methods = sorted(ratios_by_method)
    finite_all = [v for values in ratios_by_method.values() for v in values if math.isfinite(v)]
    if not finite_all:
        return []
    x_max = max(2.0, max(finite_all) * 1.08)

    fig, ax = plt.subplots(figsize=(9.0, 6.0))
    for index, method in enumerate(methods):
        values = ratios_by_method[method]
        solved = sum(math.isfinite(v) for v in values)
        x_values, y_values = profiles_mod.profile_points(values, x_max)
        ax.step(
            x_values, y_values, where="post",
            label=f"{method} ({solved}/{len(values)})",
            color=_COLORS[index % len(_COLORS)], linewidth=2.0,
        )
    ax.set_xscale("log", base=2)
    ax.set_xlim(1.0, x_max)
    ax.set_ylim(0.0, 1.02)
    ax.set_xlabel(x_label)
    ax.set_ylabel(y_label)
    ax.set_title(title)
    ax.grid(True, which="both", linestyle=":", linewidth=0.7, alpha=0.7)
    ax.legend(loc="lower right", fontsize=8.5, frameon=True)
    fig.text(0.01, 0.01, caption, fontsize=8.0)
    fig.tight_layout(rect=(0, 0.035, 1, 1))

    output_dir.mkdir(parents=True, exist_ok=True)
    png_path = output_dir / f"{filename_stem}.png"
    pdf_path = output_dir / f"{filename_stem}.pdf"
    fig.savefig(png_path, dpi=200)
    fig.savefig(pdf_path)
    plt.close(fig)
    return [png_path, pdf_path]


def plot_runtime_performance_profiles(runtime_profiles: dict, output_dir) -> list:
    """No claims embedded in the title -- 'Runtime performance profile', not
    'X is fastest'. Deterministic method ordering (alphabetical)."""
    output_dir = Path(output_dir)
    written = []
    for stratum, data in sorted(runtime_profiles.items()):
        group_slug = _slug(stratum)
        written.extend(_plot_step_curves(
            data["ratios_by_method"],
            x_label=r"Runtime ratio $\tau$ = method runtime / best runtime in group",
            y_label=r"Fraction of instance groups solved within $\tau$",
            title="Runtime performance profile",
            caption=_stratum_caption(stratum, data["instance_count"]) + f" | success criterion: {data['success_criterion']}",
            output_dir=output_dir,
            filename_stem=f"performance_profile_runtime_{group_slug}",
        ))
    return written


def plot_quality_profiles(quality_profiles: dict, output_dir, *, namespace: str) -> list:
    output_dir = Path(output_dir)
    written = []
    for stratum, data in sorted(quality_profiles.items()):
        group_slug = _slug(stratum)
        instance_count = len({r.get("run_id") for r in data["rows"]})
        written.extend(_plot_step_curves(
            data["ratios_by_method"],
            x_label=r"Quality ratio $q = 1 + \max(0, \mathrm{gap\_to\_best\_known\_feasible})$",
            y_label="Fraction of rows within quality ratio",
            title=f"Quality profile ({namespace})",
            caption=_stratum_caption(stratum, instance_count),
            output_dir=output_dir,
            filename_stem=f"quality_profile_{namespace}_{group_slug}",
        ))
    return written
