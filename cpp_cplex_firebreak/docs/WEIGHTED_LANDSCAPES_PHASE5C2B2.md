# Weighted Landscapes Phase 5C2B2: Candidate Bounds and Pruning Audit

Branch: `feature/instance-generation-updates`

Scope: audit and safe treatment of candidate-bound, permanent-pruning, early
termination, and restricted exactness mechanisms in the restricted-candidate FPP
Branch-and-Benders solver.

Conservative outcome: no permanent weighted candidate-pruning mechanism is
enabled for non-homogeneous maps. Exact weighted operation continues to rely on
eventual full activation of the eligible candidate universe.

## Mechanism Audit

### `CandidateBoundController`

Files:

- `include/benders/CandidateBoundController.hpp`
- `src/benders/CandidateBoundController.cpp`

Current formula:

- `UB_j = 1` if candidate `j` is active in `RestrictedCandidateManager`;
- `UB_j = 0` if candidate `j` is inactive;
- after `activateAll()`, `UB_j = 1` for every eligible candidate.

Inputs:

- candidate count;
- active mask from `RestrictedCandidateManager`.

Uses:

- burned-cell count: no;
- scenario loss: no;
- Benders coefficients: no;
- master objective: no;
- incumbent objective: no;
- candidate scores: no;
- structural graph information: no.

Classification:

- structural and weight-independent;
- temporarily restricts the stage master;
- does not permanently exclude a candidate;
- does not certify optimality;
- remains unchanged for arbitrary positive cell weights.

Exactness argument:

- restricted stages solve a model with some candidate variables fixed to zero;
- exactness is not claimed for these restricted stages;
- final exact mode calls `manager.activateAll()`, rebuilds the master, reinserts
  full-space cuts, and solves with all eligible candidates available;
- the final full solve uses weighted objective and weighted cuts.

Implementation decision:

- retained unchanged;
- diagnostics now label this as `active-set-upper-bound`;
- permanent pruning count is always zero.

### Candidate Activation Rounds

Files:

- `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`
- `RestrictedCandidateManager::activateTopK`

Current formula:

- score inactive candidates by burn frequency, Benders coefficients, or
  CVaR-tail blend;
- activate highest scores with deterministic candidate-ID tie-breaking.

Uses:

- candidate scores: yes;
- Benders coefficients: yes for Benders scoring;
- scenario losses: yes for CVaR-tail scoring;
- burned-cell count: no bound use.

Classification:

- heuristic ordering only;
- never a permanent-pruning certificate;
- exact weighted mode still requires eventual full activation.

Weighted decision:

- already converted in Phase 5C2A;
- not promoted into a bound in Phase 5C2B2.

### Temporary Maintenance Deactivation

Files:

- `RestrictedCandidateMaintenanceTracker`
- `RestrictedCandidateManager::deactivateCandidates`
- `FppRestrictedCandidateBranchBendersSolver`

Current formula:

- after activation, if active count exceeds target, deactivate low-scoring
  active candidates subject to budget, age, cooldown, incumbent, newly
  activated, and tail-protection safeguards.

Classification:

- temporary effort allocation;
- no permanent removal;
- no exactness certificate;
- full activation overrides it in exact mode.

Weighted decision:

- already converted in Phase 5C2B1;
- not considered a pruning bound.

### Early Termination Without Full Activation

Files:

- `FppRestrictedCandidateBranchBendersSolver::solve`
- `effective_candidate_round_limit`

Current behavior:

- if `eventually_activate_all=false`, the solver returns a restricted feasible
  or restricted heuristic result;
- `global_optimality_certified=false`;
- `restricted_bound_is_global=false`;
- `final_lower_bound_is_global=false`.

Inputs:

- `restricted_heuristic_mode`;
- `eventually_activate_all`;
- `stop_after_candidate_rounds`;
- time-limit status.

Classification:

- heuristic or incomplete restricted run;
- not an exactness certificate;
- can leave candidates inactive;
- prohibited for non-homogeneous weighted exact operation.

Weighted decision:

- non-homogeneous weighted runs still reject `eventually_activate_all=false`;
- no silent switch from exact to heuristic mode.

### Root User Cuts

Files:

- restricted solver root-user-cut path in
  `FppRestrictedCandidateBranchBendersSolver.cpp`.

Current formula:

- adds globally valid Benders user cuts at the root when enabled.

Classification:

- structural/cut-strengthening, not candidate pruning;
- does not remove candidates;
- does not avoid full activation.

Weighted decision:

- retained from earlier weighted phases;
- not a candidate-bound mechanism.

### LLBI, Projected LLBI, Dominance, Conditional Fixing, Combinatorial Benders

Files/options:

- `use_lifted_lower_bounds`;
- `FppStrengtheningOptions` coverage/path/projected LLBI flags;
- `use_global_dominance_preprocessing`;
- `use_conditional_zero_benefit_fixing`;
- `FppCombinatorialBendersOptions`.

Current effect:

- can add strengthening constraints or reduce candidate/instance structure in
  other paths;
- some mechanisms can permanently remove candidates or fix variables.

Weighted validity:

- not audited as valid for arbitrary positive weights in this phase;
- can rely on unit-loss or unconverted objective assumptions;
- conditional zero-benefit work is deferred to Phase 6.

