# Weighted Landscapes Phase 7A: DPV Audit And Shared Scoring

Phase 7A adds shared weighted DPV precomputation and scoring infrastructure
only. It does not change any production heuristic, optimization solver, manifest,
batch launcher, visualization script, or paired reburn workflow.

The central rule implemented here is destination weighting:

```text
WDPV_s(i) = sum_{k in valued_set_s(i)} w_k
```

The candidate weight `w_i` is used only when the legacy valued set explicitly
contains candidate `i`. No shortcut of the form `w_i * |D_s(i)|` is used.

## Files Added

- `include/opt/WeightedDpvScoring.hpp`
- `src/opt/WeightedDpvScoring.cpp`
- `tests/test_weighted_fpp_dpv.cpp`
- `tests/test_weighted_fpp_dpv_variants.cpp`
- `tests/test_weighted_fpp_dpv_cache.cpp`
- `tests/test_weighted_fpp_dpv_diagnostics.cpp`

## DPV Structural Terminology

- Directed descendant set: nodes reachable by a directed path from candidate
  `i`, including `i` when the legacy structure uses closed reachability.
- Guaranteed protected set: nodes for which every ignition-to-node path passes
  through `i`. The existing `DpvIndexBuilder` does not compute this set.
- Singleton protected set: nodes that do not burn when only `i` is selected.
- Currently protectable set: dynamic marginal set under a partial firebreak
  solution.

The repository currently has several DPV labels that use different structural
sets. Phase 7A keeps those definitions separate.

## Audited DPV Variants

### `DpvIndexBuilder`

- File/class/function:
  `include/opt/DpvIndexBuilder.hpp`,
  `src/opt/DpvIndexBuilder.cpp`,
  `DpvIndexBuilder::build_for_scenario`.
- Used by: Static-DPV, Static-DPV-MIP, DPV-SAA, DPV-Benders,
  DPV-Branch-Benders, DPV LLBI.
- Structural set: closed directed descendants from each compact source node.
- Includes candidate itself: yes.
- Includes non-eligible cells: yes, because node sets are built for all mapped
  nodes.
- Candidates absent from a scenario: not represented as absent; all mapped nodes
  receive a node set.
- Multiple paths/cycles: BFS with a visited vector, sorted output, no duplicate
  descendants.
- Cache behavior before Phase 7A: structural data stored directly in each
  `OptimizationScenario::dpv`, weight independent.
- Unit-weight assumption: all downstream membership and product pairs are
  unweighted.
- Intended weighted formula in shared infrastructure:
  `sum_{k in Desc_s(i)} w_k`, with an explicit ignition policy.

### `StaticDpvBenchmark`

- File/class/function:
  `include/benchmarks/StaticDpvBenchmark.hpp`,
  `src/benchmarks/StaticDpvBenchmark.cpp`,
  `StaticDpvBenchmark::run`.
- CLI/method label: `Static-DPV`.
- Structural set: closed directed descendants from `DpvIndexBuilder`.
- Formula:
  `sum_s rho_s * out_degree_s(i) * |Desc_s(i)|`.
- Includes candidate itself: yes.
- Includes non-eligible valued cells: yes, descendants are not filtered by
  eligibility.
- Ignition: legacy benchmark does not special-case ignition; if eligible, it can
  score its reachable closure.
- Scenario aggregation: probability-weighted sum.
- Normalization: none.
- Burn frequency: no, only per-scenario out-degree.
- Dynamic dependence: static.
- Tie-breaking: descending score, then lower original Cell2Fire ID.
- Intended weighted formula:
  `sum_s rho_s * out_degree_s(i) * sum_{k in Desc_s(i)} w_k`.

### `StaticDpvMipBenchmark`

- File/class/function:
  `include/benchmarks/StaticDpvMipBenchmark.hpp`,
  `src/benchmarks/StaticDpvMipBenchmark.cpp`,
  `StaticDpvMipBenchmark::run`.
- CLI/method label: Static-DPV-MIP benchmark path when used.
- Structural set: closed directed descendants from `DpvIndexBuilder`.
- Formula:
  `sum_s rho_s * sum_{k in Desc_s(i)} downstream_value_k`.
- Includes candidate itself: yes.
- Includes non-eligible valued cells: yes.
- Ignition: legacy benchmark does not special-case ignition.
- Scenario aggregation: probability-weighted sum.
- Normalization: none.
- Burn frequency: none.
- Dynamic dependence: static.
- Tie-breaking: descending score, then lower original Cell2Fire ID.
- Existing weight hook: `downstream_values_by_compact_index`.
- Intended weighted formula:
  `sum_s rho_s * sum_{k in Desc_s(i)} w_k`.

### Greedy DPV2

