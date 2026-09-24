# Weighted Landscapes Phase 6A: Structural Dominance and Conditional Zero-Benefit Fixing

Phase 6A validates the structural FPP mechanisms that do not depend on unit
burned-cell counts:

- global candidate dominance preprocessing;
- conditional zero-benefit detection for branch-and-bound local fixing.

The implementation intentionally does not change LLBI, CoverageLLBI, PathLLBI,
projected LLBI, combinatorial Benders, lifting, DPV, Static-DPV, manifests, or
batch launchers.

## Scope Decision

Global dominance is enabled for non-homogeneous weighted FPP callback and
restricted-candidate Branch-and-Benders runs because the existing rule is based
on scenario-wise protected-set inclusion.

Conditional zero-benefit is structurally weight-safe as a detector. The current
FPP callback solvers use CPLEX generic callbacks, which do not expose a safe
node-local y upper-bound tightening operation in the solver code path. Therefore
Phase 6A reports the detector as structurally weight-safe but applies zero local
fixings. This preserves exactness and avoids converting a local condition into a
global constraint.

## Dominance Audit

Files and functions:

- `include/benders/FppStrengthening.hpp`
  - `detail::scenario_dominator_sets`
  - `apply_fpp_global_dominance_preprocessing`
- integrated by:
  - `src/experiments/FppSaaOutOfSampleRunner.cpp`
  - `src/experiments/FppBranchBendersOutOfSampleRunner.cpp`
  - `src/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.cpp`
  - `src/experiments/MethodDispatcher.cpp`

Protected-set definition:

For each scenario, the code builds directed successors from the scenario arcs,
computes root reachability from the ignition, computes dominator sets on the
root-reachable subgraph, and records for each eligible candidate `i` the compact
nodes `k != root` whose dominator set contains `i`. This is the project
equivalent of:

```text
D_s(i) = { k : every directed path from ignition r_s to k passes through i }.
```

Ignition convention:

- ignition is always reachable;
- the ignition is not blocked by selecting it;
- the root node is excluded from protected sets;
- candidate `i` contributes only when it is root-reachable in that scenario.

Absent or unreachable candidates:

- if a candidate is absent from the reachable scenario subgraph, its scenario
  protected set is empty;
- empty protected sets are handled by ordinary set inclusion;
- no weights are used to infer absence or reachability.

Dominance rule:

Candidate `i` dominates candidate `j` if:

```text
D_s(j) subseteq D_s(i) for every training scenario s.
```

The implementation uses `std::includes` on sorted `std::set<int>` protected
sets. It does not compare cardinality, total weighted protected value, burn
frequency, Benders coefficients, DPV scores, or current candidate scores.

Multiple scenarios:

Dominance must hold in every training scenario used by the optimization model.
A single scenario where inclusion fails blocks global removal.

Eligible universe:

The rule is restricted to `opt.eligible_indices`. Dominated candidates are
removed from `eligible_indices` and `eligible_original_nodes` before solver
construction. The restricted-candidate solver therefore builds its active
candidate universe after dominance, and eventual full activation means full
activation of all non-dominated candidates.

Candidate identity:

External reporting remains in original Cell2Fire IDs where the surrounding
runner reports original nodes. Internally, dominance uses compact indices. Phase
6A added deterministic tie-breaking by lowest original Cell2Fire ID, then lowest
compact index.

Identical protected sets:

If two candidates have identical scenario-wise protected sets, exactly one
representative is kept. The representative is deterministic by lowest original
ID, then compact index. Duplicate eligible entries for the same compact node are
also collapsed to the first occurrence.

Budget safeguard:

If removing all dominated candidates would leave fewer eligible candidates than
the exact firebreak budget, preprocessing is skipped and the original candidate
universe is kept.

Warm starts:

The current Phase 6A code applies dominance before the warm-start construction
paths used by the converted OOS runners. Existing generated warm starts are
therefore built over the post-dominance universe. Explicit replacement of a
user-provided warm start containing a removed dominated candidate is not added in
Phase 6A; diagnostics expose `global_dominance_warm_start_replacements = 0`.

Stored cuts:

Restricted-candidate cuts are generated after dominance on the post-dominance
candidate universe. No cut coefficients are generated for candidates removed
from the exact formulation.

## Dominance Validity Proof

For every scenario, if `D_s(j) subseteq D_s(i)`, replacing selected candidate
`j` by `i` weakly preserves every cell protected by `j`. The FPP convention is
structural: selected non-root firebreaks block incoming propagation, while the
ignition still burns and propagates.

For arbitrary finite positive weights `w_k > 0`:

```text
sum_{k in D_s(j)} w_k <= sum_{k in D_s(i)} w_k
```

More importantly, the scenario burned set after replacement cannot include a
cell that was protected only by `j` and not by `i`, because the set-inclusion
condition excludes that case. Each scenario loss is weakly improved or
unchanged. Any componentwise weak improvement of the scenario-loss vector is
valid for expected value, CVaR, and mean-CVaR.

Dominance is independent of the weight values. It must be recomputed when the
scenario set, candidate universe, graph arcs, or propagation convention changes.
Changing only weight magnitudes does not change the dominance relation.

## Conditional Zero-Benefit Audit

Files and functions:

- `include/benders/FppStrengthening.hpp`
  - `detail::reachable_after_fixed_firebreaks`
  - `detect_fpp_conditional_zero_benefit_candidates`
- solver reporting:
  - `src/benders/FppBranchBendersSolver.cpp`
  - `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`

Detector rule:

Given branch-fixed selected firebreaks `F`, each scenario computes reachability
from the ignition while blocking outgoing traversal through non-root nodes in
`F`. A free candidate `j` has zero marginal benefit at that branch node if `j`
is unreachable in every scenario after applying `F`.

