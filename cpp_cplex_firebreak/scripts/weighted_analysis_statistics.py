#!/usr/bin/env python3
"""Phase 9B paired statistical comparisons: Wilcoxon signed-rank (via SciPy
when available and appropriate) with a fully deterministic exact-binomial
sign-test fallback, plus Holm-Bonferroni multiple-comparison correction.

No SciPy internals are required for the fallback path or for the effect
size (a matched-pairs rank-biserial correlation, computed manually so it is
identical regardless of which test produced the p-value) -- only the
Wilcoxon p-value itself optionally comes from SciPy.
"""

from __future__ import annotations

import math
import statistics
from math import comb

try:
    import scipy.stats as _scipy_stats
    SCIPY_AVAILABLE = True
except ImportError:  # pragma: no cover - exercised via the fallback test
    _scipy_stats = None
    SCIPY_AVAILABLE = False

DEFAULT_MIN_PAIRS = 6
"""Minimum number of paired, non-missing observations required before any
statistical test is run at all (section 20: 'do not run tests with
insufficient paired observations'). Below this, the comparison is skipped
and reported as such, never silently omitted."""


def _rank_biserial_effect_size(differences: list) -> float | None:
    """Matched-pairs rank-biserial correlation: (W+ - W-) / (W+ + W-), where
    W+/W- are the sums of ranks of |d_i| for positive/negative d_i (average
    ranks for ties in |d_i|; zero differences are excluded, matching the
    Wilcoxon convention). Returns None if there are no nonzero differences."""
    nonzero = [d for d in differences if d != 0]
    if not nonzero:
        return None
    magnitudes = sorted(range(len(nonzero)), key=lambda i: abs(nonzero[i]))
    ranks = [0.0] * len(nonzero)
    i = 0
    while i < len(magnitudes):
        j = i
        while j + 1 < len(magnitudes) and abs(nonzero[magnitudes[j + 1]]) == abs(nonzero[magnitudes[i]]):
            j += 1
        average_rank = (i + 1 + j + 1) / 2.0
        for k in range(i, j + 1):
            ranks[magnitudes[k]] = average_rank
        i = j + 1
    w_pos = sum(r for r, d in zip(ranks, nonzero) if d > 0)
    w_neg = sum(r for r, d in zip(ranks, nonzero) if d < 0)
    total = w_pos + w_neg
    if total == 0:
        return None
    return (w_pos - w_neg) / total


def sign_test(differences: list) -> tuple:
    """Deterministic exact two-sided binomial sign test (p=0.5), requiring
    no external dependency. Returns (statistic, p_value), where statistic is
    the count of strictly positive differences among the nonzero ones."""
    nonzero = [d for d in differences if d != 0]
    n = len(nonzero)
    if n == 0:
        return 0, 1.0
    positive = sum(1 for d in nonzero if d > 0)
    k = min(positive, n - positive)
    tail = sum(comb(n, i) for i in range(0, k + 1))
    p_value = min(1.0, tail * 2 / (2 ** n))
    return positive, p_value


def wilcoxon_signed_rank(differences: list) -> tuple | None:
    """Returns (statistic, p_value) via SciPy, or None if SciPy is
    unavailable or the test raises (e.g. all differences are zero)."""
    if not SCIPY_AVAILABLE:
        return None
    try:
        result = _scipy_stats.wilcoxon(differences, zero_method="wilcox", alternative="two-sided", mode="auto")
        return float(result.statistic), float(result.pvalue)
    except ValueError:
        return None


def _wilcoxon_is_appropriate(differences: list, min_pairs: int) -> bool:
    nonzero = [d for d in differences if d != 0]
    if len(differences) < min_pairs:
        return False
    if len(nonzero) < 2:
        return False
    # Degenerate when every nonzero |difference| ties (rank-sum carries no
    # information beyond the sign test in that case).
    return len({round(abs(d), 12) for d in nonzero}) > 1


