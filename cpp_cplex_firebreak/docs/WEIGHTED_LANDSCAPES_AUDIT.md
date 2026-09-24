# Weighted Landscapes Phase 0 Audit

Date: 2026-07-15

Branch: `feature/instance-generation-updates`

## Scope

This document audits the current unit-weight assumptions before implementing
non-uniform cell protection weights `w_i`.

No optimization model behavior was changed in this phase. The current code uses
unit cell loss for FPP and unit downstream cell values for most DPV variants.
The intended future scenario loss is:

```text
L_s(y) = sum_{i burns in scenario s under y} w_i
```

Physical burned-cell counts must remain available as secondary reporting
metrics.

## Baseline Architecture

The current canonical optimization data structure is
`include/opt/OptimizationInstance.hpp`. It contains compact/original cell
mapping, scenario arcs, ignition nodes, eligible firebreak nodes, scenario
probabilities, and DPV descendant sets. It does not contain a landscape weight
map or weight metadata.

The external Cell2Fire node IDs are preserved through `IndexMapper`. Any weight
implementation should store weights by original Cell2Fire ID at the loading
boundary, then expose compact-index access only after validation against
`OptimizationInstance::node_mapper`.

## Audit Table

| Component | Files / functions | Current unit-weight behavior | Required weighted behavior | Change type / validity | Tests needed | Structurally unweighted? |
| --- | --- | --- | --- | --- | --- | --- |
| Canonical instance data | `include/opt/OptimizationInstance.hpp`, `src/opt/OptimizationInstanceBuilder.cpp` | No `w_i` representation. Budget remains `floor(alpha * NCells)`. | Add a canonical landscape weight map and compact weight vector. Validate complete, finite, strictly positive weights for every mapped/evaluable cell. Preserve default homogeneous `w_i=1`. | Data plumbing; prerequisite for all model changes. | Homogeneous default equivalence; invalid/missing/duplicate/non-positive weight files fail clearly; original-to-compact mapping tests. | No. |
| Cell2Fire readers and layouts | `src/io/Cell2FireReader.cpp`, `include/io/Cell2FireReader.hpp` | Loads scenarios, ignitions, `n_cells`, metadata; no cell weights. | Load or attach a shared weight map after landscape validation. Paired folders such as `20x20` and `20x20_reburn` must use the same map. | Data plumbing with strong validation. | Weight file parser tests; paired-landscape hash equality; strict failure on incompatible maps. | No. |
| Legacy burned-area evaluator | `src/eval/BurnedAreaEvaluator.cpp` | Uses `burned_nodes.size()` and reports expected/worst/VaR/CVaR burned counts. | Keep physical burned-count evaluator, or add a separate weighted-loss evaluator. Do not overwrite physical metrics. | Reporting split; not an optimization validity issue. | Existing tests must continue; new weighted evaluator tests if introduced. | Yes, for physical burned-count reporting. |
| FPP recourse evaluator | `src/eval/FppRecourseEvaluator.cpp` | FPP convention is correct, but loss is `burned_count += 1.0`; note explicitly says unit burned area is used. | Accumulate `scenario_weighted_loss += w_i` for burned nodes while preserving `burned_count`. Ignition burns even if selected; non-root selected nodes block incoming propagation only. | Coefficient/evaluator change. | Homogeneous equivalence; nonuniform hand graph; ignition selected still burns; evaluator/model objective agreement. | Traversal is structural; loss accumulation is weighted. |
| FPP direct SAA model | `src/solver/FppSaaCplexModel.cpp` | Expected objective and CVaR scenario loss use `sum_i x_i^s`; CVaR upper bounds use `node_count`. | Replace each `x_i^s` loss coefficient with `w_i`; set CVaR bounds from total mapped/scenario weight. Result components should store weighted expected/CVaR loss. | Mostly coefficient-based. Model validity unchanged for positive weights. | Structure tests with unit weights; weighted objective and CVaR expression tests; solve/evaluator agreement. | Constraints remain structural. |
| FPP cut/reachability direct model | `src/solver/FppCutReachabilityCplexModel.cpp` | Uses scenario-local `x` variables with unit coefficients. MIP start objective comes from unit evaluator. | Use `w_i` for each local `x`; weighted evaluator for validation and warm-start objective. | Coefficient-based plus validation plumbing. | Weighted local-node objective tests; homogeneous equality with base FPP-SAA; weighted recourse validation. | `q` reachability constraints are structural. |
| FPP Benders subproblem | `src/benders/FppScenarioSubproblem.cpp` | Subproblem objective is `sum_i x_i`; dual cuts are built from that LP. | Objective becomes `sum_i w_i x_i`; cuts use duals from the weighted LP. | Coefficient-based if the subproblem owns the canonical weights. Benders validity follows from LP duality after coefficient change. | Weighted subproblem value/dual cut test; homogeneous cut equality; evaluator agreement. | Constraints remain structural. |
| FPP Benders master/risk | `src/benders/FppBendersMaster.cpp`, `src/benders/FppBendersSolver.cpp` | Master aggregates per-scenario `eta_s` by probability/CVaR; notes call recourse unweighted. | Keep master aggregation over `eta_s`, but interpret `eta_s` as weighted scenario loss. Notes and diagnostics must be renamed. | Risk aggregation is already generic over losses; no mathematical change beyond subproblem values. | Expected/CVaR/mean-CVaR weighted-loss tests; homogeneous old-result regression. | Risk utilities are structurally scenario-weighted, not cell-weighted. |
| FPP Branch-and-Benders | `src/benders/FppBranchBendersSolver.cpp` | Lazy/user cuts consume subproblem values and unit LLBI/combinatorial cuts. | Weighted subproblem manager values, weighted initial/root/lazy cuts, weighted diagnostics. | Mixed: LP Benders cuts are coefficient-based; strengthening cuts are validity-sensitive. | Weighted lazy-cut separation; root user cut regression; homogeneous equivalence. | Master y/eta structure remains unchanged. |
| Persistent subproblem manager | `src/benders/FppPersistentScenarioSubproblemManager.cpp` | Caches FPP subproblem models with unit objective coefficients. | Build/caches weighted objective coefficients once per scenario/map. | Coefficient-based with cache invalidation by weight-map identity. | Repeated solve weighted value tests; homogeneous equality; cache reuse with same hash. | Structural constraints unchanged. |
| Restricted-candidate Branch-and-Benders | `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`, candidate managers/scorers | Restricted stages and candidate activation depend on unit-loss Benders cuts, burn frequency, and scenario losses. | Use weighted Benders coefficients and weighted scenario losses for score modes where "benefit" or CVaR tail is loss-based. Keep purely structural policies documented. | Mixed. Exact restricted solves follow Benders; heuristic scoring semantics change. | Weighted candidate activation tests; tail-aware scoring with weighted losses; homogeneous regression. | Candidate-set mechanics are structural; score values are not. |
| FPP lifted lower-bound inequalities | `src/benders/FppLiftedLowerBound.cpp`, `include/benders/FppLiftedLowerBound.hpp` | `f_empty`, singleton losses, and coefficients count burned downstream nodes. | Replace counts with weighted burned loss and weighted removed downstream loss. Validate every coefficient/RHS against exhaustive tiny cases. | Validity-sensitive. Nonnegative weights should preserve set-inclusion lower-bound logic if all constants/coefficients are weighted consistently. | Exhaustive weighted LLBI validation on tiny DAGs/general directed graphs; homogeneous equality. | No. |
| Coverage LLBI | `include/benders/FppStrengthening.hpp`, branch-benders adders | `empty_burned_area = root_reachable.size()`. Each covered node contributes `1` through zeta. | Node contribution must be `w_i`; coverage zeta reductions and RHS use node weights. | Validity-sensitive but should hold for nonnegative weights as a weighted sum of per-node valid lower bounds. | Weighted coverage LLBI cut tests; projection tests; homogeneous equality. | Coverage sets are structural; contributions weighted. |
| Path LLBI | `include/benders/FppStrengthening.hpp`, branch-benders adders | Each node/path burn lower bound contributes `1`. | Each node lower-bound variable contributes `w_i`, or an equivalent scaled path inequality. | Validity-sensitive. Coefficient indexing must be checked carefully. | Weighted path LLBI validity tests on multi-path graphs; homogeneous equality. | Path enumeration is structural. |
| Projected coverage/path LLBI | `src/benders/FppProjectedLlbi.cpp` | Projected cuts use constants, saturated terms, `rhs`, and coefficients incremented by `1`. | Use node weights in constants, saturated terms, `rhs_at_ybar`, and cut coefficients. | Validity-sensitive; projection/separation formulas must be rederived with weights. | Weighted projected coverage/path cut violation tests; root-round tests; homogeneous equality. | Shortest path/search support is structural; cut values weighted. |
| Combinatorial Benders | `src/benders/FppCombinatorialBenders.cpp` | Active nodes, RHS, subproblem objective, path coefficient counts, initial cuts, greedy initial solution, and scenario losses are counts. | Active burned nodes must contribute `w_i`; path coefficients accumulate protected descendant weights, not counts. Greedy initial solution should reduce weighted expected loss. | Validity-sensitive, especially lifting and fractional separation. | Weighted integer/fractional cut tests; lifting fallback tests; greedy initial weighted-loss tests; homogeneous equality. | Path discovery is structural. |
| Dominance preprocessing | `include/benders/FppStrengthening.hpp` | Compares dominated-node sets by subset inclusion across scenarios; duplicate/equivalence by set signature. | For strictly positive weights, if candidate `i` protects a superset of candidate `j` in every scenario, `i` weakly dominates `j` under any nonnegative weights. Keep only if implementation remains set-inclusion based. | Validity preserved by set inclusion and positive weights. If future scoring uses cardinality, adapt to weighted dominated loss. | Weighted dominance proof tests; candidate-removal equivalence under nonuniform weights; homogeneous regression. | Set-inclusion dominance is structural. |
| Conditional zero-benefit fixing | `include/benders/FppStrengthening.hpp` | Candidate has zero benefit if unreachable after fixed selected nodes in all scenarios. | Remains valid for positive weights: unreachable candidate cannot block any additional burned weighted loss. | Structural validity preserved. | Weighted zero-benefit tests. | Yes. |
| Separator/dominator cuts | `src/cuts/*` | Build structural reachability/dominator/separator cuts over x/y variables. Some diagnostics count cuts/reachability. | Cut validity is structural; any ranking/reporting by protected loss should use weights if added. | Mostly structurally unweighted. | Homogeneous regression; weighted tests confirming cuts do not change objective incorrectly. | Yes for cut logic. |
| DPV index builder | `src/opt/DpvIndexBuilder.cpp`, `include/opt/OptimizationInstance.hpp` | Stores descendant node lists and product pairs; no values. | Preserve descendant lists but provide descendant weighted values or per-descendant coefficients. Avoid multiplying only by source-node weight. | Data/semantic change for DPV objective families. | Weighted descendant-sum tests; overlapping descendant multiplicity tests. | Descendant topology remains structural. |
| DPV-SAA direct model | `src/solver/DpvSaaCplexModel.cpp` | Objective is `sum_s p_s sum_{product pairs} z`, documented as unit weights. | Each product pair should be weighted by `w_descendant`, or compacted into equivalent weighted descendant contribution. | Coefficient-based once DPV semantics are defined; objective meaning changes. | Weighted DPV objective tests; source-vs-descendant weight guard test; homogeneous equality. | Propagation/linearization constraints remain structural. |
| DPV Benders and Branch-Benders | `src/benders/Dpv*` | DPV subproblems, LLBI, and callbacks use unit DPV pair/objective values. | Apply descendant weights consistently in DPV subproblems and LLBI. | Mixed; DPV LLBI validity must be checked separately. | Weighted DPV subproblem/cut/LLBI tests; homogeneous equality. | Graph topology remains structural. |
| Static DPV benchmark | `src/benchmarks/StaticDpvBenchmark.cpp` | Score is `out_degree * descendant_count`. | Score should use `out_degree * sum_{descendant} w_descendant` if retained as weighted static DPV. | Semantic heuristic change. | Weighted static DPV ordering test; homogeneous equality. | Out-degree is structural. |
| Static DPV MIP benchmark | `src/benchmarks/StaticDpvMipBenchmark.cpp` | Already has optional `downstream_values_by_compact_index`, default `1.0`; runners currently do not pass cell weights. | Pass canonical cell weights as downstream values. Default remains unit. | Mostly plumbing; implementation already supports weighted downstream values. | Runner plumbing test; weighted downstream formula test. | Treatment-loss fields remain separate and unused for this task. |
| Greedy DPV2/DPV3 | `src/heuristics/GreedyMetrics.cpp`, `src/heuristics/GreedyHeuristic.cpp` | DPV2 uses closed reachable count; DPV3 sums inverse-distance contribution of reachable nodes, each as unit value. Arc weights are scenario-frequency weights. | DPV2/DPV3 downstream node contribution should use `w_i`. Arc-frequency weights remain distinct from cell protection weights. | Heuristic semantic change. | Weighted DPV2/DPV3 ranking tests; source-weight guard; homogeneous equality. | Betweenness/closeness can remain structural/unweighted if documented. |
| Cumulative propagation graph | `src/heuristics/CumulativePropagationGraph.cpp` | Arc weights represent scenario frequency/probability over training arcs. | Keep arc weights as propagation frequencies, not cell values. Add separate access to cell weights in greedy scorers if needed. | Structurally unweighted. | Regression that cumulative arc weights unchanged under nonuniform cell weights. | Yes. |
| Reachability-greedy warm start | `src/heuristics/ReachabilityGreedyWarmStart.cpp` | Exact marginal improvement uses `FppRecourseEvaluator::expected_burned_area`; screening scores burned candidates by scenario probability plus outdegree tie term. | Exact marginal must use weighted expected loss. Screening should use burned candidate weight/probability or remain explicitly structural. | Heuristic/evaluator change. | Weighted marginal-choice test; homogeneous equality. | Outdegree tie term is structural. |
| Train/test OOS runners | `src/experiments/*OutOfSampleRunner.cpp`, `src/experiments/MethodDispatcher.cpp` | Build optimization instances without weights; evaluate train/test with physical burned counts; objective metrics named burned area/unit DPV. | Thread the same weight map into train and test optimization/evaluation. Report weighted loss objective plus physical burned-count metrics. | Plumbing/reporting and validation. | Paired train/test map identity tests; weighted objective vs physical count reporting tests. | Split generation remains structural. |
| CLI parsing | `src/main.cpp`, `include/experiments/*Options.hpp` | No weight-profile, weight-file, weight-seed, or profile parameters. | Add explicit options with homogeneous defaults. Invalid requested weights must fail, not fall back. | Interface/plumbing. | CLI parser tests for defaults, invalid ranges, missing files. | Existing non-weight commands default to homogeneous. |
| Experiment manifests | `scripts/generate_fpp_new_instances_scaling_manifests.py`, launcher scripts, config files | Manifests do not include weight profile/map/hash. Splits are deterministic by instance/case/method settings. | Add weight profile/seed/file/hash columns. Ensure generation is independent of method/risk/alpha/train/test/solver seed and shared across paired folders. | Experiment reproducibility plumbing. | Manifest determinism tests; paired-folder same-hash tests. | Worker scheduling remains structural. |
| Worker/merge/compact scripts | `scripts/run_fpp_new_instances_scaling_manifest_worker.py`, `scripts/merge_fpp_new_instances_scaling_experiment.py`, `scripts/fpp_new_instances_scaling_compact_schema.py` | Extract objective and burned-area fields without weight metadata. | Preserve physical burned count fields and add weighted-loss fields/profile/hash to worker, merged, and compact outputs. | Reporting/plumbing. | Schema round-trip tests; backward compatibility for old CSVs. | Aggregation mechanics structural. |
| Result and solution writers | `include/io/ExperimentResultWriter.hpp`, `src/io/ExperimentResultWriter.cpp`, `include/io/SolutionIO.hpp`, `src/io/SolutionIO.cpp`, `src/io/ResultWriter.cpp` | CSV/JSON fields focus on objective, expected burned area, empirical burned-area risk, selected nodes. | Add weighted objective/loss components and weight metadata; keep existing burned-area count columns for physical metrics. | Reporting/plumbing. | Golden output tests for new fields and homogeneous backward compatibility. | Selected-node output remains structural. |
| Risk utilities | `src/risk/RiskMeasure.cpp` | Correctly aggregates scenario losses with scenario probabilities; name "weighted" refers to scenario probabilities, not cell weights. | No core change if callers pass weighted cell-loss scenario values. Clarify names/docs if needed. | Structurally reusable. | Existing risk tests plus weighted cell-loss caller tests elsewhere. | Yes. |
| Tests | `tests/*` | Many assertions encode unit burned counts/objectives. | Add homogeneous regression tests and nonuniform weighted tests without deleting physical-count assertions. | Test expansion. | See implementation plan below. | No. |
| Documentation | `README.md`, `docs/*` | Multiple docs state `sum_i x_i`, unit DPV, unweighted eta, expected burned area. | Document homogeneous default, weight profiles, weight-map validation, weighted objective vs physical count reporting, and current unsupported phases. | Documentation. | README/audit consistency review. | No. |

