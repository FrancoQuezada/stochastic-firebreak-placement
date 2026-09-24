# Weighted Landscapes Phase 6B3A: Projected FPP CoverageLLBI

Phase 6B3A converts projected FPP CoverageLLBI to arbitrary positive landscape
weights. It does not implement projected PathLLBI, combinatorial Benders,
lifting, DPV, Static-DPV, manifests, or batch integration.

## Existing Formulation Audit

Implementation files:

- `include/benders/FppProjectedLlbi.hpp`
- `src/benders/FppProjectedLlbi.cpp`
- `src/benders/FppBranchBendersSolver.cpp`

Projected CoverageLLBI has two modes:

- `exp`: separated at root LP points.
- `poly`: one static all-unsaturated support cut per scenario.

Both modes reuse `build_fpp_coverage_llbi_data(opt, true)`, so the projected
and extended CoverageLLBI formulations share the same baseline reachable cells
and coverage sets.

Before this phase, projected CoverageLLBI used unit coefficients. The existing
structural coverage sets were already correct, but RHS constants and candidate
coefficients counted cells instead of weighted burned loss.

## Weighted Exact Projected Family

For scenario `s`, baseline burned cells `K_s`, empty-firebreak weighted loss
`Q_empty`, destination weights `w_k`, coverage set `A_k`, and any subset
`U subseteq K_s`, the exact projected coverage family is:

```text
eta_s >= Q_empty
       - sum_{k not in U} w_k
       - sum_{k in U} w_k sum_{i in A_k} y_i.
```

The weight is always the destination cell weight `w_k`. Candidate weights are
not used.

Cells with empty coverage sets remain in `Q_empty` and do not create projected
coefficients. The ignition/root still burns under the FPP convention, even if
it is selected; selecting the ignition does not block outgoing arcs.

## Root Separation

For a fractional root LP solution `ybar`, separation chooses:

```text
U = { k : sum_{i in A_k} ybar_i < 1 - tolerance }.
```

For `k not in U`, the cut subtracts `w_k` into the constant. For `k in U`, it
adds `-w_k` to every covering candidate coefficient. The stored cut convention
is:

```text
eta_s >= gamma + sum_i alpha_i y_i
```

with negative `alpha_i` values. `BendersCut::evaluateAt` and CPLEX insertion
use the existing convention unchanged.

## Polynomial Mode

The `poly` mode is retained as a valid static subset of the exact projected
family. It uses the all-unsaturated support set:

```text
U = { k in K_s : A_k is nonempty }.
```

This is weaker than separated `exp` but remains valid with positive weights.
It is reported as:

```text
weighted-subset-of-exact-per-cell-capped-coverage-projection
```

## Diagnostics

New projected CoverageLLBI diagnostics are propagated through `ModelResult`,
`StandardExperimentResult`, JSON output, and CSV output:

- `projected_coverage_llbi_weighted`
- `projected_coverage_llbi_mode`
- `projected_coverage_llbi_weight_map_hash`
- `projected_coverage_llbi_scenarios_precomputed`
- `projected_coverage_llbi_baseline_cells`
- `projected_coverage_llbi_nonempty_coverage_sets`
- `projected_coverage_llbi_total_incidence_terms`
- `projected_coverage_llbi_separation_calls`
- `projected_coverage_llbi_cuts_generated`
- `projected_coverage_llbi_cuts_added`
- `projected_coverage_llbi_duplicate_cuts`
- `projected_coverage_llbi_max_violation`
- `projected_coverage_llbi_precompute_time_sec`
- `projected_coverage_llbi_separation_time_sec`
- `projected_coverage_llbi_validity_mode`

CSV compatibility is preserved: these columns are emitted for new CSV files, or
when appending to an existing CSV that already has the extended projected
CoverageLLBI columns.

## Weighted Safety Scope

Weighted callback Branch-and-Benders now allows projected CoverageLLBI in both
`exp` and `poly` modes. The non-homogeneous weighted guard continues to reject:

- projected PathLLBI;
- combinatorial Benders;
- restricted-candidate projected CoverageLLBI/PathLLBI.

Exactness still comes from weighted lazy Benders cuts. Projected CoverageLLBI is
a valid root/static strengthening only.

## Tests

New tests:

- `tests/test_weighted_projected_coverage_llbi.cpp`
- `tests/test_weighted_projected_coverage_llbi_validity.cpp`
- `tests/test_weighted_projected_coverage_llbi_cplex.cpp`

The tests cover:

- destination-cell weights in cut constants and coefficients;
- fractional separation against exhaustive enumeration of the exact projected
  family;
- polynomial mode as a valid weighted subset;
- stable duplicate cut signatures;
- Expected, CVaR, and Mean-CVaR CPLEX equivalence to direct FPP SAA;
- homogeneous regression;
- root cuts and dominance combined with projected CoverageLLBI;
- continued rejection of weighted projected PathLLBI.

## Validation Commands

```bash
make cplex
make build_gpp/test_weighted_projected_coverage_llbi \
     build_gpp/test_weighted_projected_coverage_llbi_validity \
     build_gpp/test_weighted_projected_coverage_llbi_cplex
./build_gpp/test_weighted_projected_coverage_llbi
./build_gpp/test_weighted_projected_coverage_llbi_validity
./build_gpp/test_weighted_projected_coverage_llbi_cplex
```

Tiny command smoke examples:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1-2 --test-ids 3-4 \
  --alpha 0.01 --run-id phase6b3a_expected_exp \
  --weight-map-file results/weighted_phase6b3a_smoke/sub20_heterogeneous.csv \
  --use-projected-coverage-llbi-exp \
  --time-limit 30 --threads 1 \
  --output-csv results/weighted_phase6b3a_smoke/smoke.csv

./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1-2 --test-ids 3-4 \
  --alpha 0.01 --run-id phase6b3a_expected_poly \
  --weight-map-file results/weighted_phase6b3a_smoke/sub20_heterogeneous.csv \
  --use-projected-coverage-llbi-poly \
  --time-limit 30 --threads 1 \
  --output-csv results/weighted_phase6b3a_smoke/smoke.csv
```

Do not launch full batch experiments from this phase.
