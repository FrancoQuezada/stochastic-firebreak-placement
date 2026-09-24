# Weighted Landscapes Phase 5B: Callback FPP Branch-and-Benders

Phase 5B adds weighted burned-cell loss support to the LP-subproblem callback
FPP Branch-and-Benders command:

```text
run-fpp-branch-benders-oos
```

This phase is limited to the callback LP lazy-cut path and root LP Benders user
cuts. It does not implement weighted restricted-candidate Branch-and-Benders,
combinatorial Benders, LLBI, CoverageLLBI, PathLLBI, projected LLBI, DPV,
Static-DPV, dominance changes, or heuristic changes.

## Files Modified

Callback path:

- `include/benders/FppPersistentScenarioSubproblemManager.hpp`
- `src/benders/FppPersistentScenarioSubproblemManager.cpp`
- `src/benders/FppBranchBendersSolver.cpp`
- `include/experiments/FppBranchBendersOutOfSampleRunner.hpp`
- `src/experiments/FppBranchBendersOutOfSampleRunner.cpp`
- `src/main.cpp`

Build and tests:

- `Makefile`
- `tests/test_weighted_fpp_branch_benders_cplex.cpp`

## Weighted Subproblem Reuse

The callback path reuses the same FPP scenario LP semantics validated in Phase
5A. For each scenario `s`:

```text
Q_s(ybar) = min sum_i w_i x_i^s
```

The compact weight vector is aligned with `OptimizationInstance::node_mapper`.
Only burn variables `x_i^s` are weighted. Firebreak-copy variables, structural
constraints, and reachability mechanics are unchanged.

FPP conventions remain unchanged:

- the ignition always burns;
- selecting the ignition does not block outgoing propagation;
- a reached non-root selected firebreak blocks incoming propagation and has zero
  burn loss;
- non-eligible burned nodes contribute their weights.

## Persistent Subproblems

`FppPersistentScenarioSubproblemManager` now validates compact weights and builds
each persistent scenario objective once with the immutable compact weight vector.
Changing `ybar` only updates fixed first-stage row bounds; objective
coefficients are not rebuilt or remapped inside callbacks.

Diagnostics now include `weight_map_hash`, allowing tests to verify that the
persistent manager was built from the expected optimization map.

Thread-safety follows the existing design: mutable CPLEX subproblem objects are
guarded by the manager mutex, and the shared compact weight vector is immutable
after construction.

## Lazy Candidate Cuts

At an integer candidate, the callback:

1. reads candidate `y` and `eta`;
2. solves each weighted persistent scenario LP;
3. compares `eta_s` with weighted `Q_s(y)`;
4. adds the dual-derived lazy cut when violated:

```text
eta_s >= Q_s(ybar) + sum_i pi_si (y_i - ybar_i)
```

The dual sign convention is unchanged. Coefficients come directly from the
weighted LP duals; legacy cuts are not scaled manually.

## Root User Cuts

Optional root-only fractional user cuts use the same weighted persistent
subproblem. At a root relaxation point, the solver solves the weighted LP,
evaluates the weighted cut violation, and adds globally valid Benders user cuts
through the existing root-only callback.

The scope remains root-only. Maximum rounds, tolerances, and cut insertion policy
are unchanged.

## Master and Bounds

The master variables are unchanged:

- binary firebreak variables `y_i`;
- scenario recourse variables `eta_s`;
- optional CVaR threshold `phi`;
- optional excess variables `xi_s`.

Each `eta_s` now represents weighted scenario loss.

Expected value:

```text
min sum_s rho_s eta_s
```

CVaR:

```text
min phi + 1 / (1 - beta) * sum_s rho_s xi_s
xi_s >= eta_s - phi
xi_s >= 0
```

Mean-CVaR:

```text
min (1 - lambda) * sum_s rho_s eta_s
  + lambda * (phi + 1 / (1 - beta) * sum_s rho_s xi_s)
```

CPLEX incumbent objective, best bound, MIP gap, final exact recourse, and
reported validation objective are all in weighted-loss units.

## Weight Map Loading

`run-fpp-branch-benders-oos` accepts:

```bash
--weight-map-file results/weights/map.csv
```

If omitted, the homogeneous map from the optimization-instance builder preserves
legacy unit-weight behavior. If supplied, the command loads the canonical Phase
1 CSV through `FppWeightedLossUtils`, validates it against the full compact
optimization universe, and reuses the same map for:

- master and callback subproblem construction;
- train recourse validation;
- test recourse validation.

The command fails before CPLEX optimization for missing, malformed, incomplete,
duplicated, incompatible, non-positive, or non-finite maps.

## Unsupported Weighted Strengthening

For non-homogeneous maps, Phase 5B rejects unconverted modules with a clear
error instead of silently applying unit-weight strengthening:

- LLBI;
- CoverageLLBI;
- PathLLBI;
- projected LLBI;
- combinatorial Benders;
- global dominance preprocessing;
- conditional zero-benefit fixing.

Root LP Benders user cuts are converted and remain supported.

## Output Fields

The callback OOS result now reports the shared weighted fields:

```text
weight_profile
weight_map_file
weight_map_hash
weight_normalized
weight_mean
weight_min
weight_max
weight_total
objective_metric
solver_weighted_objective
evaluator_weighted_objective
objective_validation_abs_difference
objective_validation_rel_difference
objective_validation_passed
train_expected_weighted_burn_loss
test_expected_weighted_burn_loss
train_weighted_var
test_weighted_var
train_weighted_cvar
test_weighted_cvar
train_percentage_landscape_value_burned
test_percentage_landscape_value_burned
train_percentage_high_value_weight_burned
test_percentage_high_value_weight_burned
```

Existing physical burned-area and callback diagnostics are preserved.

## Tests

New CPLEX-backed test source:

```text
tests/test_weighted_fpp_branch_benders_cplex.cpp
```

Targets:

```bash
make build_gpp/test_weighted_fpp_branch_benders_callback
make build_gpp/test_weighted_fpp_branch_benders_root_cuts
make build_gpp/test_weighted_fpp_branch_benders_cplex
```

The tests cover:

- persistent weighted subproblem reuse;
- persistent versus one-shot Phase 5A subproblem equality;
- weighted evaluator equality;
- integer lazy-cut tightness;
- exhaustive integer cut validity;
- root fractional cut tightness and validity;
- homogeneous regression;
- expected, CVaR, and mean-CVaR callback equivalence;
- baseline versus root-cut equivalence;
- direct FPP-SAA versus explicit-loop Benders versus callback equivalence;
- unsupported weighted strengthening rejection.

## Smoke Commands

All smokes used Sub20, train scenarios `1,2`, test scenarios `3,4`,
`alpha=0.01`, one thread, and a 30 second time limit.

Default homogeneous expected:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5b_callback_default_expected \
  --time-limit 30 --mip-gap 0 --threads 1 --tolerance 1e-6 \
  --risk-measure expected
```

Explicit homogeneous expected:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5b_callback_homogeneous_expected \
  --time-limit 30 --mip-gap 0 --threads 1 --tolerance 1e-6 \
  --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_homogeneous.csv
```

Root cuts with a heterogeneous map:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5b_callback_heterogeneous_expected_root \
  --time-limit 30 --mip-gap 0 --threads 1 --tolerance 1e-6 \
  --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --use-root-user-cuts --root-user-cut-max-rounds 2 \
  --root-user-cut-tolerance 1e-6
```

## Smoke Summary

```text
run_id                                        risk       root  objective     direct_ref    explicit_ref  lazy  root_cuts
phase5b_callback_default_expected             expected   no    41.500000     n/a           n/a           21    0
phase5b_callback_homogeneous_expected         expected   no    41.500000     n/a           n/a           21    0
phase5b_callback_heterogeneous_expected       expected   no    40.808022     40.808022     40.808022     21    0
phase5b_callback_clustered_expected           expected   no    36.617952     36.617952     36.617952     22    0
phase5b_callback_heterogeneous_expected_root  expected   yes   40.808022     40.808022     40.808022     21    1
phase5b_callback_heterogeneous_cvar           cvar       no    49.354256     49.354256     49.354256     18    0
phase5b_callback_clustered_mean_cvar          mean-cvar  no    44.906172     44.906172     44.906172     21    0
phase5b_callback_clustered_mean_cvar_root     mean-cvar  yes   44.906172     44.906172     44.906172     21    2
```

All smoke objective validation differences were zero.

## Known Limitations

- Weighted support is limited to the LP-subproblem callback path and root LP
  Benders user cuts.
- Restricted-candidate, combinatorial, LLBI, projected LLBI, dominance, DPV,
  and heuristic methods remain deferred.
- Batch manifests were not broadened in this phase.
- Smoke runs are correctness checks, not scientific experiments.
