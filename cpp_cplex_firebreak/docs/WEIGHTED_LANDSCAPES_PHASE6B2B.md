# Weighted Landscapes Phase 6B2B: Extended FPP PathLLBI

Phase 6B2B converts the existing extended FPP PathLLBI formulation to
arbitrary positive landscape weights. It does not implement projected PathLLBI,
projected CoverageLLBI, combinatorial Benders, lifting, DPV, Static-DPV,
manifests, or batch integration.

## Existing Formulation Audit

Implementation files:

- `include/benders/FppStrengthening.hpp`
  - `build_fpp_path_llbi_data`
  - `enumerate_capped_paths`
- `src/benders/FppBranchBendersSolver.cpp`
  - `add_path_llbi_constraints`
- `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`
  - homogeneous/restricted-only analogue

Before Phase 6B2B, PathLLBI was unit-count based. For each scenario `s`, the
code built directed successors from Cell2Fire propagation arcs and enumerated
simple ignition-to-node paths. The path family was static, deterministic, and
capped by `path_llbi_max_paths_per_node`.

For each baseline node `k`, it created one continuous variable:

```text
0 <= b_{s,k} <= 1
```

and one row per stored path `p`:

```text
b_{s,k} + sum_{i in blockers(p)} y_i >= 1.
```

The final scenario row was:

```text
eta_s >= sum_k b_{s,k}.
```

`blockers(p)` contains only eligible compact nodes on the directed path, never
the ignition/root. If the destination `k` is eligible and non-root, it is a
valid blocker because selecting a non-root node blocks incoming propagation
into that node. Non-eligible path nodes remain structural path nodes but do not
appear as `y` terms. The ignition node is represented by an empty path, which
forces `b_{s,r}=1`; selecting the ignition does not relax this row.

## Weighted Formulation

Phase 6B2B keeps the structural path constraints and replaces unit node
contributions with destination-cell weights:

```text
eta_s >= sum_{k in K_s} w_k b_{s,k}
b_{s,k} + sum_{i in blockers(p)} y_i >= 1  for each stored p in P_{s,k}
0 <= b_{s,k} <= 1.
```

The coefficient is always `w_k`, the weight of the potentially burned
destination cell. Path incidence remains unweighted and structural. Candidate
weights are not used in path rows.

## Path Family And Truncation

PathLLBI now uses the baseline-reachable set `K_s`, the nodes reachable from the
scenario ignition in the directed scenario graph with no firebreaks. Unreachable
or only-observed nodes do not create PathLLBI variables.

The enumerator:

- follows directed wildfire propagation arcs only;
- enumerates simple paths;
- sorts successors and candidate blockers deterministically;
- removes duplicate blocker sets deterministically;
- never includes the root as a blocker;
- detects truncation by enumerating up to `max_paths_per_node + 1`;
- keeps only real stored paths when truncation occurs.

Truncation omits valid paths and can only weaken this lower bound. It does not
fabricate paths and does not force a node to burn without an actual stored
unblocked path. Diagnostics report whether enumeration was complete and how
many node path sets were truncated.

## Validity

For fixed selected firebreaks `S`, a stored path `p` is unblocked if it contains
no selected blocker. If a node `k` has at least one unblocked stored path, that
path is an actual directed ignition-to-`k` propagation path under the FPP
convention, so `k` burns in the exact recourse.

For each stored path row:

```text
b_{s,k} >= 1 - sum_{i in blockers(p)} y_i.
```

At the master optimum for fixed `y`:

```text
b_{s,k} = max(0, 1 - min_{p in P_{s,k}} sum_{i in blockers(p)} y_i).
```

Thus `b_{s,k}` can be one only when a stored actual path is unblocked; otherwise
it may be zero. Therefore `b_{s,k} <= x^s_k(y)` for complete or truncated
stored path families. With positive weights:

```text
sum_k w_k b_{s,k} <= sum_k w_k x^s_k(y) = Q_s(y).
```

The inequality is valid scenario by scenario. Exactness continues to come from
weighted lazy Benders cuts.

## Relation To Standard LLBI And CoverageLLBI

Standard LLBI is candidate aggregated:

```text
eta_s >= Q_s(empty) - sum_i delta_{s,i} y_i.
```

