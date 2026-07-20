# Weighted Landscapes Phase 6B3B: Projected FPP PathLLBI

Phase 6B3B converts projected FPP PathLLBI to arbitrary positive landscape
weights. It does not implement combinatorial Benders/lifting, DPV, Static-DPV,
manifests, batch integration, or restricted-candidate projected LLBI.

## Existing Formulation Audit

Implementation files:

- `include/benders/FppProjectedLlbi.hpp`
- `src/benders/FppProjectedLlbi.cpp`
- `src/benders/FppBranchBendersSolver.cpp`
- `src/experiments/FppBranchBendersOutOfSampleRunner.cpp`
- `src/io/ExperimentResultWriter.cpp`

Projected PathLLBI has two modes:

- `exp`: separated at root LP points.
- `poly`: one static cut per scenario using the first stored path per node.

Before this phase, projected PathLLBI used unit coefficients and had an
unconverted weighted guard in callback Branch-and-Benders. The path incidence
sets were structural and remained valid, but constants and candidate
coefficients counted cells instead of weighted burned loss.

## Weighted Exact Stored-Path Family

For scenario `s`, destination node `k`, positive destination weight `w_k`, and
stored simple directed ignition-to-`k` paths `P_{s,k}`, the projected stored-path
family chooses either no affine term for `k` or one stored path `p in P_{s,k}`:

```text
eta_s >= sum_{k: pi(k) != 0} w_k * (1 - sum_{i in pi(k)} y_i).
```

The weight is always the destination cell weight `w_k`. Candidate node weights
are never used as coefficients. Path incidence is structural and unweighted.

The ignition/root has an empty path and always contributes its destination
weight to the constant. It creates no firebreak coefficient. This preserves the
FPP convention: the ignition node always burns, selecting the ignition does not
block outgoing arcs, and a selected non-root firebreak blocks incoming
propagation into that node.

The implementation reuses `build_fpp_path_llbi_data`, the same deterministic
stored-path data used by extended PathLLBI. If path enumeration is truncated,
the generated cuts are still valid but represent a weaker stored-path
projection, not the complete directed-path projection.

## Root Separation

For a fractional root LP solution `ybar`, `exp` separation scans the stored paths
for each destination node and chooses the deterministic minimum-`ybar` path:

```text
path_cost(p) = sum_{i in p} ybar_i.
```

The selected path contributes if:

```text
path_cost(p) < 1 - tolerance.
```

The resulting cut uses the existing convention:

```text
eta_s >= gamma + sum_i alpha_i y_i
```

where `gamma` is the sum of selected destination weights and every candidate on
a selected path receives coefficient `-w_k` for that destination. Ties keep the
first stored path, so duplicate cut signatures are deterministic.

## Polynomial Mode

The `poly` mode is retained as a valid fixed subset. It builds path data with
one stored path per node and adds one static first-stored-path cut per scenario.
It is weaker than separated `exp`, but remains valid with positive destination
weights. It is reported as:

```text
weighted-fixed-subset-of-directed-path-projection
```

## Diagnostics

Projected PathLLBI diagnostics are propagated through `ModelResult`,
`StandardExperimentResult`, JSON output, and CSV output:

- `projected_path_llbi_enabled`
- `projected_path_llbi_weighted`
- `projected_path_llbi_mode`
- `projected_path_llbi_weight_map_hash`
- `projected_path_llbi_scenarios_precomputed`
- `projected_path_llbi_destination_nodes`
- `projected_path_llbi_total_paths`
- `projected_path_llbi_total_incidence_terms`
- `projected_path_llbi_nodes_without_paths`
- `projected_path_llbi_enumeration_complete`
- `projected_path_llbi_paths_truncated`
- `projected_path_llbi_separation_calls`
- `projected_path_llbi_cuts_generated`
- `projected_path_llbi_cuts_added`
- `projected_path_llbi_duplicate_cuts`
- `projected_path_llbi_max_violation`
- `projected_path_llbi_precompute_time_sec`
- `projected_path_llbi_separation_time_sec`
- `projected_path_llbi_validity_mode`

CSV compatibility is preserved: these columns are emitted for new CSV files, or
when appending to an existing CSV that already has the extended projected
PathLLBI columns.

## Weighted Safety Scope

Weighted callback Branch-and-Benders now allows projected PathLLBI in both
`exp` and `poly` modes. It also continues to allow the Phase 6B3A projected
CoverageLLBI modes. Only one projected family can be active at a time.

The non-homogeneous weighted guard continues to reject:

- combinatorial Benders;
- restricted-candidate projected CoverageLLBI/PathLLBI;
- DPV and Static-DPV methods that have not been converted.

Exactness still comes from weighted lazy Benders cuts. Projected PathLLBI is a
valid root/static strengthening only.

## Tests

New tests:

- `tests/test_weighted_projected_path_llbi.cpp`
- `tests/test_weighted_projected_path_llbi_validity.cpp`
- `tests/test_weighted_projected_path_llbi_separation.cpp`
- `tests/test_weighted_projected_path_llbi_cplex.cpp`

Updated tests:

- `tests/test_weighted_projected_coverage_llbi_cplex.cpp`
- `tests/test_solution_io.cpp`

The tests cover:

- destination-cell weights in cut constants and coefficients;
- ignition empty-path constant with no firebreak coefficient;
- deterministic minimum-`ybar` path separation and duplicate signatures;
- weighted `poly` mode as a fixed first-stored-path subset;
- exhaustive small-graph validity against direct FPP loss;
- truncated stored-path projections as weaker but valid cuts;
- Expected, CVaR, and Mean-CVaR CPLEX equivalence to direct FPP SAA;
- homogeneous regression;
- root cuts and dominance combined with projected PathLLBI;
- mutual exclusion between projected CoverageLLBI and projected PathLLBI;
- CSV/JSON output for projected PathLLBI diagnostics.

## Validation Commands

```bash
make cplex
make build_gpp/test_weighted_projected_path_llbi \
     build_gpp/test_weighted_projected_path_llbi_validity \
     build_gpp/test_weighted_projected_path_llbi_separation \
     build_gpp/test_weighted_projected_path_llbi_cplex
./build_gpp/test_weighted_projected_path_llbi
./build_gpp/test_weighted_projected_path_llbi_validity
./build_gpp/test_weighted_projected_path_llbi_separation
./build_gpp/test_weighted_projected_path_llbi_cplex
```

Tiny command smoke examples:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1-2 --test-ids 3-4 \
  --alpha 0.01 --run-id phase6b3b_expected_exp \
  --weight-map-file results/weighted_phase6b3b_smoke/sub20_heterogeneous.csv \
  --use-projected-path-llbi-exp \
  --time-limit 30 --threads 1 \
  --output-csv results/weighted_phase6b3b_smoke/smoke.csv

./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1-2 --test-ids 3-4 \
  --alpha 0.01 --run-id phase6b3b_expected_poly \
  --weight-map-file results/weighted_phase6b3b_smoke/sub20_heterogeneous.csv \
  --use-projected-path-llbi-poly \
  --time-limit 30 --threads 1 \
  --output-csv results/weighted_phase6b3b_smoke/smoke.csv
```

Do not launch full batch experiments from this phase.
