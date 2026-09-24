# Weighted Landscapes Phase 5C2B1: Restricted Maintenance Audit

Branch: `feature/instance-generation-updates`

Scope: weighted candidate maintenance, temporary deactivation/reactivation,
restricted cut-pool identity/diagnostics, and CVaR-tail diagnostics for the
restricted-candidate FPP Branch-and-Benders solver.

This phase does not change the direct FPP-SAA model, explicit-loop Benders,
unrestricted callback Branch-and-Benders, subproblem semantics, candidate
eligibility, or eventual full activation. It does not implement permanent
candidate pruning, candidate bounds, combinatorial Benders conversion, LLBI,
projected LLBI, dominance, conditional fixing, DPV, Static-DPV, or standalone
heuristic changes.

## Components Audited

### `RestrictedCandidateMaintenanceTracker`

Current inputs:

- active/inactive state from `RestrictedCandidateManager`;
- Benders-coefficient or CVaR-tail blend scores supplied by the restricted
  solver;
- selected incumbent candidates mapped back to candidate IDs;
- newly activated candidates;
- optional tail-protected candidates;
- min/max active size, deactivation batch size, min active age, cooldown, and
  selected-candidate protection options.

Legacy formula:

- activation scores are sorted descending outside the tracker;
- candidates blocked by cooldown are removed from activation consideration;
- deactivation candidates are active candidates not protected by incumbent,
  newly activated status, min age, or tail protection;
- deactivation order is ascending maintenance score, then candidate ID.

It does not use burned-cell counts directly. It uses Benders coefficients only
through externally supplied scores. It can deactivate candidates temporarily via
the manager. It cannot permanently remove candidates. It does not discard cut
information.

Weighted formula:

- structural counters are unchanged;
- weighted Benders scores are consumed directly from weighted cuts;
- CVaR-tail blend scores use weighted scenario losses and weighted cut
  coefficients;
- scores are not multiplied by cell weights again;
- tie-breaking remains deterministic by candidate ID.

Phase 5C2B1 adds `weight_map_hash` to tracker state and maintenance decisions.
Reuse under another hash is rejected. If score data is missing, absent scores
default to zero and deactivation remains bounded by all safety protections.

### `RestrictedCandidateManager`

Current inputs:

- candidate count;
- firebreak budget;
- initial active candidate IDs;
- activation/deactivation requests.

Legacy formula:

- active candidates have upper bound 1 in the stage master;
- inactive candidates have upper bound 0;
- deactivation is rejected if it would reduce the active set below the budget;
- `activateAll()` restores the full candidate universe.

Weighted generalization:

- no formula change; this is structural state only;
- no weights are loaded or generated;
- original candidate ID and compact index are preserved by the surrounding
  solver data.

The manager can temporarily deactivate candidates. It never changes global
candidate eligibility and cannot permanently prune.

### Restricted Solver Maintenance Path

Current trigger:

- maintenance runs after the initial restricted solve when
  `candidate_maintenance_policy=benders-coefficients`,
  `activation_policy=benders-coefficients`, `max_candidate_rounds > 0`, and
  `activation_batch_size > 0`;
- each maintenance round scores inactive candidates, activates top candidates,
  then deactivates bottom active candidates only when the active set exceeds the
  configured maximum;
- the master is rebuilt between stages, not mutated concurrently inside a
  callback.

Legacy restriction:

- maintenance was heuristic-mode only and did not claim exactness before full
  activation.

Phase 5C2B1 exact-mode rule:

- maintenance is also allowed in exact mode when `eventually_activate_all=true`;
- non-homogeneous weighted maintenance is allowed only in exact mode with
  eventual full activation;
- non-homogeneous weighted heuristic maintenance remains rejected;
- global optimality is certified only after `activateAll()` and an optimal final
  full stage.

Safety guards:

