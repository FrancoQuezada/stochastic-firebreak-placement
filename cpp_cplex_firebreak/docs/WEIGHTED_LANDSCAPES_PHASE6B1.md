# Weighted Landscapes Phase 6B1: Standard FPP LLBI

Phase 6B1 converts the existing standard FPP lifted lower-bound inequalities
to arbitrary positive landscape weights for the LP-based FPP Branch-and-Benders
code path. It does not implement CoverageLLBI, PathLLBI, projected
inequalities, combinatorial Benders, lifting, DPV, Static-DPV, manifests, or
batch launcher changes.

## Audit

Files and functions:

- `include/benders/FppLiftedLowerBound.hpp`
- `src/benders/FppLiftedLowerBound.cpp`
  - `evaluate_fixed_y_fpp_loss`
  - `evaluate_optimistic_singleton_fpp_loss`
  - `build_fpp_lifted_lower_bound_for_scenario`
  - `build_fpp_lifted_lower_bounds`
  - `validate_fpp_lifted_lower_bound_exhaustive`
- solver integration:
  - `src/benders/FppBendersSolver.cpp`
  - `src/benders/FppBranchBendersSolver.cpp`
  - `src/benders/FppRestrictedCandidateBranchBendersSolver.cpp`

The pre-existing implementation was named as a lifted/singleton lower-bound
module, but it did not solve exact singleton LP subproblems. For scenario `s`
and candidate `i`, it computed the closed directed downstream set from `i` and
subtracted the unit count of empty-burned downstream nodes.

The master constraint has the form:

```text
eta_s >= f_empty_s - sum_i delta_{s,i} y_i
```

where the old unit implementation used:

```text
f_empty_s = |B_s(empty)|
delta_{s,i} = |B_s(empty) intersect Down_s(i)|
```

`Down_s(i)` is the closed directed downstream set of `i`. The ignition
coefficient is zero because selecting the ignition does not block the ignition
or its outgoing arcs under the project FPP convention.

## Weighted Formula

Phase 6B1 keeps the same downstream-union lower-bound construction and replaces
unit counts with weighted losses:

```text
Q_s(y) = sum_{k in B_s(y)} w_k
f_empty_s = Q_s(empty)
delta_{s,i} = sum_{k in B_s(empty) intersect Down_s(i)} w_k
eta_s >= f_empty_s - sum_i delta_{s,i} y_i
```

This is intentionally the weighted equivalent of the current implementation,
not the exact singleton benefit `Q_s(empty) - Q_s({i})`. Exact singleton
benefits can be invalid as a summed lower bound on graphs with complementary
parallel-path blocking, because two candidates can have zero individual benefit
and positive joint benefit. The downstream-union coefficient is conservative:
it can overestimate the benefit of one selected candidate, which only weakens
the lower bound.

## Validity

For any selected set `S`, every node protected by selected firebreaks is inside
the union of their closed downstream sets:

```text
B_s(empty) \ B_s(S) subseteq union_{i in S} (B_s(empty) intersect Down_s(i)).
```

With strictly positive weights:

```text
Q_s(empty) - Q_s(S)
  <= sum_{k in union_{i in S}(B_s(empty) intersect Down_s(i))} w_k
  <= sum_{i in S} delta_{s,i}.
```

Therefore:

```text
Q_s(S) >= Q_s(empty) - sum_{i in S} delta_{s,i}.
```

This re-establishes scenario-wise LLBI validity for every budget-feasible
binary `y`. The validation test enumerates feasible selections on weighted DAG
and cyclic examples, including a parallel-path complementarity case.

The FPP propagation convention is unchanged:

- the ignition always burns;
- a selected non-root firebreak blocks incoming propagation into that node;
- if the ignition node is selected, it still burns;
- selecting the ignition does not block outgoing arcs from the ignition.

## Enabled Paths

Enabled for non-homogeneous weighted maps:

- `FppBendersSolver` standard LLBI;
- `FppBranchBendersSolver` standard LLBI;
- `run-fpp-branch-benders-oos` standard LLBI.

Still rejected for non-homogeneous weighted maps:

- restricted-candidate standard LLBI;
- CoverageLLBI;
- PathLLBI;
- projected Coverage/Path LLBI;
- combinatorial Benders and lifting;
- DPV and Static-DPV conversions.

Restricted-candidate LLBI remains disabled for non-homogeneous maps because the
current restricted master only has variables for active candidates. A valid
weighted LLBI over the full eligible universe would need to retain coefficients
for inactive candidates safely or rebuild the inequality consistently as the
active set changes. Phase 6B1 does not add that machinery.

Phase 6A's conditional zero-benefit limitation remains in force: it is still a
structural detector only, and no local fixing is applied.

## Diagnostics

The solver and experiment result records now expose:

- `benders_lifted_lower_bound_weighted`
- `benders_lifted_lower_bound_weight_map_hash`
- `benders_lifted_lower_bound_scenarios_precomputed`
- `benders_lifted_lower_bound_singletons_evaluated`
- `benders_lifted_lower_bound_precompute_time_sec`
- `benders_lifted_lower_bound_no_firebreak_loss_min`
- `benders_lifted_lower_bound_no_firebreak_loss_max`
- `benders_lifted_lower_bound_singleton_benefit_min`
- `benders_lifted_lower_bound_singleton_benefit_max`
- `benders_lifted_lower_bound_constraints_added`
- `benders_lifted_lower_bound_cache_hit`
- `benders_lifted_lower_bound_validity_mode`

The validity mode is `downstream-union-bound`. No LLBI cache is implemented in
Phase 6B1, so `cache_hit` is reported as false.

## Tests

New or updated tests:

- `tests/test_weighted_standard_llbi.cpp`
- `tests/test_weighted_standard_llbi_validity.cpp`
- `tests/test_weighted_fpp_branch_benders_cplex.cpp`

The CPLEX test now checks that weighted Branch-and-Benders with standard LLBI
matches the direct weighted FPP-SAA optimum and reports the new diagnostics.