## Important Validity Notes

1. FPP propagation constraints do not depend on cell weights. For positive
   weights, changing the loss coefficients from `1` to `w_i` preserves the
   feasible region.

2. Classical LP Benders cuts remain valid if and only if the subproblem objective
   itself is weighted and the cut coefficients are taken from that weighted LP.
   Do not hand-scale existing unit duals.

3. Dominance preprocessing currently relies on protected-node set inclusion. If
   candidate `i` protects a superset of candidate `j` in every scenario, then
   `i` weakly dominates `j` for any nonnegative cell weights. This reasoning
   fails for implementations based only on cardinality comparisons; the current
   global dominance code should remain set-inclusion based.

4. LLBI, projected LLBI, and combinatorial Benders are not safe to convert by
   replacing only a final constant. RHS values, saturated terms, per-node
   contributions, path coefficients, and separation violation calculations must
   all use the same weighted-loss algebra.

5. DPV methods must use descendant-cell weights. A common incorrect conversion
   would multiply a source-node DPV score by `w_source`; that does not implement
   `sum_{j in descendants(i)} w_j`.

6. Arc weights in `CumulativePropagationGraph` are scenario-frequency weights,
   not cell protection weights. They should remain separate.

## Proposed File-by-File Implementation Plan

### Phase 1: Canonical Weight Map

