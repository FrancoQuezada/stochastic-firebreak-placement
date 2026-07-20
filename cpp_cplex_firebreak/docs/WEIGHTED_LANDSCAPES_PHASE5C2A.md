# Weighted Landscapes Phase 5C2A: Candidate Scoring And Activation Ordering

Phase 5C2A converts optional restricted-candidate scoring and activation ordering
for non-uniform cell weights. It does not change the FPP objective, recourse
definition, lazy-cut separation, candidate bounds, maintenance/deactivation,
combinatorial Benders, LLBI/projected LLBI, dominance preprocessing, DPV,
Static-DPV, or standalone heuristics.

## Files Modified

- `include/benders/BurnFrequencyCandidateScorer.hpp`
- `src/benders/BurnFrequencyCandidateScorer.cpp`
- `include/benders/BendersCoefficientCandidateScorer.hpp`
- `src/benders/BendersCoefficientCandidateScorer.cpp`
- `include/benders/CvarTailAwareBendersCandidateScorer.hpp`
- `src/benders/CvarTailAwareBendersCandidateScorer.cpp`
- `include/benders/FppRestrictedCandidateBranchBendersSolver.hpp`
- `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`
- `include/io/ExperimentResultWriter.hpp`
- `src/io/ExperimentResultWriter.cpp`
- `src/experiments/FppRestrictedCandidateBranchBendersOutOfSampleRunner.cpp`
- `Makefile`

## Files Created

- `tests/test_weighted_burn_frequency_candidate_scorer.cpp`
- `tests/test_weighted_benders_coefficient_candidate_scorer.cpp`
- `tests/test_weighted_cvar_tail_aware_candidate_scorer.cpp`
- `docs/WEIGHTED_LANDSCAPES_PHASE5C2A.md`

## Scorer Audit

### Burn-Frequency Scorer

Legacy input:

- `OptimizationInstance`;
- no-firebreak reachability in each training scenario;
- scenario probability if usable, otherwise unit scenario weights;
- eligible candidate compact indices.

Legacy formula:

```text
score_i = sum_s rho_s * 1{i is reachable in scenario s without firebreaks}
```

Meaning:

- direct exposure of candidate cell `i`;
- structural reachability, not downstream protected value;
- used only for initial candidate selection and activation ordering;
- never used for pruning or bounds.

Weighted formula:

```text
score_i = w_i * sum_s rho_s * 1{i is reachable in scenario s without firebreaks}
```

This is a value-aware direct-exposure score. It intentionally does not switch the
semantics to downstream benefit. Homogeneous unit weights reproduce legacy scores
and ranks exactly.

### Benders-Coefficient Scorer

Legacy input:

- full-universe accumulated Benders cuts;
- eligible compact indices;
- candidate IDs to score;
- optional scenario probabilities.

Legacy formula:

```text
score_j = sum_cuts -rho_s * coefficient_{s,j}
```

where missing coefficients are zero. Negative cut coefficients give positive
activation scores. Ties are broken by candidate ID.

Weighted formula:

```text
score_j = sum_weighted_cuts -rho_s * weighted_coefficient_{s,j}
```

The weighted LP subproblem already embeds `w_i` in its dual values, so the scorer
does not multiply by cell weights again. This avoids double weighting. The scorer
stores weight-map hash metadata in its summary; cut-pool hash guards prevent
mixing cuts from incompatible maps.

### CVaR-Tail-Aware Benders Scorer

Legacy input:

- accumulated Benders cuts;
- candidate IDs to score;
- scenario losses from the latest restricted stage;
- `cvar_beta`;
- blend parameter `gamma`;
- scenario probabilities.

Legacy formula:

```text
generic_score_j = BendersCoefficientScore_j(all cuts)
tail_score_j    = BendersCoefficientScore_j(cuts from empirical tail scenarios)
blend_j         = (1 - gamma) * normalize(generic_score_j)
                + gamma * normalize(tail_score_j)
```

Legacy tail membership sorts scenario losses descending and selects
`ceil((1-beta) * n)` worst scenarios, with deterministic tie-breaking by scenario
ID.

Weighted formula:

- scenario losses are the weighted losses from the Phase 5B/5C1 LP subproblems;
- tail membership is therefore based on weighted `L_s(y) = sum_i w_i x_i^s`;
- generic and tail scores use weighted Benders coefficients directly;
- no second multiplication by weights is applied.

The scorer is available only for `risk_measure=cvar` or `risk_measure=mean-cvar`
with `candidate_activation_policy=benders-coefficients`. It is rejected for
expected-value runs.

