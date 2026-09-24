# Weighted Landscapes Phase 7B2: DPV Optimization Audit

## Scope

Phase 7B2 covers only solution-dependent DPV optimization models:

- DPV-SAA direct CPLEX model.
- Explicit-loop DPV Benders.
- Callback DPV Branch-and-Benders.
- DPV lifted lower-bound inequalities used by those decompositions.

The original FPP models, static/greedy DPV heuristics, restricted-candidate FPP,
combinatorial FPP, paired reburn integration, manifests, batch launchers, and
analysis scripts are intentionally out of scope.

## Product-Pair Semantics

The current DPV optimization indexer builds product pairs
`(source, successor, descendant)` for every direct successor of `source` and
every closed descendant of `source`. This preserves legacy multiplicity: if a
destination appears in multiple product pairs, it contributes once per active
pair. It is not capped by destination.

Weighted Phase 7B2 keeps that structural definition. Each active product pair
contributes:

```text
weight(descendant) * z[source, successor, descendant]
```

It does not use `weight(source) * DPV(source)` or candidate-weighted scores.

## Ignition Convention

The DPV optimization propagation model follows the FPP convention already used
in the code:

- the ignition node always burns;
- a selected firebreak blocks incoming propagation into a non-root target;
- selecting the ignition node does not prevent it from burning;
- selecting the ignition node does not block outgoing arcs from the ignition.

The production default metadata policy is `fpp-safe`. The explicit `legacy`
policy is accepted and reported for regression experiments, but the current
product-pair optimization model is governed by the FPP-safe propagation
constraints.

## Implementation Summary

- `DpvSaaCplexModel`: multiply each product-pair objective coefficient by the
  compact weight of `descendant_index`; report DPV model metadata.
- `DpvScenarioSubproblem`: use the same weighted product-pair objective so
  Benders dual cuts are generated in weighted units.
- `DpvPersistentScenarioSubproblemManager`: mirror the weighted subproblem
  objective for callback Branch-and-Benders.
- `DpvLiftedLowerBound`: replace unit product counts with sums of
  `weight(descendant)` while preserving product-pair multiplicity.
- DPV runners/dispatcher: attach weight maps through the canonical weight-map
  loader, pass `--dpv-ignition-policy`, and keep DPV surrogate objectives
  separate from FPP recourse evaluation.
- Result writer: append DPV model, Benders, and LLBI metadata fields.
- CLI usage: document `--weight-map-file` and `--dpv-ignition-policy` for the
  direct DPV-SAA, explicit-loop DPV Benders, and callback DPV
  Branch-and-Benders commands.

## Validation Results

- `make cplex` passed.
- `make test` passed.
- Phase 7B2 unit and CPLEX tests passed:
  - `test_weighted_dpv_saa`
  - `test_weighted_dpv_benders_subproblem`
  - `test_weighted_dpv_benders_cut`
  - `test_weighted_dpv_benders_cut_validity`
  - `test_weighted_dpv_benders_cplex`
  - `test_weighted_dpv_branch_benders_cplex`
  - `test_weighted_dpv_llbi`
  - `test_weighted_dpv_llbi_validity`
  - `test_weighted_dpv_reporting`
- Sub20 smoke runs passed for:
  - homogeneous, heterogeneous, and clustered DPV-SAA;
  - heterogeneous and clustered DPV Benders with DPV LLBI;
  - heterogeneous and clustered DPV Branch-and-Benders with DPV LLBI and root
    cuts;
  - explicit `--dpv-ignition-policy legacy` metadata regression.

Smoke artifacts were written under:

```text
results/weighted_phase7b2_smoke/
```

Those artifacts are validation outputs, not source inputs.