The detector:

- uses directed scenario arcs;
- treats the ignition as always reachable;
- does not block the ignition if it appears in the fixed set;
- uses only supplied fixed-one nodes, not incumbent selections;
- does not inspect eta values, Benders coefficients, objective values, scores,
  or weights.

Local versus global:

The rule is valid only at the branch-and-bound node whose local fixed-one set
produces the reachability result. Phase 6A does not add global constraints from
this condition. Because the current generic callback path does not safely expose
node-local y upper-bound tightening, the production solvers report the option as
enabled and structurally weight-safe but apply zero local fixings.

## Conditional Validity Proof

If candidate `j` is not reachable from the ignition in any scenario after the
branch-fixed selected firebreaks are applied, selecting `j` cannot block any
remaining propagation path. Its marginal benefit at that branch node is zero in
every scenario. This is a structural statement, independent of weights.

Since every scenario loss is unchanged by fixing `j = 0` under that local branch
state, expected value, CVaR, and mean-CVaR objectives are unchanged. The proof
does not rely on burned-cell counts.

## Diagnostics

Phase 6A adds or completes:

- `global_dominance_structural_weight_safe`
- `global_dominance_original_candidate_count`
- `global_dominance_candidates_removed`
- `global_dominance_equivalence_classes`
- `global_dominance_post_candidate_count`
- `global_dominance_warm_start_replacements`
- `global_dominance_precompute_time_sec`
- `conditional_zero_benefit_structural_weight_safe`
- `conditional_zero_benefit_callback_calls`
- `conditional_zero_benefit_nodes_checked`
- `conditional_zero_benefit_candidates_checked`
- `conditional_zero_benefit_fixings_attempted`
- `conditional_zero_benefit_fixings_applied`
- `conditional_zero_benefit_variables_fixed_zero`
- `conditional_zero_benefit_scenarios_reachability_computed`
- `conditional_zero_benefit_time_sec`

The existing weighted metadata fields remain unchanged:

- `weight_profile`
- `weight_map_hash`
- `objective_metric`

## Tests

Added tests:

- `tests/test_weighted_global_candidate_dominance.cpp`
- `tests/test_weighted_global_candidate_dominance_cplex.cpp`
- `tests/test_weighted_conditional_zero_benefit_fixing.cpp`
- `tests/test_weighted_conditional_zero_benefit_fixing_cplex.cpp`

Coverage:

- strict scenario-wise set inclusion dominance;
- non-inclusion counterexample where weighted value is larger but sets do not
  include;
- cardinality counterexample;
- multiple-scenario failure to dominate;
- duplicate/equivalent representative determinism;
- dominance invariance under heterogeneous versus clustered weights;
- expected, CVaR, and mean-CVaR objective preservation in CPLEX;
- conditional zero-benefit positive and negative detector cases;
- fixed-one versus incumbent distinction;
- ignition convention;
- callback and restricted solver reporting with zero applied local fixings.

## Validation Commands

```bash
make build_gpp/test_weighted_global_candidate_dominance
./build_gpp/test_weighted_global_candidate_dominance

make build_gpp/test_weighted_global_candidate_dominance_cplex
./build_gpp/test_weighted_global_candidate_dominance_cplex

make build_gpp/test_weighted_conditional_zero_benefit_fixing
./build_gpp/test_weighted_conditional_zero_benefit_fixing

make build_gpp/test_weighted_conditional_zero_benefit_fixing_cplex
./build_gpp/test_weighted_conditional_zero_benefit_fixing_cplex

make build_gpp/test_weighted_fpp_branch_benders_cplex
./build_gpp/test_weighted_fpp_branch_benders_cplex

make build_gpp/test_weighted_fpp_restricted_branch_benders_cplex
./build_gpp/test_weighted_fpp_restricted_branch_benders_cplex

make cplex
make test

git diff --check
git diff --cached --check
```

## Smoke Commands

Example Sub20 callback dominance smoke:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --run-id phase6a_callback_expected_heterogeneous_dominance \
  --time-limit 30 \
  --mip-gap 0 \
  --threads 1 \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --risk-measure expected \
  --use-global-dominance-preprocessing \
  --output-json results/weighted_phase6a_smoke/callback_expected_heterogeneous_dominance.json
```

Example restricted dominance plus conditional diagnostic smoke:

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --run-id phase6a_restricted_expected_heterogeneous_dominance_conditional \
  --time-limit 30 \
  --mip-gap 0 \
  --threads 1 \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --risk-measure expected \
  --initial-candidate-policy explicit-list \
  --initial-candidate-list 1,2,3,4 \
  --candidate-activation-policy benders-coefficients \
  --candidate-activation-batch-size 10 \
  --max-candidate-rounds 1 \
  --restricted-exact-mode \
  --use-global-dominance-preprocessing \
  --use-conditional-zero-benefit-fixing \
  --output-json results/weighted_phase6a_smoke/restricted_expected_heterogeneous_dominance_conditional.json
```

## Remaining Unsupported Modules

Non-homogeneous weighted FPP still rejects:

- standard LLBI;
- CoverageLLBI;
- PathLLBI;
- projected LLBI;
- combinatorial Benders;
- combinatorial lifting;
- DPV and Static-DPV weighted extensions not covered by previous phases.

## Deviations

Conditional zero-benefit local fixing is not applied in the current CPLEX
generic callback implementation. This is deliberate: applying `y_j <= 0` as a
global cut would be invalid for sibling nodes. Phase 6A keeps exactness by
reporting zero applied local fixings until a callback path with safe local bound
tightening is implemented.

Explicit warm-start replacement for user-provided starts containing dominated
candidates is not implemented in Phase 6A. Current generated warm starts are
built after dominance preprocessing in the converted OOS paths.