## Exactness Safeguards

Scoring affects only ordering:

- initial active set ordering for burn-frequency initialization;
- inactive candidate activation ordering for burn-frequency, Benders-coefficient,
  and CVaR-tail-aware activation;
- diagnostic top-score lists.

Scoring does not:

- remove candidates permanently;
- change eligibility;
- change recourse semantics;
- change lazy-cut validity;
- create pruning bounds;
- certify optimality before full activation.

Exactness still comes from eventual full activation and an optimal final solve.
Low-scoring candidates remain eligible and are activated by the final full
fallback if they were not selected by score-guided rounds.

## Hash And Cache Design

The scorers do not maintain persistent caches. Each score computation is built
from the current `OptimizationInstance`, current cut pool, current scenario-loss
vector, and risk config.

Hash handling:

- burn-frequency detailed scores carry the active weight-map hash;
- Benders-coefficient summaries carry the active weight-map hash;
- CVaR-tail-aware summaries carry the active weight-map hash;
- the restricted cut pool rejects reuse across incompatible hashes;
- OOS results report `candidate_score_map_hash`.

Because there are no long-lived scorer caches, cache invalidation is not needed
beyond the existing cut-pool hash guard.

## Supported Non-Homogeneous Scoring

For non-homogeneous maps, Phase 5C2A supports exact mode only:

- `initial_candidate_policy=burn-frequency`;
- `activation_policy=burn-frequency`;
- `activation_policy=benders-coefficients`;
- `candidate_score_mode=generic`;
- `candidate_score_mode=cvar-tail-blend` with Benders-coefficient activation and
  CVaR or mean-CVaR risk.

All supported scorer-guided runs must use:

```text
eventually_activate_all=true
restricted_heuristic_mode=false
candidate_maintenance_policy=none
```

## Rejected Modules

For non-homogeneous maps, Phase 5C2A continues to reject:

- candidate maintenance and deactivation;
- explicit tail-score diagnostic export;
- custom candidate-bound logic beyond existing activation upper bounds;
- combinatorial Benders;
- standard LLBI;
- CoverageLLBI;
- PathLLBI;
- projected LLBI variants;
- global dominance preprocessing;
- conditional zero-benefit fixing;
- DPV and Static-DPV paths.

## Result Diagnostics

The restricted result JSON now includes:

- `candidate_scorer`;
- `candidate_scorer_weighted`;
- `candidate_score_map_hash`;
- `initial_candidate_ids`;
- `initial_candidate_scores`;
- `score_recomputations`;
- `candidates_activated_by_score`;
- `candidates_activated_by_full_fallback`.

Existing restricted diagnostics remain unchanged, including initial/final active
counts, round logs, full activation, cut-pool size, and score top-k fields.

## Tests

New tests:

- `test_weighted_burn_frequency_candidate_scorer`
  - validates weighted direct-exposure formula;
  - validates homogeneous regression;
  - validates deterministic activation order.
- `test_weighted_benders_coefficient_candidate_scorer`
  - validates sign convention and probability aggregation;
  - validates no double weighting;
  - validates hash metadata;
  - validates weighted ranking can differ from homogeneous ranking.
- `test_weighted_cvar_tail_aware_candidate_scorer`
  - validates tail membership from weighted losses;
  - validates hash metadata;
  - validates homogeneous normalized-rank regression.
- `test_weighted_restricted_candidate_scoring_cplex`
  - validates weighted burn-frequency initialization and activation;
  - validates weighted Benders-coefficient activation;
  - validates weighted CVaR-tail-aware activation for CVaR and mean-CVaR;
  - validates misleading/small active set still reaches unrestricted optimum
    after final full activation;
  - validates unsupported Phase 5C2B options still fail.

## Validation Commands

Executed:

```bash
make build_gpp/test_benders_coefficient_candidate_scorer
./build_gpp/test_benders_coefficient_candidate_scorer

make build_gpp/test_burn_frequency_candidate_scorer
./build_gpp/test_burn_frequency_candidate_scorer

make build_gpp/test_cvar_tail_aware_benders_candidate_scorer
./build_gpp/test_cvar_tail_aware_benders_candidate_scorer

make build_gpp/test_weighted_benders_coefficient_candidate_scorer
./build_gpp/test_weighted_benders_coefficient_candidate_scorer

make build_gpp/test_weighted_burn_frequency_candidate_scorer
./build_gpp/test_weighted_burn_frequency_candidate_scorer

make build_gpp/test_weighted_cvar_tail_aware_candidate_scorer
./build_gpp/test_weighted_cvar_tail_aware_candidate_scorer

make build_gpp/test_weighted_restricted_candidate_scoring_cplex
./build_gpp/test_weighted_restricted_candidate_scoring_cplex

make build_gpp/test_weighted_fpp_restricted_branch_benders_cplex
./build_gpp/test_weighted_fpp_restricted_branch_benders_cplex

make cplex
make test
```

