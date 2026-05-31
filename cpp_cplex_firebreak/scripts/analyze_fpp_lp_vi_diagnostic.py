#!/usr/bin/env python3
import csv
import math
from collections import defaultdict
from pathlib import Path

OUT_DIR = Path("results/batch/fpp_lp_vi_diagnostic")
RAW = OUT_DIR / "raw_lp_results.csv"
ROUNDS = OUT_DIR / "round_results.csv"

MIP_DIRS = [
    Path("results/batch/fpp_12mode_benchmark_alpha001/batch_results.csv"),
    Path("results/batch/fpp_12mode_benchmark_alpha002/batch_results.csv"),
    Path("results/batch/fpp_12mode_benchmark_alpha003/batch_results.csv"),
]

VARIANTS = [
    "native_lp",
    "lp_plus_firebreak_upper_bound",
    "lp_plus_dominator",
    "lp_plus_separator_offline",
    "lp_plus_dominator_plus_separator_offline",
]

IMPACT_VARIANTS = [
    ("native -> firebreak_upper_bound", "lp_plus_firebreak_upper_bound"),
    ("native -> dominator", "lp_plus_dominator"),
    ("native -> separator", "lp_plus_separator_offline"),
    ("native -> dominator + separator", "lp_plus_dominator_plus_separator_offline"),
]


def read_csv(path):
    if not path.exists():
        return []
    with path.open(newline="") as f:
        return list(csv.DictReader(f))


def to_float(value, default=math.nan):
    try:
        if value is None or value == "":
            return default
        return float(value)
    except ValueError:
        return default


def to_int(value, default=0):
    try:
        if value is None or value == "":
            return default
        return int(float(value))
    except ValueError:
        return default


def fmt(value):
    if value is None or not math.isfinite(value):
        return ""
    return f"{value:.8f}"


def avg(values):
    vals = [v for v in values if math.isfinite(v)]
    return sum(vals) / len(vals) if vals else math.nan


