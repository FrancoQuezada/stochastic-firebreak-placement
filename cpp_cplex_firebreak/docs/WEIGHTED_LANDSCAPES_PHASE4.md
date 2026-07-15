# Weighted Landscapes Phase 4: Direct FPP-SAA

Phase 4 adds weighted burned-node losses to the direct FPP-SAA models only.

Modified direct paths:

- `FppSaaCplexModel`
- `FppCutReachabilityCplexModel`
- `solve-fpp-saa`
- `run-fpp-saa-oos`

Deferred paths:

- explicit-loop FPP-Benders
- callback Branch-and-Benders
- restricted-candidate methods
- combinatorial Benders
- LLBI and projected LLBI
- DPV and Static-DPV
- heuristic rankings
- batch manifest generation

## Scenario Loss

For each scenario `s`, the optimized loss is:

```text
L_s(y) = sum_{i in V_s} w_i x_i^s
```

`w_i` is read from `OptimizationInstance::compact_cell_weights`, aligned with the compact node mapper. The same `LandscapeWeightMap` hash is used by optimization and recourse evaluation.

The direct base formulation weights every burn variable `x_i^s`.

The cut/reachability formulation weights only burn variables `x_i^s`. Reachability variables `q_i^s` have zero objective coefficient.

FPP conventions are unchanged:

- the ignition node always burns;
- selecting the ignition as a firebreak does not remove ignition burn loss;
- a selected non-root firebreak blocks incoming propagation and has zero burn loss if reached;
- selecting a non-root firebreak does not change outgoing arcs except through the existing propagation logic.

## Objectives

Expected:

```text
min sum_s rho_s L_s
```

CVaR:

```text
min phi + 1 / (1 - beta) * sum_s rho_s xi_s
xi_s >= L_s - phi
xi_s >= 0
```

Mean-CVaR:

```text
min (1 - lambda) * sum_s rho_s L_s
  + lambda * (phi + 1 / (1 - beta) * sum_s rho_s xi_s)
```

`risk_threshold_value`, `expected_loss_component`, and `cvar_loss_component` are now in the active weighted-loss units for direct FPP-SAA runs.

## Weight Loading

Direct FPP commands accept:

```bash
--weight-map-file results/weights/map.csv
```

Supported in this phase:

```bash
./build_gpp/firebreak_cpp solve-fpp-saa ...
./build_gpp/firebreak_cpp run-fpp-saa-oos ...
```

When omitted, the builder-provided homogeneous map preserves unit-weight behavior. When provided, the CSV is loaded through the Phase 1 `LandscapeWeightMap` parser and validated against the complete compact optimization node universe. Missing, extra, nonfinite, or nonpositive weights fail before solving.

Weight generation remains separate:

```bash
./build_gpp/firebreak_cpp generate-weight-map \
  --landscape Sub20 \
  --weight-profile heterogeneous \
  --weight-seed 42 \
  --weight-normalize true \
  --output-csv results/weights/sub20_heterogeneous.csv \
  --output-json results/weights/sub20_heterogeneous.json
```

## Validation

Post-solve validation compares the CPLEX objective against the weighted recourse evaluator:

- expected: `expected_weighted_burn_loss`;
- CVaR: weighted empirical CVaR from `risk::compute_weighted_risk_metrics`;
- mean-CVaR: `(1 - lambda) * expected + lambda * CVaR`.

The validation rule is:

```text
absolute_difference <= 1e-6 OR relative_difference <= 1e-6
```

Physical burned counts remain available as reporting metrics and are not used inside weighted objectives.

## Output Additions

Direct FPP result paths now expose:

```text
weight_profile
weight_map_file
weight_map_hash
weight_normalized
weight_mean
weight_min
weight_max
weight_total
objective_metric
solver_weighted_objective
evaluator_weighted_objective
objective_validation_abs_difference
objective_validation_rel_difference
objective_validation_passed
```

`run-fpp-saa-oos` also reports:

```text
train_expected_weighted_burn_loss
test_expected_weighted_burn_loss
train_weighted_var
test_weighted_var
train_weighted_cvar
test_weighted_cvar
train_percentage_landscape_value_burned
test_percentage_landscape_value_burned
train_percentage_high_value_weight_burned
test_percentage_high_value_weight_burned
```

## Warm Starts

Warm-start feasibility construction is unchanged. The cut/reachability full start still uses the evaluator to populate `y/x/q`; its diagnostic recourse objective is now the expected weighted burn loss.

## Tests

Non-CPLEX:

```bash
make build_gpp/test_landscape_weight_map
./build_gpp/test_landscape_weight_map

make build_gpp/test_landscape_weight_generator
./build_gpp/test_landscape_weight_generator

make build_gpp/test_fpp_recourse_evaluator
./build_gpp/test_fpp_recourse_evaluator

make build_gpp/test_weighted_fpp_recourse_evaluator
./build_gpp/test_weighted_fpp_recourse_evaluator

make build_gpp/test_fpp_saa_model_structure
./build_gpp/test_fpp_saa_model_structure

make build_gpp/test_weighted_fpp_saa_model
./build_gpp/test_weighted_fpp_saa_model
```

CPLEX-backed:

```bash
make build_gpp/test_weighted_fpp_saa_cplex
./build_gpp/test_weighted_fpp_saa_cplex
```

Full validation:

```bash
make cplex
make test
```

## Smoke Commands

The Phase 4 smoke used Sub20, train scenarios `1,2`, test scenarios `3,4`, and `alpha=0.01`.

No explicit weight file:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase4_no_weight_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --fpp-formulation base --risk-measure expected
```

Explicit homogeneous:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase4_homogeneous_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --fpp-formulation base --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_homogeneous.csv
```

Heterogeneous:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase4_heterogeneous_expected \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --fpp-formulation base --risk-measure expected \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_heterogeneous.csv
```

Clustered mean-CVaR cut formulation:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 --train-ids 1,2 --test-ids 3,4 \
  --alpha 0.01 --run-id phase4_clustered_mean_cvar_cut \
  --time-limit 30 --mip-gap 0 --threads 1 \
  --fpp-formulation cut --risk-measure mean-cvar \
  --cvar-beta 0.5 --cvar-lambda 0.5 \
  --weight-map-file results/weighted_phase4_smoke/weights/sub20_clustered.csv
```

## Known Limitations

Phase 4 intentionally does not add weighted objectives to Benders, DPV, LLBI, projected LLBI, restricted-candidate methods, or heuristic ranking logic. Those methods may still report legacy burned-area metrics and should not be interpreted as weighted optimization methods until their phases are implemented.