Observed results:

- all scorer unit tests passed;
- all weighted scorer unit tests passed;
- CPLEX-backed weighted restricted scoring tests passed;
- weighted restricted Branch-and-Benders tests passed;
- `make cplex` passed;
- `make test` passed. Non-CPLEX tests that require CPLEX still skip solve checks
  by design.

## Smoke Runs

Smoke split:

```text
landscape=Sub20
train_ids=1,2
test_ids=3,4
alpha=0.01
```

Weights:

- `results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv`
- `results/weighted_phase4_smoke/weights/sub20_clustered.csv`

Summary:

| run | scorer | risk | objective | ref diff | score activations | full fallback | final active |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| heterogeneous baseline | none | expected | 40.80802236 | 0 | 0 | 356 | 360 |
| heterogeneous Benders-coeff | weighted-benders-coefficients | expected | 40.80802236 | 0 | 10 | 346 | 360 |
| heterogeneous burn init | weighted-burn-frequency | expected | 40.80802236 | 0 | 0 | 356 | 360 |
| heterogeneous burn activation | weighted-burn-frequency | expected | 40.80802236 | 0 | 10 | 346 | 360 |
| clustered Benders-coeff | weighted-benders-coefficients | expected | 36.61795220 | 0 | 10 | 346 | 360 |
| clustered burn activation | weighted-burn-frequency | expected | 36.61795220 | 0 | 10 | 346 | 360 |
| heterogeneous tail-aware | weighted-cvar-tail-blend | cvar | 49.35425644 | 0 | 10 | 346 | 360 |
| clustered tail-aware | weighted-cvar-tail-blend | mean-cvar | 44.90617215 | 0 | 10 | 346 | 360 |
| heterogeneous small initial | weighted-benders-coefficients | expected | 40.80802236 | 0 | 1 | 355 | 360 |

All smoke objectives matched direct FPP-SAA and unrestricted callback
Branch-and-Benders references with absolute difference `0.0`. Weighted evaluator
validation difference was `0.0` in each restricted smoke.

## Example Smoke Command

```bash
./build_gpp/firebreak_cpp run-fpp-restricted-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4 \
  --alpha 0.01 \
  --run-id phase5c2a_benders_heterogeneous_expected \
  --time-limit 30 \
  --mip-gap 0 \
  --threads 1 \
  --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --initial-candidate-policy explicit-list \
  --initial-candidate-list 1,2,3,4 \
  --candidate-activation-policy benders-coefficients \
  --candidate-activation-batch-size 10 \
  --max-candidate-rounds 1 \
  --eventually-activate-all \
  --restricted-exact-mode \
  --output-json results/weighted_phase5c2a_smoke/phase5c2a_benders_heterogeneous_expected.json \
  --output-csv results/weighted_phase5c2a_smoke/restricted.csv
```

## Limitations And Deferred Work

Deferred to later phases:

- weighted candidate maintenance/deactivation;
- weighted custom candidate bounds;
- weighted cut-pool prioritization or eviction;
- weighted exported candidate-score CSV;
- weighted tail-score diagnostics as an explicit export mode;
- weighted combinatorial Benders;
- weighted LLBI/projected LLBI interactions;
- weighted dominance preprocessing;
- DPV and Static-DPV changes;
- standalone greedy heuristic changes.

## Acceptance Status

Phase 5C2A criteria are satisfied:

- enabled scorers have explicit weighted definitions;
- Benders coefficients are not double weighted;
- tail-aware scoring uses weighted scenario losses;
- burn-frequency weighting follows documented direct-exposure semantics;
- no persistent scorer caches are reused across hashes;
- initial candidate selection and activation ordering remain deterministic;
- low-scoring candidates remain eligible for final full activation;
- scorer-guided restricted solutions match unrestricted weighted references;
- homogeneous scorer behavior is preserved;
- CPLEX-backed tests pass;
- no Phase 5C2B maintenance, bounds, cut-pool prioritization, combinatorial,
  LLBI, DPV, or heuristic functionality was implemented.