def write_csv(path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def enrich(raw_rows):
    for row in raw_rows:
        row["_alpha"] = to_float(row["alpha"])
        row["_case_id"] = to_int(row["case_id"])
        row["_initial"] = to_float(row["lp_objective_initial"])
        row["_final"] = to_float(row["lp_objective_final"])
        row["_improvement"] = to_float(row["lp_improvement_abs"])
        row["_improvement_pct"] = to_float(row["lp_improvement_pct"])
        row["_time"] = to_float(row["solve_time_total"], 0.0)
        row["_dom_cuts"] = to_int(row["dominator_cuts_added"])
        row["_sep_cuts"] = to_int(row["separator_cuts_added"])
        row["_total_cuts"] = row["_dom_cuts"] + row["_sep_cuts"]
    return raw_rows


def load_integer_objectives():
    best = {}
    for path in MIP_DIRS:
        for row in read_csv(path):
            try:
                alpha = float(row["alpha"])
                case_id = int(float(row["case_id"]))
                obj = float(row["objective_in_sample"])
            except Exception:
                continue
            key = (round(alpha, 8), case_id)
            best[key] = min(best.get(key, math.inf), obj)
    return best


def summary_rows(raw_rows):
    groups = defaultdict(list)
    for row in raw_rows:
        groups[(row["_alpha"], row["formulation"], row["lp_variant"])].append(row)

    output = []
    for (alpha, formulation, variant), rows in sorted(groups.items()):
        improvements = [r["_improvement"] for r in rows]
        output.append({
            "alpha": fmt(alpha),
            "formulation": formulation,
            "lp_variant": variant,
            "cases": len(rows),
            "avg_lp_bound_initial": fmt(avg(r["_initial"] for r in rows)),
            "avg_lp_bound_final": fmt(avg(r["_final"] for r in rows)),
            "avg_improvement_abs": fmt(avg(improvements)),
            "avg_improvement_pct": fmt(avg(r["_improvement_pct"] for r in rows)),
            "cases_improved": sum(1 for v in improvements if v > 1.0e-7),
            "avg_total_cuts": fmt(avg(r["_total_cuts"] for r in rows)),
            "avg_dominator_cuts": fmt(avg(r["_dom_cuts"] for r in rows)),
            "avg_separator_cuts": fmt(avg(r["_sep_cuts"] for r in rows)),
            "avg_solve_time_total": fmt(avg(r["_time"] for r in rows)),
            "avg_separator_rounds": fmt(avg(to_int(r["separator_rounds"]) for r in rows)),
            "avg_separator_min_cut_calls": fmt(avg(to_int(r["separator_min_cut_calls"]) for r in rows)),
            "avg_separator_time_sec": fmt(avg(to_float(r["separator_time_sec"], 0.0) for r in rows)),
            "min_lp_bound_final": fmt(min(r["_final"] for r in rows)),
            "max_lp_bound_final": fmt(max(r["_final"] for r in rows)),
        })
    return output


def impact_rows(raw_rows):
    by_key = defaultdict(dict)
    for row in raw_rows:
        by_key[(row["_alpha"], row["_case_id"], row["formulation"])][row["lp_variant"]] = row

    integer_best = load_integer_objectives()
    groups = defaultdict(list)
    for (alpha, case_id, formulation), variants in by_key.items():
        native = variants.get("native_lp")
        if not native:
            continue
        for label, variant in IMPACT_VARIANTS:
            target = variants.get(variant)
            if target:
                groups[(alpha, formulation, label, variant)].append((native, target, case_id))

    output = []
    for (alpha, formulation, label, variant), pairs in sorted(groups.items()):
        improvements = [t["_final"] - n["_final"] for n, t, _ in pairs]
        overheads = [t["_time"] - n["_time"] for n, t, _ in pairs]
        cuts = [t["_total_cuts"] for _, t, _ in pairs]
        closures = []
        for native, target, case_id in pairs:
            incumbent = integer_best.get((round(alpha, 8), case_id), math.nan)
            denom = incumbent - native["_final"]
            if math.isfinite(incumbent) and denom > 1.0e-9:
                closures.append((target["_final"] - native["_final"]) / denom)
        output.append({
            "alpha": fmt(alpha),
            "formulation": formulation,
            "comparison": f"{formulation}: {label}",
            "target_variant": variant,
            "cases": len(pairs),
            "avg_bound_improvement": fmt(avg(improvements)),
            "cases_improved": sum(1 for v in improvements if v > 1.0e-7),
            "avg_cuts_added": fmt(avg(cuts)),
            "avg_time_overhead": fmt(avg(overheads)),
            "avg_gap_closed_by_vi": fmt(avg(closures)),
        })
    return output


def separator_round_summary(round_rows):
    if not round_rows:
        return []
    for row in round_rows:
        row["_alpha"] = to_float(row["alpha"])
        row["_case_id"] = to_int(row["case_id"])
        row["_round"] = to_int(row["round"])
        row["_cuts"] = to_int(row["cuts_added"])
        row["_calls"] = to_int(row["min_cut_calls"])
        row["_max_violation"] = to_float(row["max_violation"], 0.0)
        row["_sep_time"] = to_float(row["separation_time"], 0.0)
        row["_improvement"] = to_float(row["objective_improvement_round"], 0.0)

    by_case = defaultdict(list)
    for row in round_rows:
        by_case[(row["_alpha"], row["_case_id"], row["formulation"], row["variant"])].append(row)

    aggregates = defaultdict(list)
    for (alpha, case_id, formulation, variant), rows in by_case.items():
        rows.sort(key=lambda r: r["_round"])
        first = rows[0]
        aggregates[(alpha, formulation, variant)].append({
            "rounds": len(rows),
            "cuts": sum(r["_cuts"] for r in rows),
            "calls": sum(r["_calls"] for r in rows),
            "first_violation": first["_max_violation"],
            "sep_time": sum(r["_sep_time"] for r in rows),
            "improvement": sum(r["_improvement"] for r in rows),
        })

    output = []
    for (alpha, formulation, variant), rows in sorted(aggregates.items()):
        starting = "dominator" if "dominator_plus" in variant else "native"
        output.append({
            "alpha": fmt(alpha),
            "formulation": formulation,
            "starting_variant": starting,
            "separator_variant": variant,
            "cases": len(rows),
            "avg_rounds": fmt(avg(r["rounds"] for r in rows)),
            "avg_separator_cuts_added": fmt(avg(r["cuts"] for r in rows)),
            "avg_min_cut_calls": fmt(avg(r["calls"] for r in rows)),
            "avg_max_violation_first_round": fmt(avg(r["first_violation"] for r in rows)),
            "avg_total_separator_time": fmt(avg(r["sep_time"] for r in rows)),
            "avg_total_lp_improvement_from_separator": fmt(avg(r["improvement"] for r in rows)),
        })
    return output


def validation_report(raw_rows):
    errors = []
    for row in raw_rows:
        if row["_improvement"] < -1.0e-7:
            errors.append(
                f"LP decreased: alpha={row['alpha']} case={row['case_id']} "
                f"formulation={row['formulation']} variant={row['lp_variant']} "
                f"improvement={row['_improvement']}"
            )
        if row["lp_variant"] in ("lp_plus_dominator", "lp_plus_dominator_plus_separator_offline"):
            if row["_dom_cuts"] <= 0:
                errors.append(
                    f"No dominator cuts added: alpha={row['alpha']} case={row['case_id']} "
                    f"formulation={row['formulation']} variant={row['lp_variant']}"
                )
    sep_added = sum(r["_sep_cuts"] for r in raw_rows)
    lines = [
        f"rows={len(raw_rows)}",
        f"validation_errors={len(errors)}",
        f"separator_cuts_added_total={sep_added}",
        f"integer_benchmark_available={bool(load_integer_objectives())}",
    ]
    lines.extend(errors[:50])
    return "\n".join(lines) + "\n"


def main():
    raw_rows = enrich(read_csv(RAW))
    round_rows = read_csv(ROUNDS)
    if not raw_rows:
        raise SystemExit(f"No raw rows found at {RAW}")

    summary = summary_rows(raw_rows)
    impact = impact_rows(raw_rows)
    sep_summary = separator_round_summary(round_rows)

    write_csv(
        OUT_DIR / "summary_by_alpha_formulation_variant.csv",
        [
            "alpha", "formulation", "lp_variant", "cases",
            "avg_lp_bound_initial", "avg_lp_bound_final",
            "avg_improvement_abs", "avg_improvement_pct",
            "cases_improved", "avg_total_cuts", "avg_dominator_cuts",
            "avg_separator_cuts", "avg_solve_time_total",
            "avg_separator_rounds", "avg_separator_min_cut_calls",
            "avg_separator_time_sec", "min_lp_bound_final", "max_lp_bound_final",
        ],
        summary,
    )
    write_csv(
        OUT_DIR / "vi_impact_summary.csv",
        [
            "alpha", "formulation", "comparison", "target_variant", "cases",
            "avg_bound_improvement", "cases_improved", "avg_cuts_added",
            "avg_time_overhead", "avg_gap_closed_by_vi",
        ],
        impact,
    )
    write_csv(
        OUT_DIR / "separator_round_summary.csv",
        [
            "alpha", "formulation", "starting_variant", "separator_variant", "cases",
            "avg_rounds", "avg_separator_cuts_added", "avg_min_cut_calls",
            "avg_max_violation_first_round", "avg_total_separator_time",
            "avg_total_lp_improvement_from_separator",
        ],
        sep_summary,
    )
    (OUT_DIR / "validation_report.txt").write_text(validation_report(raw_rows))

    print(f"Wrote {OUT_DIR / 'summary_by_alpha_formulation_variant.csv'}")
    print(f"Wrote {OUT_DIR / 'vi_impact_summary.csv'}")
    print(f"Wrote {OUT_DIR / 'separator_round_summary.csv'}")
    print(f"Wrote {OUT_DIR / 'validation_report.txt'}")


if __name__ == "__main__":
    main()