- active count never drops below the firebreak budget;
- selected incumbents are protected when `protect_selected_candidates=true`;
- newly activated candidates are protected in the same maintenance round;
- tail-protected candidates are protected when tail-aware scoring is active;
- min age and cooldown reduce oscillation;
- deactivation only changes stage upper bounds;
- persistent scenario subproblems and stored full-space cuts are not modified;
- full activation overrides all temporary deactivation before exactness is
  claimed.

### `RestrictedCandidateCutPool`

Current inputs:

- `BendersCut` with scenario ID, RHS constant, and full-space coefficients by
  compact node index;
- round/stage metadata;
- active candidate count;
- weight-map hash set by the solver.

Legacy identity:

- duplicate cuts required same scenario ID, same RHS constant, and same sorted
  coefficient vector within tolerance.

Weighted identity:

- duplicate cuts additionally require the same `weight_map_hash`;
- weighted coefficients and RHS are compared directly;
- support-only deduplication is not used;
- homogeneous and heterogeneous cuts cannot be merged through the same pool
  hash;
- records store the generating `weight_map_hash`.

The current cut pool has no eviction mechanism. Phase 5C2B1 keeps this
conservative policy for weighted runs:

- `cut_pool_evictions = 0`;
- `cut_pool_reinstantiations = 0`;
- `cut_pool_peak_size` is tracked;
- all exact lazy-cut records needed for later activation remain available.

Cut reinstantiation:

- the master is rebuilt for each stage;
- pooled cuts are inserted into each rebuilt master;
- each cut stores full-universe coefficients, including currently inactive
  candidates;
- when a candidate is activated later, its coefficient is already present in the
  stored cut and is materialized during cut insertion.

### CVaR-Tail Diagnostics

Current inputs:

- scenario probabilities;
- accumulated and recent Benders cuts;
- stage scenario losses by scenario ID;
- stage CVaR excess values;
- active/activated/deactivated/selected/protected candidate sets.

Legacy formula:

- empirical tail scenarios are the highest-loss scenarios by
  `scenario_losses_by_id`, with ties by scenario ID;
- excess-tail scenarios have positive CVaR excess above tolerance;
- generic and tail candidate scores are Benders-coefficient scores over the
  corresponding cut subsets.

Weighted formula:

- `scenario_losses_by_id` comes from weighted stage objectives
  `sum_i w_i x_i^s`;
- tail membership is determined from weighted losses, not burned-cell counts;
- Benders coefficients are already weighted and are consumed directly;
- no extra multiplication by weights is applied;
- mean-CVaR uses the same CVaR-component tail definition.

Phase 5C2B1 diagnostics now record:

- `weighted`;
- `weight_profile`;
- `weight_map_hash`;
- per-scenario probability;
- weighted loss;
- weighted VaR threshold;
- tail membership;
- tail excess.

For non-homogeneous maps, explicit tail diagnostic export is rejected for
expected-value runs because no CVaR tail is defined. CVaR-tail-aware activation
continues to reject expected-value risk.

## Result Fields Added

Solver result:

- `maintenance_weighted`;
- `maintenance_map_hash`;
- `active_candidate_target`;
- `candidates_considered_for_deactivation`;
- `candidates_deactivated`;
- `candidates_reactivated`;
- `candidates_protected_from_deactivation`;
- `full_activation_overrode_maintenance`;
- `cut_pool_peak_size`;
- `cut_pool_evictions`;
- `cut_pool_reinstantiations`.

JSON result block:

- `maintenance_weighted`;
- `maintenance_map_hash`;
- `active_candidate_target`;
- `candidates_considered_for_deactivation`;
- `candidates_deactivated`;
- `candidates_reactivated`;
- `candidates_protected_from_deactivation`;
- `full_activation_overrode_maintenance`;
- `cut_pool_peak_size`;
- `cut_pool_evictions`;
- `cut_pool_reinstantiations`;
- weighted CVaR-tail per-scenario diagnostics.

The broad CSV schema was not redesigned.

## Threading Assumptions

