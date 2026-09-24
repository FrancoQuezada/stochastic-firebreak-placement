#!/usr/bin/env python3
"""Phase 10 section 14: mean-CVaR field coverage tests.

Audit finding (documented in docs/WEIGHTED_LANDSCAPES_PHASE10.md): no
`mean_cvar`-specific field exists anywhere in the C++ result struct or its
JSON/CSV output, and no per-scenario loss vector is persisted that would let
a downstream consumer recompute it independently. The ONLY safe way to
populate `weighted_fpp_mean_cvar_*` is the documented closed-form
`lambda * expected + (1 - lambda) * cvar`, using each namespace's OWN
expected/cvar values and the row's own `cvar_lambda` -- and only when
`risk_measure` actually declares mean-CVaR (never inferred just because
both expected and cvar happen to be present under a different risk
measure). This is exactly what `weighted_result_normalize._mean_cvar()`
already implements; this test locks that behavior down as a regression.

Usage: python3 scripts/test_weighted_mean_cvar_coverage.py
"""

from __future__ import annotations

import weighted_result_normalize as norm
from weighted_result_test_helpers import base_raw_row, check, report


def test_mean_cvar_computed_when_risk_measure_and_lambda_present():
    row = base_raw_row(
        risk_measure="mean_cvar", cvar_lambda="0.7",
        train_expected_weighted_burn_loss="100.0", train_weighted_cvar="200.0",
    )
    record = norm.normalize_row(row, prefer_json=False)
    expected_value = 0.7 * 100.0 + 0.3 * 200.0
    check(record["weighted_fpp_mean_cvar_in_sample"] is not None, "mean-CVaR is computed when risk_measure=mean_cvar and lambda is present")
    check(abs(record["weighted_fpp_mean_cvar_in_sample"] - expected_value) < 1e-9,
          "mean-CVaR uses the documented closed form lambda*expected + (1-lambda)*cvar")


def test_mean_cvar_missing_when_cvar_component_unavailable():
    row = base_raw_row(risk_measure="mean_cvar", cvar_lambda="0.7", train_weighted_cvar="")
    record = norm.normalize_row(row, prefer_json=False)
    check(record["weighted_fpp_mean_cvar_in_sample"] is None,
          "mean-CVaR is left missing (never fabricated) when the CVaR component is unavailable")


def test_mean_cvar_never_inferred_under_other_risk_measures():
    """Even if both expected and CVaR values happen to be present, mean-CVaR
    must never be computed unless risk_measure actually declares mean-CVaR
    -- inferring it would misrepresent what was actually optimized/evaluated."""
    row = base_raw_row(
        risk_measure="expected", cvar_lambda="0.7",
        train_expected_weighted_burn_loss="100.0", train_weighted_cvar="200.0",
    )
    record = norm.normalize_row(row, prefer_json=False)
    check(record["weighted_fpp_mean_cvar_in_sample"] is None,
          "mean-CVaR is never inferred under risk_measure=expected, even with both components present")

    row_cvar = base_raw_row(
        risk_measure="cvar", cvar_lambda="0.7",
        train_expected_weighted_burn_loss="100.0", train_weighted_cvar="200.0",
    )
    record_cvar = norm.normalize_row(row_cvar, prefer_json=False)
    check(record_cvar["weighted_fpp_mean_cvar_in_sample"] is None,
          "mean-CVaR is never inferred under risk_measure=cvar either")


def test_mean_cvar_computed_independently_per_namespace():
    row = base_raw_row(
        risk_measure="mean_cvar", cvar_lambda="0.5",
        train_expected_weighted_burn_loss="100.0", train_weighted_cvar="200.0",
        test_expected_weighted_burn_loss="150.0", test_weighted_cvar="250.0",
    )
    record = norm.normalize_row(row, prefer_json=False)
    check(abs(record["weighted_fpp_mean_cvar_in_sample"] - 150.0) < 1e-9, "in-sample mean-CVaR uses in-sample components")
    check(abs(record["weighted_fpp_mean_cvar_out_of_sample"] - 200.0) < 1e-9, "OOS mean-CVaR uses OOS components independently")
    check(record["weighted_fpp_mean_cvar_in_sample"] != record["weighted_fpp_mean_cvar_out_of_sample"],
          "the two namespaces are never conflated")


def main() -> int:
    test_mean_cvar_computed_when_risk_measure_and_lambda_present()
    test_mean_cvar_missing_when_cvar_component_unavailable()
    test_mean_cvar_never_inferred_under_other_risk_measures()
    test_mean_cvar_computed_independently_per_namespace()
    return report(__file__)


if __name__ == "__main__":
    raise SystemExit(main())