- Add `include/core/LandscapeWeightMap.hpp` and `src/core/LandscapeWeightMap.cpp`
  or equivalent local naming. Store profile, seed, normalization flags, original
  cell weights, optional cluster IDs, summary statistics, and deterministic hash.
- Extend `include/opt/OptimizationInstance.hpp` with compact cell weights,
  weight-profile metadata, and hash fields.
- Extend `src/opt/OptimizationInstanceBuilder.cpp` to attach homogeneous weights
  by default and validate supplied maps against original cell IDs.
- Add tests for homogeneous default, nonuniform mapping, invalid/missing cells,
  duplicate IDs, nonpositive values, and deterministic hash stability.

### Phase 2: Weight Generation and Loading

- Add weight generation/loading helpers under `include/io` / `src/io`.
- Implement `homogeneous`, `heterogeneous`, and `clustered` profiles with mean-1
  normalization and deterministic seeds.
- Add CLI options in `src/main.cpp` and option structs in `include/experiments`.
- Add paired-landscape validation so shortest-path and `_reburn` folders share
  the same geographic map.
- Add parser/generator tests and CLI default tests.

### Phase 3: FPP Weighted Loss and Evaluation

- Update `src/eval/FppRecourseEvaluator.cpp` and result structs to carry both
  weighted loss and physical burned count.
