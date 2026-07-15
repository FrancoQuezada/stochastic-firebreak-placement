# Weighted Landscapes Phase 5C1: Restricted Branch-and-Benders

Phase 5C1 adds baseline weighted support to `run-fpp-restricted-branch-benders-oos`.
The scope is restricted to the LP-based restricted-candidate FPP Branch-and-Benders
path. Optional weighted scorers, combinatorial Benders, LLBI/projected LLBI,
dominance changes, DPV, Static-DPV, and heuristic conversions remain deferred.

## Files Modified

- `include/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.hpp`
  adds `weight_map_file`.
- `src/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.cpp`
  loads the Phase 1 weight map, attaches it to train/test optimization
  instances, validates weighted objectives, and records weighted result fields.
- `src/main.cpp` adds `--weight-map-file` to
  `run-fpp-restricted-branch-benders-oos`.
- `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp` validates weighted
  Phase 5C1 options, tags stage objectives as weighted, and preserves weighted
  metadata in stage results.
- `include/benders/RestrictedCandidateCutPool.hpp` and
  `src/benders/RestrictedCandidateCutPool.cpp` add a weight-map hash guard.
- `tests/test_weighted_fpp_restricted_branch_benders_cplex.cpp` adds CPLEX-backed
  weighted restricted tests.
- `Makefile` adds:
  - `build_gpp/test_weighted_fpp_restricted_branch_benders_cplex`
  - `build_gpp/test_weighted_restricted_candidate_activation`
  - `build_gpp/test_weighted_restricted_cut_pool`

## Weight-Map Loading Path

`--weight-map-file <path>` is optional. If omitted, homogeneous unit weights are
used. If provided, the runner resolves the input path and calls
`solver::attach_weight_map_to_optimization_instance`.

The loader validates the complete compact optimization universe through original
Cell2Fire IDs. Missing files, malformed CSV data, duplicate IDs, missing nodes,
extra incompatible nodes, and non-positive or non-finite weights fail before the
solver starts.

The same resolved file is attached to:

- the training optimization instance;
- final train `FppRecourseEvaluator` validation;
- the test optimization instance and test recourse evaluator.

The optimization, train validation, and test validation weight-map hashes must
match.

## Exactness Mechanism

The restricted master keeps the full eligible firebreak vector and applies upper
bounds to inactive candidates. Exact mode still relies on eventual activation of
the complete eligible candidate set. In the Phase 5C1 baseline, the supported
weighted path is:

- `initial_candidate_policy=explicit-list`;
- `activation_policy=none` with final full activation, or
  `activation_policy=activate-all-final`;
- `eventually_activate_all=true`;
- `restricted_heuristic_mode=false`.

Diagnostics report initial and final active candidate counts, whether final full
activation happened, and whether global optimality was certified.

## Weighted Recourse

The restricted callback path reuses the Phase 5B weighted LP scenario subproblem
and persistent scenario subproblem manager. Each scenario recourse is:

```text
min sum_i w_i x_i^s
```

The ignition convention is unchanged: the ignition node always burns, even if it
is selected. A selected non-root firebreak blocks incoming propagation into that
node but does not remove outgoing arcs from an ignition selected as a firebreak.

Expected, CVaR, and mean-CVaR masters use weighted eta values:

- expected: probability-weighted eta;
- CVaR: weighted eta in the tail constraints;
- mean-CVaR: weighted expected and weighted CVaR blend.

## Cut Storage And Activation

Generated Benders cuts are stored over the full compact eligible node universe.
When a cut is inserted into a restricted master, only currently instantiated or
active variable terms are materialized according to the existing restricted
master convention. The stored cut still retains coefficients for candidates
activated later.

The new test `test_weighted_restricted_cut_pool` explicitly generates a weighted
cut while the eventual optimum candidate is inactive, verifies that the inactive
candidate coefficient is present, enumerates feasible full y vectors, and checks
cut RHS validity against exact weighted recourse.

`RestrictedCandidateCutPool` now stores one weight-map hash and rejects attempts
to reset it to a different non-empty hash.

## Supported And Rejected Options

For non-homogeneous maps, Phase 5C1 allows:

- baseline weighted LP lazy Benders separation;
- final full activation;
- exact mode;
- validated weighted root user cuts.

For non-homogeneous maps, Phase 5C1 rejects:

- burn-frequency initialization;
- burn-frequency activation;
- Benders-coefficient activation;
- candidate maintenance policies other than `none`;
- `candidate_score_mode` other than `generic`;
- tail-score diagnostics;
- restricted heuristic mode;
- missing final full activation;
- combinatorial Benders;
- standard LLBI;
- CoverageLLBI;
- PathLLBI;
- projected LLBI variants;
- global dominance preprocessing;
- conditional zero-benefit fixing.

Homogeneous unit behavior is preserved, including omitted maps and explicit
homogeneous maps.

## Result Fields

The restricted OOS runner now records the weighted fields used by other weighted
FPP methods:

- `weight_profile`, `weight_map_file`, `weight_map_hash`;
- `weight_normalized`, `weight_mean`, `weight_min`, `weight_max`, `weight_total`;
- `objective_metric`;
- `solver_weighted_objective`, `evaluator_weighted_objective`;
- `objective_validation_abs_difference`;
- `objective_validation_rel_difference`;
- `objective_validation_passed`;
- `train_expected_weighted_burn_loss`;
- `test_expected_weighted_burn_loss`;
- `train_weighted_var`, `test_weighted_var`;
- `train_weighted_cvar`, `test_weighted_cvar`;
- `train_percentage_landscape_value_burned`;
- `test_percentage_landscape_value_burned`;
- `train_percentage_high_value_weight_burned`;
- `test_percentage_high_value_weight_burned`.

Unweighted burned-area metrics remain unchanged.

## CPLEX Tests

Executed commands:

```bash
make build_gpp/test_weighted_fpp_saa_cplex
./build_gpp/test_weighted_fpp_saa_cplex

make build_gpp/test_weighted_fpp_benders_cplex
./build_gpp/test_weighted_fpp_benders_cplex

make build_gpp/test_weighted_fpp_branch_benders_cplex
./build_gpp/test_weighted_fpp_branch_benders_cplex

make build_gpp/test_fpp_restricted_candidate_branch_benders_small
./build_gpp/test_fpp_restricted_candidate_branch_benders_small

make build_gpp/test_weighted_fpp_restricted_branch_benders_cplex
./build_gpp/test_weighted_fpp_restricted_branch_benders_cplex

make build_gpp/test_weighted_restricted_candidate_activation
./build_gpp/test_weighted_restricted_candidate_activation

make build_gpp/test_weighted_restricted_cut_pool
./build_gpp/test_weighted_restricted_cut_pool

make cplex
make test
```

Observed results:

- weighted FPP-SAA CPLEX tests passed;
- weighted explicit FPP Benders CPLEX tests passed;
- weighted callback FPP Branch-and-Benders CPLEX tests passed;
- weighted restricted FPP Branch-and-Benders tests passed under all three new
  target names;
- `make cplex` passed;
- `make test` passed. The non-CPLEX test suite still skips CPLEX solve checks in
  its non-CPLEX restricted test target by design.

## Smoke Runs

Smoke split:

```text
landscape=Sub20
train_ids=1,2
test_ids=3,4
alpha=0.01
initial_candidate_list=1,2,3,4
eventually_activate_all=true
```

Restricted smoke results:

| run | risk | status | objective | gap | active candidates | full activation | validation diff |
| --- | --- | --- | ---: | ---: | --- | --- | ---: |
| default expected | expected | Optimal | 41.50000000 | 0 | 4 -> 360 | true | 0 |
| homogeneous expected | expected | Optimal | 41.50000000 | 0 | 4 -> 360 | true | 0 |
| heterogeneous expected | expected | Optimal | 40.80802236 | 0 | 4 -> 360 | true | 0 |
| clustered expected | expected | Optimal | 36.61795220 | 0 | 4 -> 360 | true | 0 |
| heterogeneous CVaR | cvar | Optimal | 49.35425644 | 0 | 4 -> 360 | true | 0 |
| clustered mean-CVaR | mean-cvar | Optimal | 44.90617215 | 0 | 4 -> 360 | true | 0 |
| activation trigger | expected | Optimal | 40.80802236 | 0 | 4 -> 360 | true | 0 |

For every non-homogeneous smoke case, the restricted objective matched direct
FPP-SAA, explicit-loop FPP-Benders, and unrestricted callback Branch-and-Benders
with absolute difference `0.0`.

## Example Smoke Command

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --run-id phase5c1_restricted_heterogeneous_expected \
  --time-limit 30 \
  --mip-gap 0 \
  --threads 1 \
  --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --initial-candidate-policy explicit-list \
  --initial-candidate-list 1,2,3,4 \
  --candidate-activation-policy none \
  --eventually-activate-all \
  --restricted-exact-mode \
  --output-json results/weighted_phase5c1_smoke/phase5c1_restricted_heterogeneous_expected.json \
  --output-csv results/weighted_phase5c1_smoke/restricted.csv
```

## Deferred Phase 5C2 Work

The following remain intentionally unconverted:

- weighted burn-frequency initialization and activation;
- weighted Benders-coefficient candidate scoring;
- weighted CVaR tail-aware candidate scoring;
- weighted candidate maintenance and bound controllers beyond the exact baseline;
- weighted restricted heuristic modes;
- weighted interactions with combinatorial Benders, LLBI/projected LLBI,
  dominance preprocessing, and conditional fixing.

## Acceptance Status

Phase 5C1 acceptance criteria are satisfied for the baseline exact restricted
LP-based Branch-and-Benders path:

- restricted recourse uses weighted LP subproblems;
- expected, CVaR, and mean-CVaR use weighted eta values;
- persistent subproblems preserve weighted coefficients and hash metadata;
- lazy cuts come from weighted duals;
- inactive-candidate coefficients are preserved and validated by enumeration;
- eventual full activation preserves exactness;
- restricted solutions match direct, explicit Benders, and unrestricted callback
  weighted solutions on small CPLEX instances and Sub20 smokes;
- unit-weight behavior is unchanged;
- optimization/train/test use one shared weight map hash;
- unsupported optional modules fail clearly for non-homogeneous maps;
- no Phase 5C2, combinatorial, LLBI, DPV, or standalone heuristic functionality
  was implemented.
