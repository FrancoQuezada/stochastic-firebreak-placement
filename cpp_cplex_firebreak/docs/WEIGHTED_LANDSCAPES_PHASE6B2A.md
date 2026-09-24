# Weighted Landscapes Phase 6B2A: Extended FPP CoverageLLBI

Phase 6B2A converts the existing extended FPP CoverageLLBI formulation to
arbitrary positive landscape weights. It does not implement PathLLBI, projected
CoverageLLBI, projected PathLLBI, combinatorial Benders, lifting, DPV,
Static-DPV, manifests, or batch integration.

## Existing Formulation Audit

Implementation files:

- `include/benders/FppStrengthening.hpp`
  - `build_fpp_coverage_llbi_data`
- `src/benders/FppBranchBendersSolver.cpp`
  - `add_coverage_llbi_constraints`
- `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`
  - homogeneous/restricted-only analogue

Before Phase 6B2A, CoverageLLBI was unit-count based. For scenario `s`, the
code computed:

```text
K_s = nodes reachable from ignition r_s in the directed scenario graph
f_empty_s = |K_s|
```

For every eligible candidate `i != r_s`, it computed the closed directed
downstream set reachable from `i`. For each node `k`, the coverage set was:

```text
A_{s,k} = { i in eligible candidates : k is reachable from i in scenario s }.
```

This is project-specific downstream membership. It is not an all-path
dominator test and not exact singleton-guaranteed protection. It is a
conservative coverage upper set: a selected candidate can only protect cells in
its downstream set, but cells in the downstream set may still burn through
alternate paths.

The extended master formulation created one continuous auxiliary variable
`zeta_{s,k}` for every baseline-burned node with nonempty `A_{s,k}`:

```text
0 <= zeta_{s,k} <= 1
zeta_{s,k} <= sum_{i in A_{s,k}} y_i
eta_s >= f_empty_s - sum_k zeta_{s,k}
```

The ignition candidate is excluded from coverage because selecting the
ignition does not block the ignition or its outgoing arcs. The ignition loss
remains in `f_empty_s`.

## Weighted Formulation

Phase 6B2A keeps the same structural coverage sets and replaces unit cell
contributions with protected-cell weights:

```text
Q_s(empty) = sum_{k in K_s} w_k
c_{s,k} = w_k
eta_s >= Q_s(empty) - sum_{k in K_s} w_k zeta_{s,k}
zeta_{s,k} <= sum_{i in A_{s,k}} y_i
0 <= zeta_{s,k} <= 1
```

Requirements preserved:

- weights attach to baseline-burned protected cells `k`, not candidates `i`;
- incidence coefficients remain structural and unweighted;
- noneligible baseline-burned cells may have positive `w_k` and can have
  coverage variables if covered by eligible downstream candidates;
- the ignition is included in the baseline loss and is not coverable unless a
  non-ignition candidate's downstream relation actually includes it, which the
  directed propagation graphs normally do not;
- nodes not baseline-burned are omitted from weighted CoverageLLBI terms.

## Validity

For selected set `S`, define:

```text
U_s(S) = union_{i in S} (K_s intersect Down_s(i)).
```

The existing coverage formulation represents:

```text
CoverageLB_s(y)
  = Q_s(empty)
    - sum_{k in K_s} w_k min(1, sum_{i in A_{s,k}} y_i)
  = Q_s(empty) - sum_{k in U_s(S)} w_k.
```

Any cell protected by selected firebreaks must be downstream of at least one
selected firebreak under the same directed scenario arcs. Therefore:

```text
K_s \ B_s(S) subseteq U_s(S).
```

With positive weights:

```text
Q_s(empty) - Q_s(S)
  <= sum_{k in U_s(S)} w_k.
```

Thus:

```text
Q_s(S) >= Q_s(empty) - sum_{k in U_s(S)} w_k = CoverageLB_s(y).
```

The cap is per cell, so overlapping coverage by multiple candidates subtracts
`w_k` at most once. Cells requiring a combination of candidates can be omitted
or over-included by the downstream upper set; either way the formulation can
only weaken the lower bound, not invalidate it. Exactness continues to come
from weighted lazy Benders cuts.

## Relation To Standard LLBI

Phase 6B1 standard LLBI is candidate aggregated:

```text
eta_s >= Q_s(empty) - sum_i delta_{s,i} y_i
```

where `delta_{s,i}` is the weighted empty-burned downstream mass of candidate
`i`. A cell can be subtracted multiple times if several selected candidates
cover it.