- Update `src/solver/FppSaaCplexModel.cpp` and
  `src/solver/FppCutReachabilityCplexModel.cpp` objective/CVaR loss expressions.
- Update `src/benders/FppScenarioSubproblem.cpp`,
  `src/benders/FppPersistentScenarioSubproblemManager.cpp`,
  `src/benders/FppBendersSolver.cpp`, and Branch-Benders validation paths.
- Keep `src/eval/BurnedAreaEvaluator.cpp` as physical-count reporting unless a
  separate weighted evaluator is introduced.
- Add weighted FPP model/evaluator agreement tests and homogeneous regression.

### Phase 4: Strengthening and Cut Families

- Update `src/benders/FppLiftedLowerBound.cpp` and exhaustive validation tests.
- Update coverage/path LLBI builders in `include/benders/FppStrengthening.hpp`.
- Update projected LLBI in `src/benders/FppProjectedLlbi.cpp`.
- Update combinatorial Benders in `src/benders/FppCombinatorialBenders.cpp`.
- Add weighted validity tests for LLBI, projected LLBI, combinatorial integer and
  fractional cuts, and lifting fallbacks.

### Phase 5: DPV and Heuristics

- Extend `DpvIndexBuilder` data or add helper functions for descendant weighted
  sums.
- Update `src/solver/DpvSaaCplexModel.cpp` and `src/benders/Dpv*` solvers to use
  descendant weights.
