#!/usr/bin/env python3
"""Focused tests for paired projected-LLBI comparisons."""

from __future__ import annotations

import analyze_fpp_new_instances_scaling_experiment as analysis


EXPECTED_PAIRS = {
    "coverage_poly_vs_coverage_exp": (
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts",
    ),
    "path_poly_vs_path_exp": (
        "FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts",
    ),
    "coverage_poly_vs_path_poly": (
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts",
    ),
    "coverage_exp_vs_path_exp": (
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts",
        "FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts",
    ),
}


def projected_row(method: str, objective: str) -> dict[str, str]:
    return analysis.enrich({
        "instance_id": "new20x20",
        "instance_type": "shortest_path",
        "objective_type": "expected",
        "alpha": "0.02",
        "train_count": "200",
        "case_id": "case07",
        "method": method,
        "objective_value": objective,
        "runtime_sec": "10",
        "mip_gap": "0.001",
        "test_expected_burned_area": "120",
        "test_worst10_burned_area": "180",
    })


def test_all_required_projected_pairs_share_the_controlled_key() -> None:
    rows = [
        projected_row("FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts", "101"),
        projected_row("FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts", "102"),
        projected_row("FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts", "103"),
        projected_row("FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts", "104"),
    ]

    comparisons = analysis.build_projected_comparison(rows)
    by_label = {row["comparison"]: row for row in comparisons}

    assert set(by_label) == set(EXPECTED_PAIRS)
    for label, (lhs_method, rhs_method) in EXPECTED_PAIRS.items():
        comparison = by_label[label]
        assert comparison["lhs_method"] == lhs_method
        assert comparison["rhs_method"] == rhs_method
        assert comparison["instance_id"] == "new20x20"
        assert comparison["objective_family"] == "Expected"
        assert comparison["alpha"] == "0.02"
        assert comparison["train_count"] == "200"
        assert comparison["case_id"] == "case07"


def test_projected_pairs_do_not_cross_case_blocks() -> None:
    coverage_poly = projected_row(
        "FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts", "101")
    coverage_exp = projected_row(
        "FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts", "102")
    coverage_exp["case_id"] = "case08"

    comparisons = analysis.build_projected_comparison([coverage_poly, coverage_exp])
    assert comparisons == []


def main() -> int:
    test_all_required_projected_pairs_share_the_controlled_key()
    test_projected_pairs_do_not_cross_case_blocks()
    print("All projected-LLBI paired-comparison tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