CoverageLLBI is cell capped:

```text
eta_s >= Q_s(empty) - sum_k w_k zeta_{s,k}
zeta_{s,k} <= sum_{i in A_{s,k}} y_i.
```

Both inequalities are valid and may be enabled together. They use separate
variables, constraints, counts, and diagnostics.

## Dominance And Restricted Methods

When global dominance is enabled through the out-of-sample callback runner,
CoverageLLBI is built after dominance preprocessing on the post-dominance
optimization instance. Coverage sets therefore reference retained candidates.
The dominance relation is structural protected-set inclusion, so replacing a
removed candidate with its retained representative does not worsen the exact
weighted objective.

Restricted-candidate CoverageLLBI remains disabled for non-homogeneous maps.
The restricted master only exposes active candidate variables, while safe
weighted CoverageLLBI would need to preserve linking coefficients for inactive,
later activated, deactivated, and reactivated candidates. Phase 6B2A keeps the
explicit rejection instead of adding partial active-set coverage terms.

## Cache And Hash Safety

No reusable CoverageLLBI cache is introduced in Phase 6B2A. Precomputation
records:

- weight-map hash;
- scenario count and scenario ids through scenario records;
- post-dominance candidate universe implicitly through compact candidate ids;
- validity mode `per-cell-capped-downstream-coverage-bound`.

Weighted coefficients are recomputed for each optimization instance. Structural
coverage sets are deterministic by compact/original node ordering and do not
depend on unordered-container iteration.

## Diagnostics

CoverageLLBI diagnostics added to model and experiment results:

- `coverage_llbi_weighted`
- `coverage_llbi_weight_map_hash`
- `coverage_llbi_scenarios_precomputed`
- `coverage_llbi_baseline_cells`
- `coverage_llbi_auxiliary_variables`
- `coverage_llbi_linking_constraints`
- `coverage_llbi_loss_constraints`
- `coverage_llbi_nonempty_coverage_sets`
- `coverage_llbi_total_incidence_terms`
- `coverage_llbi_build_time_sec`
- `coverage_llbi_validity_mode`

Existing fields remain:

- `coverage_llbi_enabled`
- `coverage_llbi_num_zeta_vars`
- `coverage_llbi_num_constraints`
- `coverage_llbi_precompute_time_sec`

## Tests

New tests:

- `tests/test_weighted_coverage_llbi.cpp`
- `tests/test_weighted_coverage_llbi_validity.cpp`
- `tests/test_weighted_coverage_llbi_cplex.cpp`

Coverage:

- manual weighted baseline loss;
- manual coverage sets with ignition, unique downstream candidate, overlapping
  candidates, non-coverable baseline node, and unreachable node;
- protected-cell coefficients `w_k`, not candidate weights `w_i`;
- overlap cap through the projected auxiliary optimum;
- diamond/downstream over-inclusion cases where exact singleton protection
  would be more selective, confirming the audited downstream formulation
  remains a valid lower bound;
- homogeneous implicit versus explicit unit maps;
- exhaustive validity on tree, diamond, overlap, unequal weights, and budgeted
  selections;
- fixed-seed randomized directed graph search;
- CPLEX equivalence for expected, CVaR, mean-CVaR, standard LLBI plus
  CoverageLLBI, root cuts plus CoverageLLBI, and explicit-loop Benders
  CoverageLLBI.

Projected tests remain numerical only through the capped expression; no
projected cuts are implemented in Phase 6B2A.

## Smoke Commands

The Sub20 smoke commands used fixed train/test ids and existing Phase 4 weight
maps, for example:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 \
  --train-ids 1-4 \
  --test-ids 5-8 \
  --alpha 0.01 \
  --run-id phase6b2a_heterogeneous_expected_coverage \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv \
  --use-coverage-llbi \
  --output-json results/weighted_phase6b2a_smoke/heterogeneous_expected_coverage.json \
  --output-csv results/weighted_phase6b2a_smoke/smoke.csv
```

Additional smokes use clustered weights, CVaR, mean-CVaR, standard LLBI plus
CoverageLLBI, and dominance plus CoverageLLBI.

## Deferred Work

Still not converted in Phase 6B2A:

- PathLLBI;
- projected CoverageLLBI;
- projected PathLLBI;
- combinatorial Benders and lifting;
- restricted-candidate weighted CoverageLLBI;
- DPV and Static-DPV.