- Update `src/benchmarks/StaticDpvBenchmark.cpp`; pass canonical weights into
  `StaticDpvMipBenchmark` through existing downstream-value hooks.
- Update Greedy DPV2/DPV3 in `src/heuristics/GreedyMetrics.cpp` and
  `src/heuristics/GreedyHeuristic.cpp`; keep betweenness/closeness explicitly
  unweighted unless a weighted variant is later requested.
- Update `src/heuristics/ReachabilityGreedyWarmStart.cpp` exact marginal
  evaluation and optional screening scores.

### Phase 6: Runners, Outputs, and Scripts

- Thread weight options through `src/experiments/MethodDispatcher.cpp` and all
  OOS runners.
- Extend `ModelResult`, `StandardExperimentResult`, JSON/CSV writers, and
  solution writers with weighted loss fields and weight metadata/hash.
- Extend `scripts/generate_fpp_new_instances_scaling_manifests.py`, worker,
  merge, compact-schema scripts, and configs with weight profile/hash columns.
- Update `README.md` and relevant method documentation.

## Phase 0 Verification

Verification commands for this phase:

```bash
make cplex
make test
```

Results should be recorded in the final Phase 0 report after the commands run.

Observed Phase 0 result on this branch:

- `make cplex`: passed; built and installed `build_gpp/firebreak_cpp`.
- `make test`: passed. The existing `make test` target builds test binaries
  without `FIREBREAK_WITH_CPLEX`, so tests that require CPLEX solve execution
  reported their existing "Skipping ... because CPLEX is not enabled" messages.