Maintenance occurs between complete stage solves. It does not mutate active
candidates from inside the lazy callback. The CPLEX callback continues to own
cut separation within a single rebuilt stage. Weight vectors and hashes are
immutable during a solve. Diagnostic JSON is written after solve completion,
not from high-frequency callback code. Existing one-solve-per-run assumptions
are preserved.

## Tests Added

- `test_weighted_restricted_candidate_maintenance`;
- `test_weighted_restricted_cut_pool_prioritization`;
- `test_weighted_restricted_cut_reactivation`;
- `test_weighted_restricted_tail_diagnostics`;
- `test_weighted_restricted_maintenance_cplex`.

Existing weighted restricted CPLEX tests were extended with:

- exact weighted maintenance with temporary deactivation and final full
  activation restoration;
- weighted CVaR-tail diagnostic export;
- continued rejection of non-homogeneous weighted heuristic maintenance;
- continued rejection of expected-value tail diagnostics for non-homogeneous
  maps.

## Validation Commands

```bash
make build_gpp/test_weighted_restricted_candidate_maintenance
./build_gpp/test_weighted_restricted_candidate_maintenance

make build_gpp/test_weighted_restricted_cut_pool_prioritization
./build_gpp/test_weighted_restricted_cut_pool_prioritization

make build_gpp/test_weighted_restricted_cut_reactivation
./build_gpp/test_weighted_restricted_cut_reactivation

make build_gpp/test_weighted_restricted_tail_diagnostics
./build_gpp/test_weighted_restricted_tail_diagnostics

make build_gpp/test_weighted_restricted_maintenance_cplex
./build_gpp/test_weighted_restricted_maintenance_cplex

make build_gpp/test_weighted_fpp_restricted_branch_benders_cplex
./build_gpp/test_weighted_fpp_restricted_branch_benders_cplex

make cplex
make test

git diff --check
git diff --cached --check
```

## Smoke Commands

Sub20 smoke runs should compare restricted maintenance to direct FPP-SAA and
unrestricted callback references, using the same fixed split as Phase 5C2A.
Example pattern:

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --risk-measure expected \
  --weight-map results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --initial-candidate-policy explicit-list \
  --initial-candidates 1,2,3,4 \
  --candidate-activation-policy benders-coefficients \
  --activation-batch-size 10 \
  --max-candidate-rounds 2 \
  --candidate-maintenance-policy benders-coefficients \
  --candidate-min-active-size 4 \
  --candidate-max-active-size 8 \
  --candidate-deactivation-batch-size 4 \
  --eventually-activate-all \
  --time-limit 30 \
  --threads 1 \
  --output-json results/weighted_phase5c2b1_smoke/maintenance_expected.json
```

For CVaR-tail diagnostics:

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --risk-measure cvar \
  --cvar-beta 0.5 \
  --weight-map results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --initial-candidate-policy explicit-list \
  --initial-candidates 1,2,3,4 \
  --candidate-activation-policy benders-coefficients \
  --activation-batch-size 10 \
  --max-candidate-rounds 2 \
  --candidate-maintenance-policy benders-coefficients \
  --candidate-score-mode cvar-tail-blend \
  --candidate-tail-score-gamma 0.5 \
  --export-tail-score-diagnostics \
  --eventually-activate-all \
  --time-limit 30 \
  --threads 1 \
  --output-json results/weighted_phase5c2b1_smoke/maintenance_cvar_tail.json
```

## Deferred Work

- no weighted candidate-bound pruning;
- no permanent candidate elimination;
- no cut eviction for correctness-critical records;
- no combinatorial, LLBI, projected LLBI, dominance, conditional fixing, DPV,
  Static-DPV, or standalone heuristic conversion;
- no persistent master branch-and-bound tree reuse.

## Acceptance Status

Phase 5C2B1 acceptance criteria are satisfied if the validation commands above
pass and smoke runs match exact weighted references. The implementation keeps
maintenance as an accelerator only: low score never proves a candidate
unnecessary, stored cuts retain full coefficients, and exactness is certified
only after final full activation.