CoverageLLBI is protected-cell capped:

```text
eta_s >= Q_s(empty) - sum_k w_k zeta_{s,k}.
```

PathLLBI is burn-path based:

```text
eta_s >= sum_k w_k b_{s,k}.
```

The three formulations use separate variables, constraints, counts, and
diagnostics. Standard LLBI, CoverageLLBI, and PathLLBI can be enabled together
in callback Branch-and-Benders and remain exact because all are valid lower
bounds on the same weighted `eta_s`.

## Dominance And Restricted Methods

When global dominance preprocessing is enabled through the callback runner,
PathLLBI is built after dominance preprocessing on the reduced optimization
instance, so stored paths reference retained candidates. The exact weighted
objective remains protected by the weighted lazy cuts.

Restricted-candidate weighted PathLLBI remains disabled. A safe restricted
implementation would need to retain path coefficients for inactive, later
activated, deactivated, and reactivated candidates. Phase 6B2B keeps the
explicit rejection instead of constructing partial active-candidate path rows.

## Cache And Hash Safety

No reusable PathLLBI cache is introduced in Phase 6B2B. Precomputation records:

- weight-map hash;
- scenario count and scenario ids through scenario records;
- post-dominance candidate universe implicitly through compact candidate ids;
- path limit and truncation diagnostics;
- validity mode `directed-simple-path-burning-lower-bound`.

Weighted coefficients are recomputed for each optimization instance. Structural
path enumeration is deterministic and does not depend on unordered-container
iteration.

## Diagnostics

PathLLBI diagnostics added to model and experiment results:

- `path_llbi_weighted`
- `path_llbi_weight_map_hash`
- `path_llbi_scenarios_precomputed`
- `path_llbi_baseline_nodes`
- `path_llbi_auxiliary_variables`
- `path_llbi_path_constraints`
- `path_llbi_loss_constraints`
- `path_llbi_total_paths`
- `path_llbi_total_candidate_incidence_terms`
- `path_llbi_nodes_without_paths`
- `path_llbi_path_enumeration_complete`
- `path_llbi_paths_truncated`
- `path_llbi_build_time_sec`
- `path_llbi_validity_mode`

Existing fields remain:

- `path_llbi_enabled`
- `path_llbi_num_b_vars`
- `path_llbi_num_path_constraints`
- `path_llbi_num_paths_used`
- `path_llbi_precompute_time_sec`

## Tests

New tests:

- `tests/test_weighted_path_llbi.cpp`
- `tests/test_weighted_path_llbi_validity.cpp`
- `tests/test_weighted_path_llbi_cplex.cpp`

Coverage:

- manual single and parallel directed paths;
- destination weights `w_k`, not candidate weights;
- ignition empty-path convention;
- non-eligible path nodes and empty blocker paths;
- duplicate blocker-set deduplication;
- path truncation diagnostics;
- homogeneous implicit versus explicit unit maps;
- exhaustive validity on chains, diamonds, cycles, unequal weights, and
  truncated path families;
- fixed-seed randomized directed graph search;
- CPLEX equivalence for expected, CVaR, mean-CVaR, standard LLBI plus PathLLBI,
  CoverageLLBI plus PathLLBI, root cuts plus PathLLBI, and explicit-loop
  PathLLBI.

Projected tests remain out of scope; no projected cuts are implemented in Phase
6B2B.

## Smoke Commands

The Sub20 smoke commands use fixed train/test ids and existing Phase 4 weight
maps, for example:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1-4 \
  --test-ids 5-8 \
  --alpha 0.01 \
  --run-id phase6b2b_heterogeneous_expected_path \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --use-path-llbi \
  --output-json results/weighted_phase6b2b_smoke/heterogeneous_expected_path.json \
  --output-csv results/weighted_phase6b2b_smoke/smoke.csv
```

Additional smokes use clustered weights, CVaR, mean-CVaR, standard LLBI plus
PathLLBI, CoverageLLBI plus PathLLBI, and dominance plus PathLLBI.

## Deferred Work

Still not converted in Phase 6B2B:

- projected CoverageLLBI;
- projected PathLLBI;
- combinatorial Benders and lifting;
- restricted-candidate weighted LLBI families;
- DPV and Static-DPV.