Implementation decision:

- rejected for non-homogeneous weighted restricted Branch-and-Benders;
- no weighted conversion implemented in Phase 5C2B2.

## Counterexamples

### Direct Cell Weight Is Not a Benefit Bound

A low-weight candidate can protect a high-weight downstream region. Test
`test_weighted_restricted_bound_counterexamples` constructs:

- candidate at node 2 with weight `0.01`;
- downstream node 3 with weight `100`;
- selecting node 2 blocks both node 2 and node 3.

The improvement is greater than `100`, far above direct cell weight `0.01`.
Therefore `w_i` is not a safe upper bound on candidate benefit.

### Zero Current Score Is Not Permanent Zero Benefit

A candidate can have no score under a current partial cut/sample view while
still reducing weighted recourse in the full graph. Phase 5C2B2 does not use
zero score as a pruning certificate.

## Diagnostics Added

Solver result and restricted JSON now expose:

- `candidate_bounds_enabled`;
- `candidate_bounds_weighted`;
- `candidate_bound_type`;
- `candidate_bound_map_hash`;
- `candidates_evaluated_by_bound`;
- `candidates_permanently_pruned`;
- `candidates_not_pruned_due_to_safety`;
- `early_exactness_certificate_used`;
- `full_activation_avoided`;
- `unvalidated_bound_rejected`.

For exact non-homogeneous weighted runs:

- `candidate_bound_type = active-set-upper-bound`;
- `candidates_permanently_pruned = 0`;
- `early_exactness_certificate_used = false`;
- `full_activation_avoided = false` after successful exact solve;
- full activation remains the exactness fallback.

The broad batch CSV schema was not redesigned.

## Cache and Hash Rules

No persistent candidate-bound cache exists in this phase.

If a future bound cache is introduced, its key must include:

- `weight_map_hash`;
- risk measure;
- `cvar_beta`;
- `cvar_lambda`;
- training scenario identity;
- candidate universe identity;
- incumbent/reference-solution identity when applicable;
- bound algorithm version.

The current diagnostics record the active weight-map hash for the structural
active-set bound.

## Exact Versus Heuristic Mode

Exact mode:

- `eventually_activate_all=true`;
- no unproven permanent pruning;
- no early exactness certificate;
- final solve with all candidates is required for global optimality.

Heuristic/incomplete mode:

- `eventually_activate_all=false` or `restricted_heuristic_mode=true`;
- results do not claim full-problem optimality;
- non-homogeneous weighted heuristic mode remains rejected until separately
  validated.

## Tests Added

- `test_weighted_restricted_candidate_bounds`
  - validates structural active-set UBs;
  - verifies default no-pruning diagnostics;
  - verifies heuristic result flags do not claim global optimality.
- `test_weighted_restricted_bound_counterexamples`
  - validates downstream-value counterexample;
  - confirms direct cell weight is not a valid benefit bound.
- `test_weighted_restricted_bounds_cplex`
  - CPLEX-backed alias over weighted restricted CPLEX tests;
  - validates exact weighted no-pruning diagnostics;
  - validates full activation fallback;
  - validates unsupported weighted bound/pruning options fail before solve.

Existing `test_weighted_fpp_restricted_branch_benders_cplex` now also checks:

- no silent permanent pruning;
- no early exactness certificate;
- active-set upper-bound diagnostics;
- rejection of weighted heuristic/no-full-activation mode.

## Validation Commands

```bash
make build_gpp/test_weighted_restricted_candidate_bounds
./build_gpp/test_weighted_restricted_candidate_bounds

make build_gpp/test_weighted_restricted_bound_counterexamples
./build_gpp/test_weighted_restricted_bound_counterexamples

make build_gpp/test_weighted_restricted_bounds_cplex
./build_gpp/test_weighted_restricted_bounds_cplex

make build_gpp/test_weighted_fpp_restricted_branch_benders_cplex
./build_gpp/test_weighted_fpp_restricted_branch_benders_cplex

make cplex
make test

git diff --check
git diff --cached --check
```

## Smoke Commands

Exact weighted smoke with candidate bounds disabled in the permanent-pruning
sense uses the current exact restricted path:

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --run-id phase5c2b2_restricted_expected_heterogeneous \
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
  --output-json results/weighted_phase5c2b2_smoke/restricted_expected_heterogeneous.json
```

Rejected unvalidated bound example:

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --run-id phase5c2b2_reject_llbi_heterogeneous \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --initial-candidate-list 1,2,3,4 \
  --use-lifted-lower-bounds
```

Expected failure: non-homogeneous weighted restricted Branch-and-Benders rejects
unvalidated permanent-pruning/strengthening modules.

## Recommendation

Keep weighted permanent candidate pruning disabled until a proof is available
for a specific bound under expected, CVaR, and mean-CVaR weighted objectives.
The current exact solver is safe because every eligible candidate remains
available and exactness is certified only after full activation.

## Acceptance Status

Phase 5C2B2 is complete when the validation commands pass and smoke runs show:

- `candidates_permanently_pruned = 0`;
- `early_exactness_certificate_used = false`;
- `full_activation_reached = true` for exact weighted runs;
- objectives match direct FPP-SAA and unrestricted callback references.
