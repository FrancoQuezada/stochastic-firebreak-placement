# Weighted Landscapes Phase 6C1: Baseline FPP Combinatorial Benders

Phase 6C1 converts the baseline integer-incumbent FPP combinatorial Benders
path-activation cut to arbitrary positive landscape weights. This phase is
intentionally narrow: it does not implement lifting, initial cuts, fractional
cuts, root combinatorial user cuts, cut sampling, eta-desc ordering,
restricted-candidate combinatorial Benders, DPV, Static-DPV, manifests, or batch
method changes.

## Cut Family Audit

The legacy cut is generated at an integer incumbent `ybar` for scenario `s`.
Propagation is evaluated with the FPP convention:

- the ignition/root always burns;
- selecting the ignition does not block outgoing arcs;
- a selected non-root firebreak blocks incoming propagation into that node;
- a reached selected non-root firebreak does not burn and stops propagation;
- non-eligible burned cells still contribute to loss.

For each active burned destination `k`, the separator stores one selected
activation path `P_{s,k}` from the ignition to `k`. The unit cut was:

```text
eta_s >= |A_s(ybar)| - sum_i n_i y_i
```

where `A_s(ybar)` is the incumbent active burned set and `n_i` counts active
destinations whose selected activation path contains candidate `i`.

The weighted Phase 6C1 cut replaces destination counts with destination-cell
weights:

```text
eta_s >= sum_{k in A_s(ybar)} w_k
       - sum_i (sum_{k: i in P_{s,k}} w_k) y_i.
```

The coefficient uses the weight of the protected destination `k`, not the weight
of candidate `i`. The ignition contributes its own weight through the constant
with an empty path and no coefficient.

## Validity Scope

For binary `y`, each selected destination-path term is valid because:

```text
w_k * (1 - sum_{i in P_{s,k}} y_i) <= w_k * x_k(y)
```

where `x_k(y)` is the exact FPP burn indicator for destination `k`. If any
candidate on the stored activation path is selected, the left side is
non-positive and therefore valid. If no candidate on that path is selected, the
same directed path remains open, so `k` burns and the term is at most `w_k`.
Summing over active incumbent destinations gives a valid lower bound on exact
weighted recourse loss.

This proof is for integer incumbent separation without lifting. Phase 6C1
therefore accepts non-homogeneous maps only when all of these options hold:

```text
enabled=true
lift_mode=none
scenario_order=eta-asc
cut_sampling_ratio=1
separate_fractional=false
initial_cuts=false
```

For non-homogeneous maps, the implementation rejects root combinatorial user
cuts, standard/projected LLBI combined through this combinatorial path,
combinatorial lifting, posterior/heuristic strengthening, fractional
separation, initial cuts, sampling, eta-desc ordering, structural dominance, and
conditional zero-benefit fixing when used with combinatorial Benders.

## Implementation Notes

Main implementation files:

- `include/benders/FppCombinatorialBenders.hpp`
- `src/benders/FppCombinatorialBenders.cpp`
- `src/benders/FppBranchBendersSolver.cpp`
- `src/experiments/FppBranchBendersOutOfSampleRunner.cpp`
- `src/experiments/MethodDispatcher.cpp`
- `include/solver/ModelResult.hpp`
- `include/io/ExperimentResultWriter.hpp`
- `src/io/ExperimentResultWriter.cpp`

The separator now keeps compact cell weights from the optimization instance.
If no explicit map is present, unit weights preserve legacy behavior. Duplicate
cut signatures include the weight-map hash and eligible candidate universe so
cuts from different weighted landscapes are not merged accidentally.

## Diagnostics

The result structures and CSV/JSON writers now report:

- `combinatorial_benders_weighted`
- `combinatorial_benders_mode`
- `combinatorial_benders_weight_map_hash`
- `combinatorial_benders_weighted_recourse_evaluations`
- `combinatorial_benders_duplicate_cuts`
- `combinatorial_benders_cuts_tight_at_incumbent`
- `combinatorial_benders_lifting_enabled`
- `combinatorial_benders_scenario_sampling_enabled`
- `combinatorial_benders_max_tightness_error`
- `combinatorial_benders_max_violation`
- `combinatorial_benders_propagation_time_sec`
- `combinatorial_benders_cut_build_time_sec`
- `combinatorial_benders_validity_mode`

Existing combinatorial diagnostics continue to report enabled state, lift mode,
scenario order, cut sampling ratio, fractional/initial flags, cuts added,
scenarios checked, separation time, average paths per cut, average cut nonzeros,
and violated-cut counts.

Phase 6C1 also emits repository-neutral aliases for the same baseline
combinatorial diagnostics:

- `combinatorial_weighted`
- `combinatorial_mode`
- `combinatorial_weight_map_hash`
- `combinatorial_candidate_callbacks`
- `combinatorial_scenarios_evaluated`
- `combinatorial_weighted_recourse_evaluations`
- `combinatorial_cuts_generated`
- `combinatorial_cuts_added`
- `combinatorial_duplicate_cuts`
- `combinatorial_cuts_tight_at_incumbent`
- `combinatorial_max_tightness_error`
- `combinatorial_max_violation`
- `combinatorial_propagation_time_sec`
- `combinatorial_cut_build_time_sec`
- `combinatorial_callback_time_sec`
- `combinatorial_validity_mode`
- `combinatorial_lifting_enabled`
- `combinatorial_fractional_cuts_enabled`
- `combinatorial_initial_cuts_enabled`
- `combinatorial_scenario_sampling_enabled`

## Tests

New tests:

- `tests/test_weighted_fpp_combinatorial_cut.cpp`
- `tests/test_weighted_fpp_combinatorial_cut_validity.cpp`
- `tests/test_weighted_fpp_combinatorial_callback.cpp`
- `tests/test_weighted_fpp_combinatorial_cplex.cpp`

Updated tests:

- `tests/test_solution_io.cpp`

The tests cover exact weighted constants and coefficients, ignition semantics,
selected non-root firebreak semantics, non-eligible burned-cell contribution,
small exhaustive cut validity, randomized small directed graphs with cycles,
policy rejection for unsupported non-homogeneous options, CSV/JSON diagnostic
output, and CPLEX equivalence to direct FPP-SAA and LP callback
Branch-and-Benders for Expected, CVaR, and Mean-CVaR objectives.

## Validation Commands

```bash
make cplex
make test
make build_gpp/test_weighted_fpp_saa_cplex \
     build_gpp/test_weighted_fpp_branch_benders_cplex \
     build_gpp/test_weighted_fpp_combinatorial_cplex
./build_gpp/test_weighted_fpp_saa_cplex
./build_gpp/test_weighted_fpp_branch_benders_cplex
./build_gpp/test_weighted_fpp_combinatorial_cplex
git diff --check
git diff --cached --check
```

Tiny smoke example:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 --train-ids 1-2 --test-ids 3-4 \
  --alpha 0.01 --run-id phase6c1_heterogeneous_expected_combinatorial \
  --weight-map-file results/weighted_phase6c1_smoke/weights/sub20_heterogeneous.csv \
  --use-combinatorial-benders \
  --combinatorial-benders-lift none \
  --combinatorial-benders-cut-sampling-ratio 1 \
  --combinatorial-benders-scenario-order eta-asc \
  --combinatorial-benders-separate-fractional false \
  --combinatorial-benders-initial-cuts false \
  --time-limit 30 --threads 1 --mip-gap 0 \
  --output-json results/weighted_phase6c1_smoke/heterogeneous_expected_combinatorial.json \
  --output-csv results/weighted_phase6c1_smoke/smoke.csv
```

Do not launch full batch experiments from this phase.

## Smoke Results

The Phase 6C1 smoke used `Sub20`, train scenarios `1-2`, test scenarios `3-4`,
`alpha=0.01`, `threads=1`, `mip_gap=0`, and CPLEX time limit `30`.

Generated weight maps:

```text
homogeneous:   fnv1a64:37686b4201c1b949
heterogeneous: fnv1a64:4c068f217a93170f
clustered:     fnv1a64:df988a642f271d17
```

Combinatorial baseline smokes:

```text
case                         risk       status   objective      validation diff
default homogeneous          expected   Optimal  41.50000000    0
explicit homogeneous         expected   Optimal  41.50000000    0
heterogeneous                expected   Optimal  40.25326732    0
clustered                    expected   Optimal  55.04142464    0
heterogeneous                cvar       Optimal  47.34752030    0
clustered                    mean-cvar  Optimal  69.31254101    0
```

Non-homogeneous objective comparisons:

```text
case                 combinatorial  direct FPP-SAA  LP callback   max diff
heterogeneous exp    40.25326732    40.25326732     40.25326732   0
clustered exp        55.04142464    55.04142464     55.04142464   0
heterogeneous cvar   47.34752030    47.34752030     47.34752030   0
clustered mean-cvar  69.31254101    69.31254101     69.31254101   0
```

The negative smoke with non-homogeneous weights, combinatorial Benders, and
global dominance failed as intended:

```text
Non-homogeneous weighted FPP combinatorial Benders Phase 6C1 keeps global
dominance disabled until the combinatorial separator remapping is separately
validated.
```

## Deferred Work

Future phases should validate or redesign weighted combinatorial lifting,
fractional separation, initial/root combinatorial cuts, scenario sampling,
eta-desc ordering, restricted-candidate combinatorial cuts, and any DPV or
Static-DPV combinatorial analogues before enabling them for non-homogeneous
weights.
