#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict


def as_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def as_int(value, default=0):
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def avg(values):
    return sum(values) / len(values) if values else 0.0


def write_rows(path, fieldnames, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def summarize(rows, keys):
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(k, "") for k in keys)].append(row)

    out = []
    for key_values, group in sorted(groups.items()):
        removed = [as_int(r.get("global_dominance_candidates_removed")) for r in group]
        before = [as_int(r.get("eligible_candidate_count_before")) for r in group]
        after = [as_int(r.get("eligible_candidate_count_after")) for r in group]
        classes = [as_int(r.get("global_dominance_equivalence_classes")) for r in group]
        times = [as_float(r.get("global_dominance_precompute_time_sec")) for r in group]
        row = {k: v for k, v in zip(keys, key_values)}
        row.update({
            "rows": len(group),
            "avg_candidates_before": f"{avg(before):.3f}",
            "avg_candidates_removed": f"{avg(removed):.3f}",
            "avg_candidates_after": f"{avg(after):.3f}",
            "min_candidates_removed": min(removed) if removed else 0,
            "max_candidates_removed": max(removed) if removed else 0,
            "avg_equivalence_classes": f"{avg(classes):.3f}",
            "avg_precompute_time_sec": f"{avg(times):.6f}",
        })
        out.append(row)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--results-dir",
        default="results/diagnostics/sub20_alpha001_002_global_dominance_preprocessing",
    )
    args = parser.parse_args()

    input_csv = os.path.join(args.results_dir, "global_dominance_preprocessing_results.csv")
    if not os.path.exists(input_csv):
        raise SystemExit(f"Missing input CSV: {input_csv}")
    with open(input_csv, newline="") as f:
        rows = list(csv.DictReader(f))

    summary_by_case_alpha = summarize(rows, ["case_id", "alpha"])
    summary_by_alpha = summarize(rows, ["alpha"])
    summary_overall = summarize(rows, [])

    base_fields = [
        "rows",
        "avg_candidates_before",
        "avg_candidates_removed",
        "avg_candidates_after",
        "min_candidates_removed",
        "max_candidates_removed",
        "avg_equivalence_classes",
        "avg_precompute_time_sec",
    ]
    write_rows(
        os.path.join(args.results_dir, "summary_by_case_alpha.csv"),
        ["case_id", "alpha"] + base_fields,
        summary_by_case_alpha,
    )
    write_rows(
        os.path.join(args.results_dir, "summary_by_alpha.csv"),
        ["alpha"] + base_fields,
        summary_by_alpha,
    )
    write_rows(
        os.path.join(args.results_dir, "summary_overall.csv"),
        base_fields,
        summary_overall,
    )
    print(f"Wrote summaries under {args.results_dir}")


if __name__ == "__main__":
    main()
