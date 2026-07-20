# Weighted Landscapes Phase 7B1: Production DPV Heuristics

Scope: integrate Phase 7A weighted DPV scoring into production heuristic methods only:
`Static-DPV`, `Static-DPV-MIP`, `Greedy-DPV2`, and `Greedy-DPV3`.

Out of scope for this phase: DPV-SAA, DPV-Benders, DPV Branch-and-Benders,
DPV LLBI, FPP SAA/Benders/combinatorial/restricted-candidate solvers,
batch manifest generation, paired reburn evaluation, analysis, and visualization.

## Common Weighted Semantics

All integrated methods score destination cells:

```text
WDPV_s(i) = sum_{k in D_s(i)} w_k
```

They do not use `w_i * |D_s(i)|`. Candidate filters and compact/original node
mapping are inherited from `OptimizationInstanceBuilder`. Missing or incomplete
weight maps fail through the Phase 1 loader. Omitted `--weight-map-file` means
homogeneous unit weights.

Production CLI defaults to:

```text
--dpv-ignition-policy fpp-safe
```

Under `fpp-safe`, an ignition candidate receives no downstream-protection credit.
The compatibility value `legacy` is still accepted explicitly for regression
comparisons.

Final reported in-sample and out-of-sample weighted losses are computed by
`FppRecourseEvaluator`; DPV scores remain surrogate selection objectives.

## Static-DPV Audit

- CLI/dispatcher label: `run-static-dpv-oos`, method `Static-DPV`.
- Runner: `StaticDpvOutOfSampleRunner`.
- Heuristic/model class: `benchmarks::StaticDpvBenchmark`.
- Previous score builder: local loop over `scenario.dpv.node_sets`.
- Structural definition: closed directed descendants from `DpvIndexBuilder`,
  multiplied by candidate out-degree.
- Static/recomputed: computed once from training scenarios.
- Scenario aggregation: probability-weighted sum.
- Normalization: none.
- Tie-breaking: larger score, then smaller original Cell2Fire node ID.
- Ignition handling: production default `fpp-safe`; legacy only by option.
- Candidate eligibility: `opt.eligible_indices`.
- Budget handling: top `opt.budget` candidates.
- Stopping rule: stop when budget is filled.
- Output fields: standard experiment fields plus `dpv_*` diagnostics.
- In-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Out-of-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Unit-weight assumption removed: descendant count is replaced by destination
  weight sum.
- Phase 7A integration: `WeightedDpvVariant::StaticClosedDescendantsTimesOutDegree`.

## Static-DPV-MIP Audit

- CLI/dispatcher label: `run-static-dpv-mip-oos`, method `Static-DPV-MIP`.
- Runner: `StaticDpvOutOfSampleRunner`.
- Heuristic/model class: `benchmarks::StaticDpvMipBenchmark`.
- Previous score builder: local closed-descendant count/value loop.
- Structural definition: closed directed descendants from `DpvIndexBuilder`,
  without out-degree multiplier.
- Static/recomputed: computed once from training scenarios.
- Scenario aggregation: probability-weighted sum.
- Normalization: none.
- Tie-breaking: deterministic top-budget sort.
- Ignition handling: production default `fpp-safe`; legacy only by option.
- Candidate eligibility: `opt.eligible_indices`.
- Budget handling: top `opt.budget` candidates under existing feasibility checks.
- Stopping rule: stop when budget is filled.
- Output fields: standard experiment fields plus `dpv_*` diagnostics.
- In-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Out-of-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Unit-weight assumption removed: MIP surrogate coefficients now use destination
  weight sums.
- Phase 7A integration: `WeightedDpvVariant::StaticClosedDescendants`.

## Greedy-DPV2 Audit

- CLI/dispatcher label: `run-greedy-oos --metric DPV2`, method `Greedy-DPV2`.
- Runner: `GreedyOutOfSampleRunner`.
- Heuristic/model class: `GreedyHeuristic` with `LowMemoryGreedyScorer`.
- Previous score builder: active cumulative propagation graph.
- Structural definition: active outgoing frequency sum times closed reachable
  destination set on the active cumulative graph.
- Static/recomputed: initial scores plus exact recomputation for candidates that
  can reach the newly blocked selected node.
- Scenario aggregation: cumulative arc frequencies over training scenarios.
- Normalization: none.
- Tie-breaking: larger score, then smaller original Cell2Fire node ID.
- Ignition handling: production default `fpp-safe`; any training ignition
  candidate receives zero DPV2 credit.
- Candidate eligibility: `opt.eligible_indices`.
- Budget handling: greedy selection until `opt.budget`.
- Stopping rule: budget filled or no eligible candidate remains.
- Output fields: standard experiment fields plus `dpv_*` diagnostics and greedy
  recomputation counters.
- In-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Out-of-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Unit-weight assumption removed: closed reachable count is replaced by closed
  reachable destination weight sum.
- Phase 7A integration: uses Phase 7A canonical compact weights and policy names;
  dynamic cumulative structure is preserved rather than replaced by a static score.

## Greedy-DPV3 Audit

- CLI/dispatcher label: `run-greedy-oos --metric DPV3`, method `Greedy-DPV3`.
- Runner: `GreedyOutOfSampleRunner`.
- Heuristic/model class: `GreedyHeuristic` with `LowMemoryGreedyScorer`.
- Previous score builder: active cumulative propagation graph with inverse
  frequency-distance downstream value.
- Structural definition: active outgoing frequency sum times weighted
  inverse-distance destination sum using arc cost `1 / frequency`.
- Static/recomputed: initial scores plus exact recomputation for candidates that
  can reach the newly blocked selected node.
- Scenario aggregation: cumulative arc frequencies over training scenarios.
- Normalization: none.
- Tie-breaking: larger score, then smaller original Cell2Fire node ID.
- Ignition handling: production default `fpp-safe`; any training ignition
  candidate receives zero DPV3 credit.
- Candidate eligibility: `opt.eligible_indices`.
- Budget handling: greedy selection until `opt.budget`.
- Stopping rule: budget filled or no eligible candidate remains.
- Output fields: standard experiment fields plus `dpv_*` diagnostics and greedy
  recomputation counters.
- In-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Out-of-sample evaluation: burned area plus weighted `FppRecourseEvaluator`.
- Unit-weight assumption removed: each reachable destination contributes
  `w_k / (1 + dist(i,k))` instead of `1 / (1 + dist(i,k))`.
- Phase 7A integration: uses Phase 7A canonical compact weights and policy names;
  dynamic cumulative structure is preserved.

## Implementation Notes

- Added CLI options `--weight-map-file` and `--dpv-ignition-policy` to the three
  production OOS commands in scope.
- Low-level benchmark defaults remain legacy-compatible; production runners pass
  the FPP-safe policy explicitly by default.
- Static methods use Phase 7A deterministic ranking directly.
- Greedy-DPV2 and Greedy-DPV3 keep their existing active-graph recomputation and
  overlap/blocking behavior, but replace unit destination contribution with
  canonical compact cell weights.
- Test-set weighted evaluation remaps selected original nodes into the test
  `OptimizationInstance` compact mapper before calling `FppRecourseEvaluator`.
