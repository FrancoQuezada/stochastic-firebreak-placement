# Weighted Landscapes Phase 5A: Explicit-Loop FPP-Benders

Phase 5A adds weighted burned-cell loss support to the classical explicit-loop
FPP-Benders command:

```text
run-fpp-benders-oos
```

This phase does not implement callback Branch-and-Benders, restricted-candidate
methods, combinatorial Benders, LLBI/projected LLBI, DPV, Static-DPV, heuristic
rankings, or Phase 5B changes.

## Files Modified

Explicit-loop Benders path:

- `include/benders/FppScenarioSubproblem.hpp`
- `src/benders/FppScenarioSubproblem.cpp`
- `src/benders/FppBendersSolver.cpp`
- `include/experiments/FppBendersOutOfSampleRunner.hpp`
- `src/experiments/FppBendersOutOfSampleRunner.cpp`
- `src/main.cpp`

Build and tests:

- `Makefile`
- `tests/test_weighted_fpp_benders_cplex.cpp`

Shared Phase 4 weighted-loss utilities are reused through
`solver/FppWeightedLossUtils`.

## Weighted Scenario Subproblem

For each training scenario `s`, the explicit-loop Benders subproblem now solves:

```text
Q_s(ybar) = min sum_i w_i x_i^s
```

The compact weight `w_i` is aligned with the compact optimization node mapper.
Only burn variables `x_i^s` are weighted. Firebreak-copy, reachability, and
structural variables receive no objective weight.

The FPP burn convention is unchanged:

- the ignition node always burns;
- selecting the ignition does not remove ignition loss;
- a selected non-root firebreak blocks incoming propagation and contributes zero
  burn loss if reached;
- non-eligible nodes that burn contribute their weights.

When no explicit map is attached, unit compact weights preserve the homogeneous
legacy behavior.

## Benders Cuts

Cuts keep the existing sign convention and stored form:

```text
eta_s >= c_s + sum_i a_si y_i
```

Equivalently:

```text
eta_s >= Q_s(ybar) + sum_i pi_si (y_i - ybar_i)
```

Weights enter only through the weighted LP objective. The cut coefficients and
constant are obtained from the weighted LP dual solution. Legacy cuts are not
manually scaled.

The cut export records the active weighted units with:

```text
objective_metric
weight_profile
weight_map_hash
scenario_weighted_recourse_value
cut_value_at_incumbent
cut_violation
```

For generated cuts, `cut_value_at_incumbent` matches the weighted subproblem
value within the existing tolerance.

## Master and Bounds

The master structure is unchanged. Each `eta_s` now approximates weighted
scenario recourse.

Expected value:

```text
min sum_s rho_s eta_s
```

CVaR:

```text
min phi + 1 / (1 - beta) * sum_s rho_s xi_s
xi_s >= eta_s - phi
xi_s >= 0
```

Mean-CVaR:

```text
min (1 - lambda) * sum_s rho_s eta_s
  + lambda * (phi + 1 / (1 - beta) * sum_s rho_s xi_s)
```

Lower bounds, incumbent upper bounds, convergence gaps, and final objectives are
all in weighted-loss units. Existing master initialization, iteration sequence,
tolerances, and optional lifted-lower-bound behavior are preserved.

The incumbent upper bound uses the same weighted risk aggregation utility as the
validated direct FPP-SAA path:

- expected: probability-weighted mean of weighted scenario losses;
- CVaR: repository `risk::compute_weighted_risk_metrics` convention;
- mean-CVaR: `(1 - lambda) * expected + lambda * CVaR`.

## Weight Map Loading

`run-fpp-benders-oos` accepts:

```bash
--weight-map-file results/weights/map.csv
```

If omitted, the homogeneous default is used. If present, the command loads the
canonical Phase 1 CSV, validates it against the full compact optimization
universe, maps by original Cell2Fire node IDs, and attaches the same map to:

- master and scenario subproblem construction;
- train recourse validation;
- test recourse validation.

The command fails before solving if the file is missing, malformed, incomplete,
contains incompatible extra nodes, or includes non-positive/non-finite weights.
The `weight_map_hash` must match across optimization, train evaluation, and test
evaluation.

## Objective Validation