- Files/classes/functions:
  `include/heuristics/GreedyHeuristic.hpp`,
  `src/heuristics/GreedyHeuristic.cpp`,
  `include/heuristics/GreedyMetrics.hpp`,
  `src/heuristics/GreedyMetrics.cpp`,
  `CumulativePropagationGraph`.
- CLI/method label: `Greedy-DPV2`.
- Structural set: closed currently reachable nodes from a candidate in the
  active cumulative graph, excluding already selected blocked nodes.
- Formula:
  `active_outgoing_frequency_sum(i) * closed_reachable_count(i)`.
- Includes candidate itself: yes if active.
- Includes non-eligible cells: yes in reachable count.
- Ignition: current production heuristic has no special ignition exemption.
- Scenario aggregation: not by scenario probability; cumulative arc frequency
  counts how many training scenarios contain each arc.
- Normalization: none.
- Burn frequency: yes, outgoing arc occurrence counts.
- Dynamic dependence: yes, scores are updated after selected nodes become
  blocked.
- Tie-breaking: descending score, then lower original Cell2Fire ID.
- Intended weighted formula for a future integration:
  `active_outgoing_frequency_sum(i) * sum_{k reachable active from i} w_k`,
  with explicit handling for the ignition convention.

Phase 7A does not alter Greedy-DPV2 selection.

### Greedy DPV3

- Files/classes/functions:
  same heuristic files as Greedy-DPV2.
- CLI/method label: `Greedy-DPV3`.
- Structural set: active cumulative graph nodes reachable by shortest paths
  where arc cost is `1 / arc_frequency`.
- Formula:
  `active_outgoing_frequency_sum(i) * sum_{k reachable} 1/(1 + dist_inv(i,k))`.
- Includes candidate itself: yes, with distance zero.
- Includes non-eligible cells: yes.
- Ignition: current production heuristic has no special ignition exemption.
- Scenario aggregation: cumulative arc frequencies, not scenario probabilities.
- Normalization: inverse-distance attenuation, no landscape normalization.
- Burn frequency: yes, both outgoing multiplier and inverse arc costs.
- Dynamic dependence: yes.
- Tie-breaking: descending score, then lower original Cell2Fire ID.
- Intended weighted formula for a future integration:
  `active_outgoing_frequency_sum(i) * sum_{k reachable} w_k/(1 + dist_inv(i,k))`,
  with explicit handling for the ignition convention.

Phase 7A does not alter Greedy-DPV3 selection.

### DPV-SAA Direct Model

- File/class/function:
  `include/solver/DpvSaaCplexModel.hpp`,
  `src/solver/DpvSaaCplexModel.cpp`,
  `DpvSaaCplexModel::solve`.
- CLI/method label: `DPV-SAA`.
- Structural object: product pairs `(source, successor, descendant)` generated
  from every successor of `source` and every closed descendant of `source`.
- Objective:
  `sum_s rho_s * sum_{(i,j,k) in product_pairs_s} z_sijk`.
- Linearization: `z_sijk` models `x_sj * x_sk`.
- Includes candidate itself: via descendant sets when present in product pairs.
- Includes non-eligible cells: yes, `x` variables exist for all mapped nodes.
- Ignition: model enforces `x_ignition = 1`; selecting ignition does not block
  outgoing arcs because propagation uses `y_v` on incoming target `v`.
- Scenario aggregation: probability-weighted objective.
- Normalization: none.
- Burn frequency: no.
- Dynamic dependence: solution-dependent exact MIP recourse.
- Unit-weight assumption: every product pair has coefficient one.
- Intended weighted formula is not implemented in Phase 7A. It must not be
  collapsed into simple descendant sums because product-pair multiplicity is
  part of the legacy DPV-SAA objective.

### DPV Benders And Branch-And-Benders

- Files/classes/functions:
  `DpvBendersMaster`, `DpvBendersSolver`, `DpvScenarioSubproblem`,
  `DpvPersistentScenarioSubproblemManager`, `DpvBranchBendersSolver`.
- CLI/method labels:
  `DPV-Benders`, `DPV-Branch-Benders`,
  `DPV-Branch-Benders-LLBI`, `DPV-Branch-Benders-RootCuts`,
  `DPV-Branch-Benders-LLBI-RootCuts`.
- Structural object: same product-pair objective as DPV-SAA.
- Scenario aggregation: master objective uses `sum_s rho_s eta_s`.
- Subproblem objective: unweighted sum of active product-pair variables for one
  scenario.
- Ignition: same propagation convention as DPV-SAA.
- Unit-weight assumption: product pairs are unit coefficients.
- Cache behavior: persistent subproblem manager caches CPLEX LP models and
  updates fixed `y_copy` bounds.
