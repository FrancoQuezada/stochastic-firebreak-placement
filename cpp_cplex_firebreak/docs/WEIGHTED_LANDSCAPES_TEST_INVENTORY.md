# Weighted Landscapes: Test Inventory (Phase 10)

Machine-readable inventory:
[`docs/weighted_landscapes_test_inventory.csv`](weighted_landscapes_test_inventory.csv)
(columns: `test_name, phase, language, requires_cplex, requires_instance_data,
expected_runtime_class, coverage_area`).

## Summary

| | Count |
|---|---|
| Total weighted-landscape tests | 95 |
| C++ (`tests/*.cpp`) | 67 |
| Python (`scripts/test_weighted*.py`, `test_weight_manifest_generation.py`) | 28 |
| Require a CPLEX-enabled build/license for full coverage | 13 |
| Require local instance data (`new_instances/`) | 2 (`test_weighted_manifest_worker.py`, `test_weight_manifest_generation.py`) |

## Classification

- **Unit**: pure structural/logic tests, no solver execution
  (`*_validity.cpp`, `*_counterexamples.cpp`, `*_diagnostics.cpp`,
  `*_reporting.cpp`, and every `test_weighted_result_*.py`/
  `test_weighted_*.py` analysis test in Phase 9A/9B/10 — all in-memory,
  synthetic-fixture based, no binary invocation).
- **CPLEX integration** (13): every C++ test with a `_cplex` suffix
  (`test_weighted_fpp_saa_cplex`-style binaries do not exist under that
  exact name in this checkout's `tests/*.cpp` list, but e.g.
  `test_weighted_fpp_combinatorial_cplex.cpp`,
  `test_weighted_fpp_restricted_branch_benders_cplex.cpp`,
  `test_weighted_conditional_zero_benefit_fixing_cplex.cpp`, etc. do) plus
  `scripts/test_weighted_manifest_worker.py` (invokes the real compiled
  binary end to end). These still build and run under the non-CPLEX
  `make test` target, but internally skip the actual solve portion
  (confirmed by this phase's `make test` run: "Skipping ... because CPLEX
  is not enabled" for each) — full coverage requires `make cplex` first.
- **Integration**: `scripts/test_weight_manifest_generation.py` (drives the
  real manifest generator + `ensure-weight-map` against local instance
  data, no solver invocation).
- **End-to-end**: `scripts/test_weighted_analysis_end_to_end.py` (real
  Phase 8B smoke fixture through the full Phase 9A merge -> Phase 9B
  analysis pipeline, plus a synthetic multi-instance fixture for
  statistics).
- **Regression** (Phase 10 additions): `test_weighted_paired_gate_regression.py`,
  `test_weighted_replicate_aggregation.py`, `test_weighted_table_stratification.py`,
  `test_weighted_mean_cvar_coverage.py`, `test_weighted_failure_paths.py`.

## Duplication / obsolescence review

- No duplicated test coverage was found: each C++ test targets one
  specific weighted feature (one phase's conversion), and the Python tests
  are cleanly layered (Phase 8A/8B manifest/worker infrastructure, Phase 9A
  schema/merge, Phase 9B analysis, Phase 10 regression) with no two files
  asserting the same behavior.
- No obsolete test targets were found. All 67 C++ weighted test binaries
  are still wired into `Makefile`'s `TEST_EXES` and pass under `make test`
  (verified this phase). No test was removed or skipped for being slow —
  per instructions, slowness alone is never a deletion criterion.

## Known gap (not closed this phase, documented per section 15/23)

No explicit duplicate-original-firebreak-ID rejection test was found for
`core::FirebreakSolution::from_csv` (it does validate non-empty,
numeric, and positive-ID tokens). Adding one is a small, well-scoped
candidate for a future phase; not added here to avoid touching
solver-adjacent C++ without a dedicated full regression pass.
