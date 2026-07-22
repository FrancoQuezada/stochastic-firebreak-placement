#!/usr/bin/env python3
"""Phase 9B publication-table tests. Covers mandatory cases:
  32.21 LaTeX escaping (special method names render safely)
  32.22 stable output ordering (deterministic files)

Usage: python3 scripts/test_weighted_publication_tables.py
"""

from __future__ import annotations

import tempfile
from pathlib import Path

import weighted_analysis_tables as tables
from weighted_analysis_test_helpers import check, report

SPECIAL_METHOD_NAME = r"FPP-SAA_50% & 'Special' #1 {tag} ~x^2\end"


def test_latex_escaping():  # 32.21
    escaped = tables.escape_latex(SPECIAL_METHOD_NAME)
    check(r"\_" in escaped, "underscore is escaped")
    check(r"\%" in escaped, "percent sign is escaped")
    check(r"\&" in escaped, "ampersand is escaped")
    check(r"\#" in escaped, "hash is escaped")
    check(r"\{" in escaped and r"\}" in escaped, "braces are escaped")
    check(r"\textasciitilde{}" in escaped, "tilde is escaped to a safe LaTeX macro")
    check(r"\textasciicircum{}" in escaped, "caret is escaped to a safe LaTeX macro")
    check(r"\textbackslash{}" in escaped, "backslash is escaped to a safe LaTeX macro")


def test_latex_table_with_special_method_name_is_written_safely():
    rows = [{"method": SPECIAL_METHOD_NAME, "instances": 3, "mean_gap_to_best_feasible": 0.01}]
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "table.tex"
        tables.write_latex_table(path, rows, ["method", "instances", "mean_gap_to_best_feasible"],
                                  caption="Test", label="tab:test", bold_column="mean_gap_to_best_feasible")
        text = path.read_text(encoding="utf-8")
        check("\\_" in text and "\\%" in text and "\\&" in text, "the written .tex file contains escaped special characters")
        check("{tag}" not in text, "raw unescaped braces from a method name never survive verbatim in the .tex output")


def test_stable_output_ordering():  # 32.22
    rows = [
        {"method": "Zeta", "instances": 1, "mean_gap_to_best_feasible": 0.02},
        {"method": "Alpha", "instances": 2, "mean_gap_to_best_feasible": 0.01},
    ]
    columns = ["method", "instances", "mean_gap_to_best_feasible"]
    with tempfile.TemporaryDirectory() as tmp1, tempfile.TemporaryDirectory() as tmp2:
        p1 = Path(tmp1) / "t.csv"
        p2 = Path(tmp2) / "t.csv"
        tables.write_csv_table(p1, rows, columns)
        tables.write_csv_table(p2, rows, columns)
        check(p1.read_text(encoding="utf-8") == p2.read_text(encoding="utf-8"),
              "writing the same rows twice produces byte-identical CSV output")


def test_bold_only_marks_within_group_best():
    rows = [
        {"method": "A", "mean_gap_to_best_feasible": 0.0},
        {"method": "B", "mean_gap_to_best_feasible": 0.05},
    ]
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "t.tex"
        tables.write_latex_table(path, rows, ["method", "mean_gap_to_best_feasible"],
                                  bold_column="mean_gap_to_best_feasible", bold_is_minimum=True)
        text = path.read_text(encoding="utf-8")
        lines = [line for line in text.splitlines() if line.startswith("A ") or line.startswith("B ")]
        check("\\textbf{0}" in lines[0], "the minimum value (method A) is bolded")
        check("\\textbf" not in lines[1], "the non-minimum value (method B) is never bolded")


def main() -> int:
    test_latex_escaping()
    test_latex_table_with_special_method_name_is_written_safely()
    test_stable_output_ordering()
    test_bold_only_marks_within_group_best()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