After termination, the selected firebreaks are evaluated with the weighted
`FppRecourseEvaluator`. The final reported feasible objective is compared
against the active weighted evaluator objective:

- expected: expected weighted burn loss;
- CVaR: weighted CVaR;
- mean-CVaR: weighted mean-CVaR blend.

Reported fields include:

```text
solver_weighted_objective
evaluator_weighted_objective
objective_validation_abs_difference
objective_validation_rel_difference
objective_validation_passed
```

Physical burned-cell counts remain in the output as reporting metrics, not as
optimization objectives.

## Tests

The Phase 5A CPLEX-backed test source is:

```text
tests/test_weighted_fpp_benders_cplex.cpp
```

It is compiled through these explicit targets:

```bash
make build_gpp/test_weighted_fpp_benders_subproblem
make build_gpp/test_weighted_fpp_benders_cut
make build_gpp/test_weighted_fpp_benders_cplex
```

The test cases cover:

- weighted subproblem objective values;
- ignition-selected behavior;
- reached non-root firebreak behavior;
- non-eligible weighted burn variables;
- cut tightness at the generating incumbent;
- exhaustive cut validity by enumerating feasible firebreak vectors;
- homogeneous cut regression;
- direct FPP-SAA versus explicit-loop Benders equivalence;
- expected, CVaR, and mean-CVaR objectives;
- missing/incomplete map rejection;
- iteration-limited feasible objective validation.

The validation tolerance used by the tests and solver validation is the existing
weighted objective rule:

```text
abs_difference <= 1e-6 OR relative_difference <= 1e-6
```

## Smoke Commands

The smoke runs used Sub20 with train scenarios `1,2`, test scenarios `3,4`,
`alpha=0.01`, one thread, and a 30 second limit.

Default homogeneous expected:

```bash
./build_gpp/firebreak_cpp run-fpp-benders-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5a_benders_default_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --max-iterations 30 --tolerance 1e-6 \
  --risk-measure expected
```

Explicit homogeneous expected:

```bash
./build_gpp/firebreak_cpp run-fpp-benders-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5a_benders_homogeneous_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --max-iterations 30 --tolerance 1e-6 \
  --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_homogeneous.csv
```

Heterogeneous expected and direct reference:

```bash
./build_gpp/firebreak_cpp run-fpp-benders-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5a_benders_heterogeneous_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --max-iterations 30 --tolerance 1e-6 \
  --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv

./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase5a_direct_heterogeneous_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --fpp-formulation base --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv
```

Clustered expected, heterogeneous CVaR, and clustered mean-CVaR were run with
the same split and matching direct `run-fpp-saa-oos` references.

## Smoke Summary

```text
run_id                                      status   risk       profile       objective     direct_ref    eval_diff
phase5a_benders_default_expected           Optimal  expected   homogeneous   41.500000     n/a           0
phase5a_benders_homogeneous_expected       Optimal  expected   loaded        41.500000     n/a           0
phase5a_benders_heterogeneous_expected     Optimal  expected   loaded        40.808022     40.808022     0
phase5a_benders_clustered_expected         Optimal  expected   loaded        36.617952     36.617952     0
phase5a_benders_heterogeneous_cvar         Optimal  cvar       loaded        49.354256     49.354256     0
phase5a_benders_clustered_mean_cvar        Optimal  mean-cvar  loaded        44.906172     44.906172     0
```

Selected firebreaks:

```text
default expected:              154,174,290,328
explicit homogeneous expected: 154,174,290,328
heterogeneous expected:        154,174,290,328
clustered expected:            154,174,290,328
heterogeneous CVaR:            136,154,174,290
clustered mean-CVaR:           134,154,174,290
```

Weight hashes:

```text
homogeneous:   fnv1a64:37686b4201c1b949
heterogeneous: fnv1a64:8647c9d000ac6c4e
clustered:     fnv1a64:b05f40be4c10dd28
```

## Known Limitations

- Only explicit-loop `run-fpp-benders-oos` is weighted in this phase.
- Callback Branch-and-Benders and restricted-candidate code paths remain
  deferred.
- No new weighted LLBI, singleton, dominance, DPV, or heuristic strengthening
  was added.
- Batch manifests were not broadened in this phase.
- Smoke runs are correctness checks only and are not scientific experiments.