- Intended weighted formula is deferred; product-pair coefficients need a
  variant-specific design.

Phase 7A does not modify these solvers.

### DPV Lifted Lower Bounds

- Files/classes/functions:
  `include/benders/DpvLiftedLowerBound.hpp`,
  `src/benders/DpvLiftedLowerBound.cpp`.
- Used by: optional DPV Benders and DPV Branch-and-Benders LLBI variants.
- Structural set: optimistic downstream singleton sets from closed downstream
  traversal, used to count active product pairs touching downstream nodes.
- Formula: unit decrease in product-pair loss; coefficients are negative counts.
- Includes candidate itself: downstream traversal includes source.
- Ignition: candidate equal to ignition receives coefficient zero.
- Includes non-eligible cells: yes as product-pair endpoints.
- Multiple paths/cycles: BFS visited set prevents duplicate downstream nodes;
  product stamps prevent duplicate product-pair credit.
- Scenario aggregation: per-scenario inequalities; master applies scenario
  probabilities through eta objective.
- Normalization: none.
- Intended weighted formula is deferred; product-pair coefficients are not the
  same as descendant-cell loss weights.

## Shared Phase 7A Infrastructure

`WeightedDpvScoring` provides two static variants:

- `StaticClosedDescendants`: values the closed descendant set once.
- `StaticClosedDescendantsTimesOutDegree`: values the closed descendant set and
  multiplies by candidate out-degree, matching legacy `Static-DPV`.

It also provides two ignition policies:

- `LegacyIncludeReachable`: preserves the legacy closed-descendant set for the
  ignition node, useful for homogeneous regression against existing code.
- `FppIgnitionNoProtection`: the default Phase 7A FPP-safe diagnostic policy;
  an ignition candidate has an empty valued set because ignition loss is
  unavoidable and selecting ignition must not claim downstream protection.

The default FPP policy intentionally does not change current production
heuristic behavior. It is available for Phase 7B integration.

## Cache Design

Structural data is independent of weights and contains:

- variant label;
- ignition policy;
- structural-definition string;
- candidate universe hash;
- scenario graph hash;
- implementation version;
- digest.

Numerical cache metadata contains:

- structural digest;
- weight profile;
- weight-map hash;
- scenario-probability hash;
- normalization mode;
- implementation version;
- digest.

The evaluator rejects mismatched variant, ignition policy, scenario graph,
scenario count/order, scenario probabilities, and candidate universe. Changing
only the weight vector keeps the structural digest unchanged and changes the
numerical digest.

## Diagnostics

The standalone report exposes:

```text
dpv_variant
dpv_weighted
dpv_structural_definition
dpv_weight_profile
dpv_weight_map_hash
dpv_scenarios
dpv_candidates
dpv_structural_sets_computed
dpv_structural_cache_hit
dpv_weighted_cache_hit
dpv_total_valued_incidence
dpv_score_min
dpv_score_max
dpv_score_mean
dpv_zero_score_candidates
dpv_precompute_time_sec
dpv_weighted_evaluation_time_sec
```

No experiment-result schema was expanded in Phase 7A.

## Validation Coverage

The new tests cover:

- single-chain destination weighting;
- candidate-versus-descendant weight sensitivity;
- branching descendants;
- reconverging paths;
- cycles and duplicate prevention;
- FPP ignition convention with high ignition weight;
- non-eligible high-value descendants;
- unequal scenario probabilities applied exactly once;
- homogeneous regression against `StaticDpvBenchmark` and
  `StaticDpvMipBenchmark`;
- ranking reversal under heterogeneous weights;
- structural invariance across weight maps;
- numerical cache invalidation across weight maps;
- cache mismatch failure;
- missing/invalid/nonfinite weight failure;
- deterministic rank ordering;
- fixed-seed randomized structural validation against an independent BFS
  reference.

## Exact Evaluator Cross-Checks

No Phase 7A weighted DPV structural score is asserted to equal
`Q_s(empty) - Q_s({i})` in general. The audited static variants are downstream
reachability proxies, not guaranteed-protection or exact singleton-protected
sets. DPV-SAA product-pair loss is also distinct from descendant-cell loss.

The one exact convention enforced here is ignition: the FPP diagnostic policy
assigns no avoidable downstream value to selecting/scoring the ignition
candidate.

## Deferred Work

Phase 7B or later should decide how to integrate weighted scores into:

- Static-DPV production selection;
- Greedy-DPV2 and Greedy-DPV3 dynamic loops;
- DPV-SAA product-pair objective coefficients;
- DPV Benders subproblem objectives and cut coefficients;
- DPV LLBI coefficients;
- restricted-candidate activation;
- manifests, batch scripts, and result schemas.

Those paths were intentionally not modified in Phase 7A.