def paired_comparison(
    values_a: list, values_b: list, *,
    method_a: str, method_b: str, metric: str, weight_profile: str = "", risk_measure: str = "",
    min_pairs: int = DEFAULT_MIN_PAIRS, tolerance: float = 1.0e-6,
) -> dict:
    """Compare two equal-length, index-aligned (already paired) value lists.
    Callers are responsible for pairing rows by comparison group before
    calling this -- this function only does the statistics."""
    pairs = [(a, b) for a, b in zip(values_a, values_b) if a is not None and b is not None]
    n = len(pairs)
    base = {
        "method_a": method_a,
        "method_b": method_b,
        "metric": metric,
        "weight_profile": weight_profile,
        "risk_measure": risk_measure,
        "paired_n": n,
    }
    if n < min_pairs:
        base.update({
            "skipped": True,
            "skip_reason": f"paired_n={n} < required minimum {min_pairs}",
            "median_difference": None, "mean_difference": None,
            "wins": None, "ties": None, "losses": None,
            "test_name": None, "test_statistic": None, "raw_p_value": None,
            "adjusted_p_value": None, "effect_size": None,
        })
        return base

    differences = [a - b for a, b in pairs]
    wins = sum(1 for d in differences if d < -tolerance)
    losses = sum(1 for d in differences if d > tolerance)
    ties = n - wins - losses

    use_wilcoxon = _wilcoxon_is_appropriate(differences, min_pairs)
    test_name = None
    stat = None
    p_value = None
    if use_wilcoxon:
        result = wilcoxon_signed_rank(differences)
        if result is not None:
            test_name = "wilcoxon_signed_rank" if SCIPY_AVAILABLE else None
            stat, p_value = result
    if test_name is None:
        stat, p_value = sign_test(differences)
        test_name = "sign_test"

    base.update({
        "skipped": False,
        "skip_reason": "",
        "median_difference": statistics.median(differences),
        "mean_difference": statistics.fmean(differences),
        "wins": wins,
        "ties": ties,
        "losses": losses,
        "test_name": test_name,
        "test_statistic": stat,
        "raw_p_value": p_value,
        "adjusted_p_value": None,  # filled in by holm_correct_family()
        "effect_size": _rank_biserial_effect_size(differences),
    })
    return base


def holm_correct_family(comparisons: list) -> list:
    """Holm-Bonferroni correction applied within one family (a list of
    comparison dicts from paired_comparison sharing one metric/weight_profile/
    risk_measure combination, per section 20). Skipped comparisons are left
    with adjusted_p_value=None and excluded from the correction's m."""
    testable = [c for c in comparisons if not c.get("skipped") and c.get("raw_p_value") is not None]
    m = len(testable)
    if m == 0:
        return comparisons
    order = sorted(range(m), key=lambda i: testable[i]["raw_p_value"])
    running_max = 0.0
    adjusted_in_rank_order = []
    for rank, idx in enumerate(order):
        adj = (m - rank) * testable[idx]["raw_p_value"]
        running_max = max(running_max, adj)
        adjusted_in_rank_order.append(min(running_max, 1.0))
    adjusted = [None] * m
    for rank, idx in enumerate(order):
        adjusted[idx] = adjusted_in_rank_order[rank]
    for i, comparison in enumerate(testable):
        comparison["adjusted_p_value"] = adjusted[i]
    return comparisons


# ---------------------------------------------------------------------------
# Family orchestration: build paired comparisons for every method pair within
# each (metric, weight_profile, risk_measure) family, then Holm-correct each
# family independently (section 20: never pool heterogeneous/clustered maps
# or risk measures without an explicit aggregate analysis).
# ---------------------------------------------------------------------------

def run_family_comparisons(
    rows: list, *, metric: str, group_key_fn, metric_field_fn, stratum_fn,
    filter_fn=None, min_pairs: int = DEFAULT_MIN_PAIRS, tolerance: float = 1.0e-6,
) -> list:
    """`group_key_fn` identifies a comparison group (an "instance" for
    pairing purposes); `stratum_fn` maps a record to (weight_profile,
    risk_measure) -- the Holm-correction family boundary. Two methods are
    compared using every group within one stratum where both have a value."""
    by_stratum: dict = {}
    for r in rows:
        if filter_fn is not None and not filter_fn(r):
            continue
        value = metric_field_fn(r)
        if value is None:
            continue
        stratum = stratum_fn(r)
        group_key = group_key_fn(r)
        bucket = by_stratum.setdefault(stratum, {}).setdefault(group_key, {})
        if r.get("method") in bucket:
            bucket[r.get("method")] = None
        else:
            bucket[r.get("method")] = value

    all_comparisons = []
    for stratum in sorted(by_stratum, key=lambda s: (s[0] or "", s[1] or "")):
        groups = {gk: {m: v for m, v in bucket.items() if v is not None} for gk, bucket in by_stratum[stratum].items()}
        methods = sorted({m for bucket in groups.values() for m in bucket})
        family = []
        for i, method_a in enumerate(methods):
            for method_b in methods[i + 1:]:
                values_a, values_b = [], []
                for bucket in groups.values():
                    if method_a in bucket and method_b in bucket:
                        values_a.append(bucket[method_a])
                        values_b.append(bucket[method_b])
                family.append(paired_comparison(
                    values_a, values_b, method_a=method_a, method_b=method_b, metric=metric,
                    weight_profile=stratum[0], risk_measure=stratum[1],
                    min_pairs=min_pairs, tolerance=tolerance,
                ))
        all_comparisons.extend(holm_correct_family(family))
    return all_comparisons
