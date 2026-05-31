# C++ Stochastic Firebreak Placement Foundation

This folder contains the C++ foundation for the stochastic firebreak placement
research codebase. It currently supports Cell2Fire-style data loading, scenario
ID detection, ignition loading, directed propagation graph construction,
smoke-test reporting, fixed-firebreak burned-area evaluation, direct FPP-SAA
optimization, direct DPV-SAA optimization, the Static-DPV and Static-DPV-MIP
benchmarks, greedy heuristic baselines, explicit-loop FPP/DPV Benders
decomposition, callback Branch-and-Benders solvers, restricted-candidate FPP
Branch-and-Benders, FPP static and projected lower-bound strengthening
families, global dominance preprocessing diagnostics, and the FPP
combinatorial Branch-and-Benders variant.

The direct FPP-SAA and DPV-SAA models are implemented behind optional CPLEX
support. Static-DPV, Static-DPV-MIP, and greedy heuristics are solver-free. The
default build does not require CPLEX. The Benders and Branch-and-Benders
implementations are CPLEX-backed when they solve optimization models.

## Build

From this directory:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

The executable is:

```bash
./firebreak_cpp
```

This project requires C++17 or newer and uses only the C++ standard library.

If CMake is unavailable, the legacy full-build script is still available:

```bash
./build_gpp.sh
```

The preferred local workflow is the incremental `Makefile`:

```bash
make help
make
make build
make cplex
make test
```

`make` and `make cplex` compile with CPLEX using `CPLEX_STUDIO_DIR`, defaulting
to `/opt/ibm/ILOG/CPLEX_Studio2211`. `make build` compiles without CPLEX.
The Makefile builds per-source `.o` files and `.d` dependency files, so running
`make` twice without code changes does not recompile. If one `.cpp` or included
header changes, only the affected objects are rebuilt and then relinked.

```bash
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 make cplex
```

The intended workflow is:

```bash
make cplex
scripts/run_phase11_debug.sh
scripts/run_phase12_small_calibration.sh
```

or, using Make targets:

```bash
make phase11-debug
make phase12-small
```

The experiment and aggregation Make targets use the existing
`build_gpp/firebreak_cpp` binary. They do not rebuild automatically. Re-run
`make` after changing C++ code; it will rebuild only what is necessary.

Larger calibration scripts are guarded so they are not launched accidentally:

```bash
CONFIRM_LONG_RUN=1 make phase12-exact
CONFIRM_LONG_RUN=1 make phase12-alpha02
```

Aggregation convenience commands:

```bash
make aggregate-phase11-debug
make aggregate-phase12-small
```

The scripts do not define new experiments. They run the existing manifests and
the same `firebreak_cpp` flags documented below.

This produces:

```text
build_gpp/firebreak_cpp
build_gpp/cplex/firebreak_cpp
build_gpp/nocplex/firebreak_cpp
build_gpp/obj/cplex/**/*.o
build_gpp/obj/cplex/**/*.d
build_gpp/obj/nocplex/**/*.o
build_gpp/obj/nocplex/**/*.d
build_gpp/obj/tests/**/*.o
build_gpp/obj/tests/**/*.d
build_gpp/test_burned_area_evaluator
build_gpp/test_index_mapper
build_gpp/test_dpv_index_builder
build_gpp/test_fpp_saa_model_structure
build_gpp/test_dpv_saa_model_structure
build_gpp/test_graph_diagnostics
build_gpp/test_solution_io
build_gpp/test_scenario_split_utils
build_gpp/test_static_dpv_benchmark
build_gpp/test_cumulative_propagation_graph
build_gpp/test_greedy_heuristics
build_gpp/test_warm_start
build_gpp/test_batch_experiment_config
build_gpp/test_experiment_aggregator
build_gpp/test_experiment_manifest
build_gpp/test_batch_summary_reporter
build_gpp/test_runtime_profiler
build_gpp/test_shared_splits
build_gpp/test_benders_cut
build_gpp/test_dpv_subproblem_structure
build_gpp/test_dpv_benders_small
```

## Optional CPLEX Build

The default build is CPLEX-free. To build the FPP-SAA and DPV-SAA solver paths,
configure CPLEX include and library paths and pass `--with-cplex`.

On a typical Linux CPLEX Studio install:

```bash
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex
```

Alternatively, set explicit paths:

```bash
CPLEX_INCLUDE_DIR=/path/to/cplex/include \
CPLEX_CONCERT_INCLUDE_DIR=/path/to/concert/include \
CPLEX_LIB_DIR=/path/to/cplex/lib/x86-64_linux/static_pic \
CPLEX_CONCERT_LIB_DIR=/path/to/concert/lib/x86-64_linux/static_pic \
./build_gpp.sh --with-cplex
```

With CMake:

```bash
cmake -S . -B build -DFIREBREAK_WITH_CPLEX=ON
cmake --build build
```

If CPLEX support is not enabled, solver commands such as `solve-fpp-saa`,
`run-dpv-saa-oos`, and `run-dpv-benders-oos` exit cleanly with:

```text
CPLEX support was not enabled at build time.
```

## Smoke Test

The smoke command reads existing repository data, validates requested scenarios,
builds directed propagation graphs from message files, and writes a JSON-like
summary under `cpp_cplex_firebreak/results/` unless an explicit output path is
given.

Example from `cpp_cplex_firebreak/build`:

```bash
./firebreak_cpp smoke \
  --landscape Sub20 \
  --forest-path ../../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../../sample_test/Sub20 \
  --scenario-ids 1,2 \
  --output results/sub20_smoke_summary.json
```

The paths can also be omitted for known landscapes:

```bash
./firebreak_cpp smoke \
  --landscape Sub20 \
  --scenario-ids 1,2 \
  --output results/sub20_smoke_summary.json
```

Supported landscapes are `Sub20`, `Sub40`, and `Sub100`, using the existing
folders:

- `sample_test/data/CanadianFBP/Sub20` and `sample_test/Sub20`
- `sample_test/data/CanadianFBP/Sub40` and `sample_test/Sub40`
- `sample_test/data/CanadianFBP/Sub100` and `sample_test/Sub100`

## Scenario ID Syntax

Flags that accept scenario IDs support comma-separated IDs, inclusive ranges
with `-`, inclusive ranges with `:`, and mixtures of these forms:

```bash
--scenario-ids 1,2,3
--scenario-ids 1-5
--train-ids 1-5,10,20:25
--test-ids 101:200
```

Descending ranges such as `5-1` are rejected. Duplicate IDs are rejected during
scenario validation.

The available scenario IDs are detected from the `Messages/` folder. In the
current repository data:

- `Sub20`: `1` through `1000`
- `Sub40`: `1` through `1000`
- `Sub100`: `1` through `2000`

## Burned-Area Evaluation

The `evaluate` command evaluates a fixed firebreak set over selected scenarios.
It does not solve an optimization model.

Example:

```bash
./build_gpp/firebreak_cpp evaluate \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --scenario-ids 1,2 \
  --firebreaks 10,20,30 \
  --output results/sub20_eval_firebreaks_10_20_30.json
```

Use an empty quoted string for no firebreaks:

```bash
./build_gpp/firebreak_cpp evaluate \
  --landscape Sub20 \
  --scenario-ids 1,2 \
  --firebreaks "" \
  --output results/sub20_eval_no_firebreaks.json
```

Relative output paths are resolved under `cpp_cplex_firebreak/`, so
`results/example.json` writes to `cpp_cplex_firebreak/results/example.json`.

## Evaluation Logic

- Ignition is forced to burn.
- A selected firebreak blocks incoming propagation into that node.
- If the ignition node is selected as a firebreak, it still burns.
- Under the current convention, selecting the ignition node does not block
  outgoing arcs from that ignition; this matches the optimization convention
  where ignition is forced reachable but `y` blocks incoming propagation.
- Burned area is the number of reached nodes.
- Worst 10% burned area averages the largest
  `max(1, ceil(0.10 * number_of_scenarios))` burned counts.

## Tests

The synthetic evaluator tests do not depend on external Cell2Fire data:

```bash
./build_gpp.sh
./build_gpp/test_burned_area_evaluator
./build_gpp/test_index_mapper
./build_gpp/test_dpv_index_builder
./build_gpp/test_fpp_recourse_evaluator
./build_gpp/test_reachability_greedy_warm_start
./build_gpp/test_fpp_saa_model_structure
./build_gpp/test_fpp_cut_reachability_model
./build_gpp/test_dominator_cuts
./build_gpp/test_node_separator_min_cut
./build_gpp/test_separator_context_callback
./build_gpp/test_dpv_saa_model_structure
./build_gpp/test_graph_diagnostics
./build_gpp/test_solution_io
./build_gpp/test_scenario_split_utils
./build_gpp/test_static_dpv_benchmark
./build_gpp/test_cumulative_propagation_graph
./build_gpp/test_greedy_heuristics
./build_gpp/test_warm_start
./build_gpp/test_batch_experiment_config
./build_gpp/test_experiment_aggregator
./build_gpp/test_experiment_manifest
./build_gpp/test_batch_summary_reporter
./build_gpp/test_runtime_profiler
./build_gpp/test_shared_splits
./build_gpp/test_benders_cut
./build_gpp/test_dpv_subproblem_structure
./build_gpp/test_dpv_benders_small
./build_gpp/test_dpv_branch_benders_small
./build_gpp/test_fpp_benders_small
./build_gpp/test_fpp_branch_benders_small
./build_gpp/test_fpp_restricted_candidate_branch_benders_small
./build_gpp/test_fpp_persistent_scenario_subproblem_manager
./build_gpp/test_fpp_combinatorial_benders
./build_gpp/test_fpp_projected_llbi
./build_gpp/test_fpp_strengthening
./build_gpp/test_risk_measure
./build_gpp/test_candidate_bound_controller
./build_gpp/test_benders_coefficient_candidate_scorer
./build_gpp/test_burn_frequency_candidate_scorer
./build_gpp/test_cvar_tail_aware_benders_candidate_scorer
./build_gpp/test_cvar_tail_score_diagnostics
./build_gpp/test_restricted_candidate_manager
./build_gpp/test_restricted_candidate_cut_pool
./build_gpp/test_restricted_candidate_maintenance_tracker
```

## Optimization-Ready Instance Builder

The `build-opt-instance` command converts loaded scenarios into compact,
solver-ready indexing data. It does not build or solve a mathematical model.

Example:

```bash
./build_gpp/firebreak_cpp build-opt-instance \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --scenario-ids 1,2 \
  --alpha 0.01 \
  --output results/sub20_opt_instance_summary.json
```

The builder creates:

- a deterministic compact node index sorted by original Cell2Fire cell ID,
- eligible firebreak nodes,
- uniform scenario probabilities,
- compact arc lists for constraints of the form `x[u,s] <= x[v,s] + y[v]`,
- ignition indices,
- budget `floor(alpha * NCells)`,
- DPV successor, descendant, and product index sets for future DPV-SAA work.

Eligibility convention:

- If `Instance.available_nodes` is known and nonempty, those nodes are eligible
  for firebreak placement.
- Otherwise, eligibility falls back to the union of observed scenario graph
  nodes and ignition nodes.
- The optimization node universe always includes eligible nodes, observed graph
  nodes, and ignition nodes, so scenario arcs should be mappable.

DPV descendant convention:

- Descendants use closed reachability: each node is included in its own
  descendant set.
- Isolated nodes therefore have a singleton descendant set but no DPV product
  pairs unless they have successors.

## FPP-SAA Direct Model

The `solve-fpp-saa` command builds the optimization-ready instance and solves
the direct finite-scenario burned-area model when CPLEX support is enabled.

```bash
./build_gpp/firebreak_cpp solve-fpp-saa \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --scenario-ids 1,2 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --output results/sub20_fpp_saa_solve.json
```

Model:

- binary `y_i` for eligible firebreak nodes only,
- binary `x_i^s` for every mapped node and loaded scenario,
- objective `sum_s rho_s * sum_i x_i^s`,
- budget `sum_i y_i <= floor(alpha * NCells)`,
- ignition `x_ignition^s = 1`,
- propagation `x_u^s <= x_v^s + y_v` when `v` is eligible,
- propagation `x_u^s <= x_v^s` when `v` is not eligible.

After solving, the command evaluates the selected firebreaks with the existing
graph-based burned-area evaluator and writes both solver and evaluation results.

## Expected Burned-Area FPP-SAA Components

The expected burned-area FPP-SAA implementation now has two exact formulations
for the same unit-weight objective:

- `fpp_formulation=base`: the original direct formulation with binary `y` and
  binary scenario burn variables `x`.
- `fpp_formulation=cut`: the cut/reachability formulation with binary `y` and
  continuous scenario variables `x` for burned nodes and `q` for entrance
  reachability.

Both formulations minimize `sum_s rho_s * burned_count_s` and use unit burned
area. Node weights are not implemented. On solved instances, the final selected
firebreaks are checked with `FppRecourseEvaluator`; the evaluator objective,
absolute difference, relative difference, and validation status are written to
the result JSON/CSV.

`FppRecourseEvaluator` is the canonical reusable recourse evaluator for FPP:

- the scenario root always burns and propagates, even if selected as a
  firebreak;
- a non-root reached firebreak has `q=1` and `x=0`, and propagation stops;
- a reached untreated node has `q=1` and `x=1`;
- unreached nodes have `q=0` and `x=0`;
- outputs use compact global node indices.

The reachability-greedy warm start uses this evaluator to build a feasible
firebreak set. With the base formulation the solver receives a y-only MIP
start. With the cut/reachability formulation the solver receives a full
`y/x/q` MIP start generated from evaluator burned/reached indicators.
`enable_greedy_warm_start=true` cannot be combined with another
`warm_start_policy`; the dispatcher fails clearly rather than choosing between
two warm-start sources.

## FPP-SAA Out-Of-Sample Experiments

The `run-fpp-saa-oos` command solves FPP-SAA on training scenarios and evaluates
the same selected firebreaks on both training and test scenarios. Test scenarios
are never used for optimization.

By default this command minimizes expected burned area. It also supports direct
FPP-SAA CVaR and mean-CVaR objectives through `--risk-measure cvar` and
`--risk-measure mean-cvar`. The same FPP-only risk flags are also available on
explicit-loop `run-fpp-benders-oos`, callback `run-fpp-branch-benders-oos`, and
restricted-candidate `run-fpp-restricted-branch-benders-oos`.

Explicit split:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1-5 \
  --test-ids 6:30 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --run-id sub20_fpp_saa_explicit_smoke \
  --output-json results/experiments/sub20_fpp_saa_explicit_smoke.json \
  --output-csv results/experiments/fpp_saa_oos_results.csv
```

Generated deterministic split:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --seed 123 \
  --train-count 2 \
  --test-count 3 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --risk-measure expected \
  --run-id sub20_fpp_saa_seed123_smoke \
  --output-json results/experiments/sub20_fpp_saa_seed123_smoke.json \
  --output-csv results/experiments/fpp_saa_oos_results.csv
```

Risk aggregation options:

- `--risk-measure expected`: expected burned area, the default and legacy
  behavior.
- `--risk-measure cvar --cvar-beta 0.9`: pure CVaR of scenario burned-area
  losses.
- `--risk-measure mean-cvar --cvar-beta 0.9 --cvar-lambda 0.5`: blend
  `(1-lambda) * expected + lambda * CVaR`.

`--cvar-beta` must satisfy `0 < beta < 1`; `--cvar-lambda` must satisfy
`0 <= lambda <= 1`. The VaR-like model variable is reported as
`risk_threshold_value` and is not related to the firebreak-budget `--alpha`.

Generated splits are saved under `results/splits/`, for example:

```text
results/splits/Sub20_seed123_train2_test3_train.csv
results/splits/Sub20_seed123_train2_test3_test.csv
```

The experiment JSON and CSV rows use a standard schema with run ID, landscape,
method, alpha, budget, train/test IDs, solver status, objective, bound, MIP gap,
runtime, variable and constraint counts, selected firebreaks, optional warm-start
metadata, train/test expected burned area, train/test worst-10% burned area,
train evaluation runtime, test evaluation runtime, test scenario loading
runtime, objective metric, and this graph note:

```text
Cell2Fire scenarios are treated as directed propagation graphs.
```

Selected firebreak solutions are also exported as JSON and CSV using original
Cell2Fire node IDs as the public representation.

## FPP-SAA Explicit-Loop Benders

The `run-fpp-benders-oos` command solves the same FPP-SAA model with a
classical explicit-loop Benders decomposition. Expected burned area remains the
default objective. The command also supports FPP-only CVaR and mean-CVaR risk
aggregation at the master level through the same risk flags as direct
FPP-SAA-CVaR. It is separate from the direct `run-fpp-saa-oos` model and from
all DPV Benders solvers.

Current implementation:

- binary master `y_i` variables and continuous per-scenario `eta_s` variables,
- one LP scenario subproblem per training scenario after each master solve,
- FPP recourse objective `sum_i x_i`,
- propagation constraints matching direct FPP-SAA,
- Benders cuts in the stored form
  `eta_s >= rhs_constant + sum_i coeff_i y_i`,
- optional master risk aggregation with `risk_threshold` and
  `cvar_excess_s >= eta_s - risk_threshold`,
- optional FPP lifted lower-bound inequalities with
  `--use-lifted-lower-bounds`,
- optional added-cut export with `--export-benders-cuts`.

Example:

```bash
./build_gpp/firebreak_cpp run-fpp-benders-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4,5 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --max-iterations 20 \
  --tolerance 1e-6 \
  --risk-measure expected \
  --run-id sub20_fpp_benders_smoke \
  --output-json results/experiments/sub20_fpp_benders_smoke.json \
  --output-csv results/experiments/fpp_benders_oos_results.csv \
  --export-benders-cuts results/experiments/sub20_fpp_benders_cuts.csv
```

Risk options:

- `--risk-measure expected`: expected burned area, the default and legacy
  behavior.
- `--risk-measure cvar --cvar-beta 0.9`: pure CVaR of the FPP burned-area
  recourse losses.
- `--risk-measure mean-cvar --cvar-beta 0.9 --cvar-lambda 0.5`: mean-CVaR
  blend.

The FPP scenario subproblem and Benders cuts are unchanged under CVaR; only the
master objective and master risk variables change.

Optional FPP lifted lower-bound inequalities can be enabled with:

```text
--use-lifted-lower-bounds
--export-lifted-lower-bounds path/to/fpp_llbi.csv
```

These inequalities use optimistic downstream singleton burned-area losses and
strengthen the `eta_s` lower bounds. They are independent of the risk
aggregation and can be used with expected, CVaR, or mean-CVaR FPP-Benders.
They are disabled by default.

This is explicit-loop FPP-Benders only. Callback FPP Branch-and-Benders is
available as a separate command. FPP root user cuts are callback-only, and
scenario retention remains future work. See
`docs/PHASE24_FPP_BENDERS.md` and
`docs/PHASE30_FPP_BENDERS_CVAR.md`, and
`docs/PHASE32_FPP_CVAR_STRENGTHENING_AND_LABELS.md`.

## FPP-SAA Callback Branch-And-Benders

The `run-fpp-branch-benders-oos` command solves FPP-SAA with a callback-based
Branch-and-Benders-cut solver. It keeps the master compact with binary `y_i`
variables and continuous `eta_s` variables, then adds lazy Benders optimality
cuts at integer candidate incumbents. The FPP scenario LP subproblem is the
same burned-area recourse model used by explicit-loop FPP-Benders. Expected
burned area remains the default objective. The command also supports FPP-only
CVaR and mean-CVaR risk aggregation at the master level through
`--risk-measure`.

Example:

```bash
./build_gpp/firebreak_cpp run-fpp-branch-benders-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4,5 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --risk-measure expected \
  --run-id sub20_fpp_branch_benders_smoke \
  --output-json results/experiments/sub20_fpp_branch_benders_smoke.json \
  --output-csv results/experiments/fpp_branch_benders_oos_results.csv
```

Risk options:

- `--risk-measure expected`: expected burned area, the default and legacy
  callback behavior.
- `--risk-measure cvar --cvar-beta 0.9`: pure CVaR of the FPP burned-area
  recourse losses.
- `--risk-measure mean-cvar --cvar-beta 0.9 --cvar-lambda 0.5`: mean-CVaR
  blend.

The lazy Benders cuts and FPP scenario subproblem are unchanged under CVaR;
only the master objective and master risk variables change. Explicit-loop
FPP-Benders remains available through `run-fpp-benders-oos`.

Optional callback strengthening flags:

```text
--use-lifted-lower-bounds
--use-coverage-llbi
--use-path-llbi
--path-llbi-max-paths-per-node 8
--use-root-user-cuts
--root-user-cut-max-rounds 1
--root-user-cut-tolerance 1e-6
--use-global-dominance-preprocessing
--use-conditional-zero-benefit-fixing
```

FPP LLBI rows are added before optimization. Extended CoverageLLBI adds
continuous `zeta` coverage variables; extended PathLLBI adds continuous `b`
burn lower-bound variables. These extended lower-bound modules strengthen the
same scenario loss variables `eta_s` and are compatible with expected, CVaR,
and mean-CVaR FPP Branch-and-Benders because risk aggregation only changes the
master objective and epigraph variables.

Projected LLBI variants keep the master in the original `(y, eta)` space and
do not add `zeta` or `b` variables:

```text
--use-projected-coverage-llbi-poly
--use-projected-path-llbi-poly
--use-projected-coverage-llbi-exp
--use-projected-path-llbi-exp
--projected-llbi-root-rounds 10
--projected-llbi-max-cuts-per-round 100
--projected-llbi-violation-tolerance 1e-6
--projected-llbi-cut-density-limit 0
--projected-poly-max-cuts 100000
```

`Projected*LLBI-poly` means a fixed polynomial-size projected cut subset.
`Projected*LLBI-exp` means cuts from the exponential projected valid-inequality
family, generated in practice by root LP separation. No `-sep` labels are used.
Only one extended/projected Coverage/Path LLBI family should be active at a
time unless a specific experiment has been designed for combined families.

Root user cuts are separated only at the root relaxation and then remain
globally active in CPLEX. These strengthening modules are disabled by default
unless a direct flag or method label enables them. Scenario retention remains
future work. DPV-Benders and DPV Branch-and-Benders are separate expected-value
solvers. See
`docs/PHASE25_FPP_BRANCH_AND_BENDERS.md` and
`docs/PHASE31_FPP_BRANCH_BENDERS_CVAR.md`, and
`docs/PHASE32_FPP_CVAR_STRENGTHENING_AND_LABELS.md`.

Global dominance preprocessing is static: it removes eligible candidates that
are dominated in every training scenario, preserving every scenario loss
componentwise. Conditional zero-benefit fixing is callback-only diagnostics /
safe local fixing support; it reports attempted and applied fixings when
enabled. Both write standard CSV/JSON diagnostics such as
`global_dominance_candidates_removed`,
`conditional_zero_benefit_fixings_attempted`, and
`conditional_zero_benefit_fixings_applied`.

Optional combinatorial Branch-and-Benders flags:

```text
--use-combinatorial-benders
--combinatorial-benders-lift none|posterior|heuristic
--combinatorial-benders-cut-sampling-ratio 0.10
--combinatorial-benders-separate-fractional true|false
--combinatorial-benders-initial-cuts true|false
```

The combinatorial variant replaces LP scenario-subproblem separation with graph
search and activation-path cuts for the same scenario loss variables `eta_s`.
It is available for expected value, pure CVaR, and mean-CVaR FPP objectives.
Lazy cuts at integer candidate incumbents preserve exactness; fractional
separation is optional strengthening. The default batch labels use the adapted
I+SFH-style configuration: initial cuts enabled, fractional separation enabled,
cut sampling ratio `0.10`, and heuristic lifting. For fractional solutions,
the implementation uses non-lifted path cuts when heuristic lifting is not
asserted as a globally valid fractional cut.

## Warm Starts For CPLEX Models

`run-fpp-saa-oos` and `run-dpv-saa-oos` accept an optional warm-start solution
CSV through:

```text
--warm-start-solution path/to/solution.csv
```

The CSV must contain original Cell2Fire node IDs, for example:

```text
155,252,233,309
```

Warm-start IDs are mapped to compact eligible node indices in the training
optimization instance. Duplicate IDs are removed after the first occurrence,
unmapped or ineligible IDs are reported and ignored, and too many valid IDs are
trimmed to the firebreak budget in file order. Fewer than `B` valid IDs are used
as a partial MIP start. Missing warm-start files fail clearly before solving.

The warm start does not change the FPP-SAA or DPV-SAA mathematical objective,
constraints, selected budget, or out-of-sample evaluation logic; it can only
affect solver behavior.

For the base FPP-SAA formulation and DPV-SAA, the implementation passes a
y-only CPLEX MIP start. For `fpp_formulation=cut`, the cut/reachability model
uses a full start: selected/unselected `y` values plus scenario `x` burn values
and `q` entrance-reached values. The `x/q` values are generated by
`FppRecourseEvaluator`, so the start follows the same propagation convention as
post-solve validation:

- the root always has `q=1` and `x=1`, even if selected as a firebreak;
- a reached non-root firebreak has `q=1` and `x=0`;
- a reached untreated node has `q=1` and `x=1`;
- unreached nodes have `q=0` and `x=0`;
- non-eligible nodes have no `y` value but still receive `x/q` values.

Example FPP-SAA warm start:

```bash
./build_gpp/firebreak_cpp run-fpp-saa-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4,5 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --warm-start-solution results/experiments/sub20_greedy_dpv3_for_warm_start_solution.csv \
  --run-id sub20_fpp_saa_warm_greedy_dpv3_smoke \
  --output-json results/experiments/sub20_fpp_saa_warm_greedy_dpv3_smoke.json \
  --output-csv results/experiments/fpp_saa_oos_results.csv
```

Example DPV-SAA warm start:

```bash
./build_gpp/firebreak_cpp run-dpv-saa-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4,5 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --warm-start-solution results/experiments/sub20_greedy_dpv3_for_warm_start_solution.csv \
  --run-id sub20_dpv_saa_warm_greedy_dpv3_smoke \
  --output-json results/experiments/sub20_dpv_saa_warm_greedy_dpv3_smoke.json \
  --output-csv results/experiments/dpv_saa_oos_results.csv
```

## Static-DPV Out-Of-Sample Benchmarks

The `run-static-dpv-oos` command computes a precomputed graph-based DPV score
on training scenarios only, selects the highest-scoring eligible nodes, and
evaluates that fixed solution on train and test directed propagation graphs. It
does not require CPLEX and is not the solution-dependent DPV-SAA optimization
model.

Phase 7 Static-DPV definition uses unit weights:

```text
DPV_s(i) = out_degree_s(i) * |Reach_s(i)|
DPV(i) = sum_s rho_s * DPV_s(i)
```

`Reach_s(i)` is the closed downstream reachable set in the directed propagation
graph, including `i`. Tie-breaking is deterministic: larger score first, then
smaller original Cell2Fire node ID.

The `run-static-dpv-mip-oos` command adds the separate `Static-DPV-MIP`
benchmark inspired by the published static downstream-value MIP structure. It
does not replace `Static-DPV`. For the current unit-weight experiments it uses:

```text
DPV_s(i) = |Reach_s(i)|
DPV(i) = sum_s rho_s * DPV_s(i)
max sum_i DPV(i) y_i
s.t. sum_i y_i <= budget, y_i binary
```

The default implementation has `treatment_loss(i) = 0`, so EMPC and
treatment-loss constraints are not enabled. Since the initial model has only a
cardinality constraint, it is solved exactly by deterministic top-budget
sorting: larger DPV first, then smaller original Cell2Fire node ID. The output
objective metric is `static_DPV_MIP_unit_downstream_value`.

Explicit split:

```bash
./build_gpp/firebreak_cpp run-static-dpv-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1-5 \
  --test-ids 6:30 \
  --alpha 0.01 \
  --run-id sub20_static_dpv_explicit_smoke \
  --output-json results/experiments/sub20_static_dpv_explicit_smoke.json \
  --output-csv results/experiments/static_dpv_oos_results.csv
```

Generated deterministic split:

```bash
./build_gpp/firebreak_cpp run-static-dpv-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --seed 123 \
  --train-count 2 \
  --test-count 3 \
  --alpha 0.01 \
  --run-id sub20_static_dpv_seed123_smoke \
  --output-json results/experiments/sub20_static_dpv_seed123_smoke.json \
  --output-csv results/experiments/static_dpv_oos_results.csv
```

Static-DPV-MIP generated deterministic split:

```bash
./build_gpp/firebreak_cpp run-static-dpv-mip-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --seed 123 \
  --train-count 2 \
  --test-count 3 \
  --alpha 0.01 \
  --run-id sub20_static_dpv_mip_seed123_smoke \
  --output-json results/experiments/sub20_static_dpv_mip_seed123_smoke.json \
  --output-csv results/experiments/static_dpv_mip_oos_results.csv
```

## Greedy Out-Of-Sample Heuristics

The `run-greedy-oos` command builds a cumulative directed propagation graph
from training scenarios, selects budgeted firebreaks with a greedy metric, and
evaluates the fixed solution on train and test scenarios. It does not require
CPLEX.

Supported metric values:

- `DPV3`
- `DPV2`
- `Betweenness`
- `Closeness`

Cumulative graph construction:

- each directed arc observed in at least one training scenario appears once,
- each arc has frequency weight equal to the number of training scenarios in
  which it appears,
- selected firebreak nodes are blocked for later scoring,
- scores are recomputed from scratch after each greedy selection,
- tie-breaking uses larger score first, then smaller original Cell2Fire node ID.

Metric formulas:

- `DPV3`: `outgoing_frequency_sum(i) * sum_{k reachable from i} 1 / (1 + dist_inv(i,k))`,
  where `dist_inv` uses shortest paths with arc cost `1 / frequency`.
- `DPV2`: `outgoing_frequency_sum(i) * closed_reachable_count(i)`.
- `Betweenness`: unweighted directed Brandes betweenness on the active
  cumulative graph.
- `Closeness`: `reachable_count(i) / sum_distance(i,k)` using unweighted
  directed distances over reachable nodes.

Explicit split:

```bash
./build_gpp/firebreak_cpp run-greedy-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1-5 \
  --test-ids 6:30 \
  --alpha 0.01 \
  --metric DPV3 \
  --run-id sub20_greedy_dpv3_explicit_smoke \
  --output-json results/experiments/sub20_greedy_dpv3_explicit_smoke.json \
  --output-csv results/experiments/greedy_oos_results.csv
```

Generated deterministic split:

```bash
./build_gpp/firebreak_cpp run-greedy-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --seed 123 \
  --train-count 2 \
  --test-count 3 \
  --alpha 0.01 \
  --metric Betweenness \
  --run-id sub20_greedy_betweenness_seed123_smoke \
  --output-json results/experiments/sub20_greedy_betweenness_seed123_smoke.json \
  --output-csv results/experiments/greedy_oos_results.csv
```

Smoke-run comparisons are for plumbing validation only. Scientific conclusions
require larger controlled experiments.

## DPV-SAA Out-Of-Sample Experiments

The `run-dpv-saa-oos` command mirrors the FPP-SAA train/test workflow. It
solves the solution-dependent DPV surrogate on training scenarios and evaluates
the selected firebreaks on both training and test directed propagation graphs
using burned-area metrics. Test scenarios are never used for optimization.

Explicit split:

```bash
./build_gpp/firebreak_cpp run-dpv-saa-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1-5 \
  --test-ids 6:30 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --run-id sub20_dpv_saa_explicit_smoke \
  --output-json results/experiments/sub20_dpv_saa_explicit_smoke.json \
  --output-csv results/experiments/dpv_saa_oos_results.csv
```

Generated deterministic split:

```bash
./build_gpp/firebreak_cpp run-dpv-saa-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --seed 123 \
  --train-count 2 \
  --test-count 3 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --run-id sub20_dpv_saa_seed123_smoke \
  --output-json results/experiments/sub20_dpv_saa_seed123_smoke.json \
  --output-csv results/experiments/dpv_saa_oos_results.csv
```

DPV-SAA formulation notes:

- binary `y_i` for eligible firebreak nodes only,
- binary `x_i^s` for every mapped node and loaded training scenario,
- continuous `z_{j,k}^s` variables in `[0,1]` for DPV product pairs,
- objective metric `solution_dependent_DPV_unit_weights`,
- unit cell weights are used in the Phase 6 DPV objective,
- DPV product-pair multiplicity is preserved,
- DPV descendants are reachability sets in directed propagation graphs.

Smoke-run comparisons against FPP-SAA are for plumbing validation only.
Scientific conclusions require larger controlled experiments.

## DPV-SAA Classical Benders Out-Of-Sample Experiments

### DPV-SAA Benders solver

The `run-dpv-benders-oos` command mirrors the DPV-SAA train/test workflow but
solves the training problem with a classical iterative Benders loop:

1. solve the master MIP with binary `y_i` and continuous `eta_s`,
2. fix `y` to the incumbent master solution,
3. solve one LP scenario subproblem per training scenario,
4. add violated Benders optimality cuts,
5. repeat until no violated cuts or the iteration/time limit is reached.

Current implementation:

- classical iterative master-subproblem loop,
- no callbacks,
- no lazy constraints,
- one LP scenario subproblem per training scenario,
- scenario-wise eta variables,
- optimality cuts generated from y-copy duals,
- detailed per-iteration diagnostics in JSON output,
- optional added-cut CSV export for debugging,
- optional lifted lower-bound inequalities behind an explicit flag.

Useful debugging flags:

- `--max-iterations`
- `--tolerance`
- `--output-json`
- `--output-csv`
- `--export-benders-cuts`
- `--use-lifted-lower-bounds`
- `--export-lifted-lower-bounds`

Phase 17 adds lifted lower-bound inequalities only as an opt-in strengthening
option. The coefficients use optimistic downstream singleton values: selecting
node `i` is treated as removing the closed downstream reachable set from `i`,
even when a DAG has alternate paths that could still burn those descendants in
the true singleton recourse. On propagation trees this matches the true
singleton value; on DAGs it is intentionally lower than or equal to the true
singleton value. This keeps the inequality conservative. DPV-CVaR and scenario
retention remain intentionally postponed.

The LLBI implementation precomputes scenario structural data only when
`--use-lifted-lower-bounds` is passed: adjacency, empty-fire burned nodes,
active empty-fire DPV product records, and product incidence lists by compact
node are reused while computing all coefficients. Default DPV-Benders runs do
not build this LLBI data.

See `docs/PHASE14_SUMMARY.md` for the Phase 14 instrumentation checkpoint and
validation results. See
`docs/PHASE17_LIFTED_LOWER_BOUND_INEQUALITIES.md` for the optional lifted
lower-bound implementation and validation notes.

Explicit split:

```bash
./build_gpp/firebreak_cpp run-dpv-benders-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4,5 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --max-iterations 20 \
  --tolerance 1e-6 \
  --run-id sub20_dpv_benders_explicit_smoke \
  --output-json results/experiments/sub20_dpv_benders_explicit_smoke.json \
  --output-csv results/experiments/dpv_benders_oos_results.csv \
  --export-benders-cuts results/experiments/sub20_dpv_benders_explicit_cuts.csv
```

Optional lifted lower-bound smoke runs add:

```bash
  --use-lifted-lower-bounds \
  --export-lifted-lower-bounds results/experiments/sub20_dpv_benders_llbi.csv
```

The master objective is `sum_s rho_s eta_s`. Scenario subproblems use continuous
`x`, `z`, and `y_copy` variables, fix `y_copy_i = ybar_i`, and minimize the
unweighted scenario DPV contribution. CPLEX equality-row duals from
`y_copy_i = ybar_i` are used directly in cuts:

```text
eta_s >= Q_s(ybar) + sum_i pi_i * (y_i - ybar_i)
```

JSON output includes a `benders` block with summary diagnostics and the full
iteration log. CSV output includes only compact Benders summary fields.

### DPV-SAA Branch-and-Benders-cut solver

The `run-dpv-branch-benders-oos` command runs a callback-based
Branch-and-Benders-cut variant for the same DPV-SAA expected-value model. It is
separate from `run-dpv-benders-oos`; the explicit-loop solver remains the
validated comparison baseline.

Current implementation:

- CPLEX generic candidate callback,
- lazy Benders optimality cuts at integer incumbents,
- optional root-node fractional Benders user cuts only when
  `--use-root-user-cuts` is passed,
- no non-root fractional-node user cuts,
- one LP scenario subproblem per training scenario when a candidate is checked,
- same Benders cut sign convention as the explicit-loop solver,
- optional lifted lower-bound inequalities only when
  `--use-lifted-lower-bounds` is passed,
- JSON callback diagnostics under `branch_benders`.

Root user cuts are disabled by default. Lazy integer-incumbent Benders cuts
remain the exactness mechanism, so the baseline Branch-and-Benders behavior is
recovered when `--use-root-user-cuts` is absent. Phase 21 separates fractional
cuts only at the root node and caps separation with `--root-user-cut-max-rounds`
(`1` by default). `--root-user-cut-tolerance` overrides the root separation
tolerance; otherwise it uses the Benders tolerance. Scenario retention,
FPP-specific full-node fractional user cuts, and non-root fractional user cuts
remain future work. DPV-CVaR is unsupported and out of scope.

Callback smoke command:

```bash
./build_gpp/firebreak_cpp run-dpv-branch-benders-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1,2 \
  --test-ids 3,4,5 \
  --alpha 0.01 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --run-id sub20_dpv_branch_benders_smoke \
  --output-json results/experiments/sub20_dpv_branch_benders_smoke.json \
  --output-csv results/experiments/dpv_branch_benders_oos_results.csv
```

For the Sub20 smoke split, the callback solver matches the monolithic DPV-SAA
and explicit-loop DPV-Benders objective `463.500000` and selects firebreaks
`154, 174, 290, 328`.

Optional root-user-cut smoke runs add:

```bash
  --use-root-user-cuts \
  --root-user-cut-max-rounds 1
```

For batch and manifest experiments, the callback Branch-and-Benders variants
are requested with stable method labels:

- `DPV-Branch-Benders`: baseline callback solver, no optional strengthening.
- `DPV-Branch-Benders-LLBI`: adds corrected optimistic lifted lower-bound
  inequalities to the master.
- `DPV-Branch-Benders-RootCuts`: adds root-only fractional Benders user cuts
  with `root_user_cut_max_rounds=1` unless configured otherwise.
- `DPV-Branch-Benders-LLBI-RootCuts`: enables both optional strengthening
  modules.

Root user cuts are generated only at the root but remain globally active after
being added. Lazy integer-incumbent Benders cuts remain the exactness mechanism
for every variant. These labels are only option mappings around the existing
callback solver; they do not change the baseline behavior when optional labels
are not used.

See `docs/PHASE18_BRANCH_AND_BENDERS_CUT.md` for the Phase 18 implementation
and validation notes. See `docs/PHASE21_ROOT_FRACTIONAL_USER_CUTS.md` for the
root-only fractional user-cut implementation and validation notes. See
`docs/PHASE23_DPV_BRANCH_BENDERS_VARIANT_LABELS.md` for the batch/manifest
variant labels.

## Official Method Labels

Batch and manifest experiments should use these stable labels.

Exact FPP methods:

- `FPP-SAA`: direct monolithic expected-burned-area FPP-SAA model, exact,
  command `run-fpp-saa-oos`.
- `FPP-SAA-CVaR`: direct monolithic FPP-SAA with pure burned-area CVaR
  aggregation, exact, command `run-fpp-saa-oos`.
- `FPP-SAA-MeanCVaR`: direct monolithic FPP-SAA with mean-CVaR burned-area
  aggregation, exact, command `run-fpp-saa-oos`.
- `FPP-Benders`: explicit-loop FPP-SAA Benders decomposition, exact, command
  `run-fpp-benders-oos`.
- `FPP-Benders-CVaR`: explicit-loop FPP-Benders with pure burned-area CVaR
  aggregation, exact, command `run-fpp-benders-oos`.
- `FPP-Benders-MeanCVaR`: explicit-loop FPP-Benders with mean-CVaR
  burned-area aggregation, exact, command `run-fpp-benders-oos`.
- `FPP-Branch-Benders`: callback Branch-and-Benders-cut for FPP-SAA, exact,
  command `run-fpp-branch-benders-oos`.
- `FPP-Branch-Benders-CVaR`: callback FPP Branch-and-Benders with pure
  burned-area CVaR aggregation, exact, command `run-fpp-branch-benders-oos`.
- `FPP-Branch-Benders-MeanCVaR`: callback FPP Branch-and-Benders with
  mean-CVaR burned-area aggregation, exact, command
  `run-fpp-branch-benders-oos`.
- `FPP-Branch-Benders-LLBI`: expected-value callback FPP Branch-and-Benders
  with optional FPP lifted lower-bound inequalities enabled.
- `FPP-Branch-Benders-RootCuts`: expected-value callback FPP Branch-and-Benders
  with root-only fractional Benders user cuts enabled.
- `FPP-Branch-Benders-LLBI-RootCuts`: expected-value callback FPP
  Branch-and-Benders with both FPP strengthening modules enabled.
- `FPP-Branch-Benders-CVaR-LLBI`: pure-CVaR callback FPP Branch-and-Benders
  with optional FPP lifted lower-bound inequalities enabled.
- `FPP-Branch-Benders-CVaR-RootCuts`: pure-CVaR callback FPP
  Branch-and-Benders with root-only fractional Benders user cuts enabled.
- `FPP-Branch-Benders-CVaR-LLBI-RootCuts`: pure-CVaR callback FPP
  Branch-and-Benders with both FPP strengthening modules enabled.
- `FPP-Branch-Benders-MeanCVaR-LLBI`: mean-CVaR callback FPP
  Branch-and-Benders with FPP lifted lower-bound inequalities enabled.
- `FPP-Branch-Benders-MeanCVaR-RootCuts`: mean-CVaR callback FPP
  Branch-and-Benders with root-only fractional Benders user cuts enabled.
- `FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts`: mean-CVaR callback FPP
  Branch-and-Benders with standard LLBI and root-only user cuts enabled.
- `FPP-Branch-Benders-<Risk>-CoverageLLBI`,
  `FPP-Branch-Benders-<Risk>-PathLLBI`,
  `FPP-Branch-Benders-<Risk>-CoverageLLBI-PathLLBI`, and
  `FPP-Branch-Benders-<Risk>-LLBI-CoverageLLBI-PathLLBI`: callback FPP
  Branch-and-Benders labels for extended CoverageLLBI, extended PathLLBI, and
  combined static lower-bound families. `<Risk>` is omitted for expected value
  or is `CVaR` / `MeanCVaR`. A `-RootCuts` suffix enables root user cuts.
- `FPP-Branch-Benders-<Risk>-ProjectedCoverageLLBI-poly`,
  `FPP-Branch-Benders-<Risk>-ProjectedPathLLBI-poly`,
  `FPP-Branch-Benders-<Risk>-ProjectedCoverageLLBI-exp`, and
  `FPP-Branch-Benders-<Risk>-ProjectedPathLLBI-exp`: callback FPP
  Branch-and-Benders labels for projected lower-bound families in the original
  `(y, eta)` space. A `-RootCuts` suffix enables root user cuts.
- `FPP-Branch-Benders-Combinatorial`: callback FPP Branch-and-Benders using
  graph-search combinatorial Benders separation with expected burned-area
  aggregation.
- `FPP-Branch-Benders-Combinatorial-CVaR`: callback FPP combinatorial
  Branch-and-Benders with pure burned-area CVaR aggregation.
- `FPP-Branch-Benders-Combinatorial-MeanCVaR`: callback FPP combinatorial
  Branch-and-Benders with mean-CVaR burned-area aggregation.
- `FPP-Restricted-Branch-Benders`: restricted-candidate callback FPP
  Branch-and-Benders in exact mode, with eventual full candidate activation.
- `FPP-Restricted-Branch-Benders-CVaR`: restricted-candidate callback FPP
  Branch-and-Benders with pure burned-area CVaR aggregation.
- `FPP-Restricted-Branch-Benders-MeanCVaR`: restricted-candidate callback FPP
  Branch-and-Benders with mean-CVaR burned-area aggregation.
- `FPP-Restricted-Branch-Benders-<Risk>-LLBI`,
  `FPP-Restricted-Branch-Benders-<Risk>-RootCuts`, and
  `FPP-Restricted-Branch-Benders-<Risk>-LLBI-RootCuts`: restricted-candidate
  FPP Branch-and-Benders with the same standard LLBI/root-cut options. `<Risk>`
  is omitted for expected value or is `CVaR` / `MeanCVaR`.
- `FPP-Restricted-Branch-Benders-Combinatorial`: restricted-candidate FPP
  Branch-and-Benders using combinatorial Benders separation.
- `FPP-Restricted-Branch-Benders-Combinatorial-CVaR`: restricted-candidate
  combinatorial FPP Branch-and-Benders with pure burned-area CVaR aggregation.
- `FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR`: restricted-candidate
  combinatorial FPP Branch-and-Benders with mean-CVaR burned-area aggregation.

For `MeanCVaR` method labels, the manifest default is `cvar_lambda=0.5` when
no explicit `cvar_lambda` is provided. Pure `CVaR` labels use
`cvar_lambda=1.0`. All FPP CVaR labels use `cvar_beta=0.9` unless a manifest or
CLI option overrides it.

Exact DPV methods:

- `DPV-SAA`: direct monolithic solution-dependent DPV-SAA model, exact,
  command `run-dpv-saa-oos`.
- `DPV-Benders`: explicit-loop DPV-SAA Benders decomposition, exact, command
  `run-dpv-benders-oos`. Optional LLBI exists for the direct command but is not
  enabled by this batch label.
- `DPV-Branch-Benders`: baseline callback Branch-and-Benders-cut for DPV-SAA,
  exact, no optional strengthening.
- `DPV-Branch-Benders-LLBI`: exact callback DPV Branch-and-Benders with
  corrected optimistic lifted lower-bound inequalities enabled.
- `DPV-Branch-Benders-RootCuts`: exact callback DPV Branch-and-Benders with
  root-only fractional Benders user cuts enabled. Lazy incumbent cuts remain
  the exactness mechanism.
- `DPV-Branch-Benders-LLBI-RootCuts`: exact callback DPV Branch-and-Benders
  with both optional strengthening modules enabled.

Benchmark and heuristic methods:

- `Static-DPV`: static graph-score benchmark, not an exact optimizer.
- `Static-DPV-MIP`: static downstream-value MIP benchmark solved exactly by
  deterministic top-budget sorting for the current pure-cardinality model.
- `Greedy-DPV3`: greedy heuristic using the DPV3 metric.
- `Greedy-DPV2`: greedy heuristic using the DPV2 metric.
- `Greedy-Betweenness`: greedy betweenness-centrality heuristic.
- `Greedy-Closeness`: greedy closeness-centrality heuristic.

Supported aliases are normalized only for convenience, for example
`dpv_saa`, `fpp_saa_benders`, `dpv_callback_benders_llbi`, and underscore
variants of the official labels. Result tables and manifests should use the
official labels above. Postponed method families include scenario retention /
partial extensive masters and any non-root LP-based FPP fractional user-cut
variants. FPP LLBI, FPP root user cuts, and combinatorial Benders separation
are optional strengthening/algorithm modules, disabled by default unless a
method label enables them. CVaR variants are supported only for FPP methods;
DPV methods remain expected-value methods.

## Primary Experiment Candidate Methods

The full supported method set is intentionally larger than the recommended
first-pass experiment set. For future scientific experiments, start with this
smaller candidate set unless a specific ablation needs another label.

FPP candidates:

- `FPP-SAA`
- `FPP-SAA-CVaR`
- `FPP-SAA-MeanCVaR`
- `FPP-Branch-Benders`
- `FPP-Branch-Benders-CVaR`
- `FPP-Branch-Benders-MeanCVaR`
- `FPP-Branch-Benders-RootCuts`
- `FPP-Branch-Benders-Combinatorial`
- `FPP-Branch-Benders-Combinatorial-CVaR`
- `FPP-Branch-Benders-Combinatorial-MeanCVaR`
- `FPP-Branch-Benders-CVaR-LLBI`
- `FPP-Branch-Benders-CVaR-LLBI-RootCuts`
- `FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts`
- `FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts`
- `FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts`
- `FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts`
- `FPP-Restricted-Branch-Benders`
- `FPP-Restricted-Branch-Benders-CVaR`
- `FPP-Restricted-Branch-Benders-MeanCVaR`
- `FPP-Restricted-Branch-Benders-Combinatorial`
- `FPP-Restricted-Branch-Benders-Combinatorial-CVaR`
- `FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR`

DPV candidates:

- `DPV-SAA`
- `DPV-Branch-Benders`
- `DPV-Branch-Benders-LLBI`
- `DPV-Branch-Benders-LLBI-RootCuts`

Benchmark candidates:

- `Static-DPV`
- `Static-DPV-MIP`
- `Greedy-DPV3`

Development/reference methods remain supported but should not be included in
main tables by default unless a specific ablation needs them: explicit-loop
Benders labels, extended CoverageLLBI/PathLLBI combinations, restricted
projected variants, `Greedy-DPV2`, `Greedy-Betweenness`, and
`Greedy-Closeness`.

Reference manifests were added:

```text
config/final_candidate_methods_fpp_smoke.txt
config/final_candidate_methods_dpv_smoke.txt
```

## Risk-Measure Support

Expected value remains the default optimization objective. Phase 28 added
empirical VaR/CVaR utilities and reporting fields for selected solutions.
Phase 29 implements CVaR and mean-CVaR optimization for direct
`run-fpp-saa-oos`; Phase 30 implements the same FPP-only risk aggregation for
explicit-loop `run-fpp-benders-oos`; Phase 31 implements it for callback
`run-fpp-branch-benders-oos`. Phase 32 adds official FPP CVaR method labels,
FPP LLBI, and callback FPP root user cuts. The combinatorial FPP
Branch-and-Benders labels use the same risk variables and scenario loss
variables as the LP-based callback solver.

Current risk options for `run-fpp-saa-oos`, `run-fpp-benders-oos`,
`run-fpp-branch-benders-oos`, and
`run-fpp-restricted-branch-benders-oos`:

```text
--risk-measure expected|cvar|mean-cvar
--cvar-beta 0.9
--cvar-lambda 1.0
```

DPV-CVaR is not planned; DPV methods remain expected-value methods.

## Batch Out-Of-Sample Experiments

The `run-batch-oos` command runs multiple methods over the same deterministic
train/test splits. It is intended for controlled comparisons across alpha
values, training scenario counts, random cases, and method lists. Test scenarios
are never used for optimization or scoring.

Supported method names:

- `FPP-SAA`
- `FPP-SAA-CVaR`
- `FPP-SAA-MeanCVaR`
- `FPP-Benders`
- `FPP-Benders-CVaR`
- `FPP-Benders-MeanCVaR`
- `FPP-Branch-Benders`
- `FPP-Branch-Benders-CVaR`
- `FPP-Branch-Benders-MeanCVaR`
- `FPP-Branch-Benders-Combinatorial`
- `FPP-Branch-Benders-Combinatorial-CVaR`
- `FPP-Branch-Benders-Combinatorial-MeanCVaR`
- `FPP-Branch-Benders-LLBI`
- `FPP-Branch-Benders-RootCuts`
- `FPP-Branch-Benders-LLBI-RootCuts`
- `FPP-Branch-Benders-CVaR-LLBI`
- `FPP-Branch-Benders-CVaR-RootCuts`
- `FPP-Branch-Benders-CVaR-LLBI-RootCuts`
- `FPP-Branch-Benders-MeanCVaR-LLBI`
- `FPP-Branch-Benders-MeanCVaR-RootCuts`
- `FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts`
- `FPP-Branch-Benders-CoverageLLBI`
- `FPP-Branch-Benders-PathLLBI`
- `FPP-Branch-Benders-CoverageLLBI-PathLLBI`
- `FPP-Branch-Benders-LLBI-CoverageLLBI-PathLLBI`
- the corresponding `CVaR` and `MeanCVaR` extended CoverageLLBI/PathLLBI
  labels, with optional `-RootCuts` suffixes.
- `FPP-Branch-Benders-ProjectedCoverageLLBI-poly`
- `FPP-Branch-Benders-ProjectedPathLLBI-poly`
- `FPP-Branch-Benders-ProjectedCoverageLLBI-exp`
- `FPP-Branch-Benders-ProjectedPathLLBI-exp`
- the corresponding `CVaR` and `MeanCVaR` projected LLBI labels, with optional
  `-RootCuts` suffixes.
- `FPP-Restricted-Branch-Benders`
- `FPP-Restricted-Branch-Benders-CVaR`
- `FPP-Restricted-Branch-Benders-MeanCVaR`
- `FPP-Restricted-Branch-Benders-Combinatorial`
- `FPP-Restricted-Branch-Benders-Combinatorial-CVaR`
- `FPP-Restricted-Branch-Benders-Combinatorial-MeanCVaR`
- `DPV-SAA`
- `DPV-Benders`
- `Static-DPV`
- `Static-DPV-MIP`
- `Greedy-DPV3`
- `Greedy-DPV2`
- `Greedy-Betweenness`
- `Greedy-Closeness`
- `DPV-Branch-Benders`
- `DPV-Branch-Benders-LLBI`
- `DPV-Branch-Benders-RootCuts`
- `DPV-Branch-Benders-LLBI-RootCuts`

Optional manifest keys `cvar_beta` and `cvar_lambda` configure FPP CVaR method
labels. A conflicting `risk_measure` key fails clearly. DPV methods accept only
absent or expected risk configuration; DPV-CVaR labels are unsupported.

The combinatorial FPP method labels enable the I+SFH-style defaults:
`combinatorial_benders_lift=heuristic`,
`combinatorial_benders_cut_sampling_ratio=0.10`,
`combinatorial_benders_separate_fractional=true`, and
`combinatorial_benders_initial_cuts=true`. The same options can also be set
directly with CLI flags.

Projected FPP Branch-and-Benders labels enable exactly one projected family and
one projected variant. CSV/JSON output records:

```text
projected_llbi_family=coverage|path|none
projected_llbi_strategy=poly|exp|none
projected_llbi_cuts_added
projected_llbi_total_nonzeros
projected_llbi_root_bound_initial
projected_llbi_root_bound_final
projected_llbi_root_bound_improvement_abs
projected_llbi_root_bound_improvement_pct
```

Projected rows should have `coverage_llbi_num_zeta_vars=0` and
`path_llbi_num_b_vars=0`. Extended CoverageLLBI/PathLLBI rows may create those
auxiliary variables.
Projected and combinatorial options can also be set explicitly in manifests or
through the corresponding `run-batch-oos` flags.

Solver methods still require a CPLEX-enabled build. Static-DPV and greedy
methods do not require CPLEX.

Small smoke batch:

```bash
./build_gpp/firebreak_cpp run-batch-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --alphas 0.01 \
  --train-counts 2 \
  --test-count 3 \
  --num-cases 2 \
  --seed-base 123 \
  --methods FPP-SAA,DPV-SAA,Static-DPV,Greedy-DPV3 \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --output-dir results/batch/sub20_phase10_smoke \
  --output-csv results/batch/sub20_phase10_smoke/batch_results.csv
```

Batch outputs are written under the requested output directory:

- `batch_results.csv`
- `json/*.json`
- `solutions/*_solution.json`
- `solutions/*_solution.csv`
- `splits/*_train.csv`
- `splits/*_test.csv`

The batch CSV includes experiment ID, case ID, landscape, alpha, train/test
counts, train/test IDs, method, objective, solver status, bounds, MIP gap,
runtime, selected firebreaks, train/test burned-area metrics, train/test
evaluation runtimes, test scenario loading runtime, train/test graph
classification ratios, graph note, and notes. For FPP-SAA it also includes the
mode/formulation flags, instance sizes, heuristic statistics, dominator and
separator cut statistics, MIP-start status, and post-solve recourse-validation
fields. Graph ratios use the compact format
`RT=...;ADAG=...;GDG=...;NFR=...;EMPTY=...`, where `RT` is rooted arborescence,
`ADAG` is acyclic DAG not tree, `GDG` is general directed graph, `NFR` is not
fully reachable from ignition, and `EMPTY` is empty or invalid. For CPLEX
methods, `mip_gap` is the CPLEX relative MIP gap of the incumbent solution.
This is reported even for time-limited solves when CPLEX has a feasible
incumbent and bound.

Warm-start policies can be passed with `--warm-start-policy`; the default is
`none`. Supported policies are `none`, `greedy-dpv3-for-fpp`,
`greedy-dpv3-for-dpv`, `static-dpv-for-fpp`, and `static-dpv-for-dpv`.

FPP formulation selection is available for FPP-SAA. The default is the current
direct model:

```text
fpp_formulation=base
enable_dominator_cuts=false
enable_separator_cuts=false
enable_greedy_warm_start=false
enable_local_search=false
```

The accepted formulation values are `base` and `cut`. `base` routes to the
original FPP-SAA model. `cut` routes to the exact cut/reachability formulation
with binary `y` variables and continuous `x/q` scenario variables.
`enable_dominator_cuts=true` adds limited dominator valid inequalities to
either formulation. `enable_separator_cuts=true` registers dynamic separator
user cuts through the CPLEX generic relaxation context callback for either FPP
formulation. Local search is still not implemented and fails with a clear
message when enabled. With either formulation, `enable_greedy_warm_start=true`
runs the reachability-greedy recourse heuristic. The base formulation receives
the existing y-only MIP start. The cut/reachability formulation receives the
full `y/x/q` MIP start constructed from `FppRecourseEvaluator`.

For systematic expected-burned-area comparisons, `run-batch-oos` accepts
`--fpp-modes` (or manifest key `fpp_modes`) with comma-separated mode aliases.
Each mode expands to a formulation and enhancement flag set, and local search
is always disabled:

| Mode | Formulation | Greedy | Dominator | Separator |
| --- | --- | --- | --- | --- |
| `fpp_base` | `base` | false | false | false |
| `fpp_base_greedy` | `base` | true | false | false |
| `fpp_base_dominator` | `base` | false | true | false |
| `fpp_base_separator` | `base` | false | false | true |
| `fpp_base_dominator_separator` | `base` | false | true | true |
| `fpp_base_dominator_separator_greedy` | `base` | true | true | true |
| `fpp_cut` | `cut` | false | false | false |
| `fpp_cut_greedy` | `cut` | true | false | false |
| `fpp_cut_dominator` | `cut` | false | true | false |
| `fpp_cut_separator` | `cut` | false | false | true |
| `fpp_cut_dominator_separator` | `cut` | false | true | true |
| `fpp_cut_dominator_separator_greedy` | `cut` | true | true | true |

Example tiny mode-comparison smoke:

```bash
./build_gpp/firebreak_cpp run-batch-oos \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --alphas 0.01 \
  --train-counts 2 \
  --test-count 3 \
  --num-cases 1 \
  --seed-base 123 \
  --methods FPP-SAA \
  --fpp-modes fpp_base,fpp_cut,fpp_base_dominator_separator_greedy,fpp_cut_dominator_separator_greedy \
  --time-limit 60 \
  --mip-gap 0.001 \
  --threads 1 \
  --output-dir results/batch/sub20_fpp_modes_smoke \
  --output-csv results/batch/sub20_fpp_modes_smoke/batch_results.csv \
  --rerun-existing
```

`run-fpp-saa-oos` remains the legacy direct FPP-SAA path. Use `run-batch-oos`
or manifests for variant comparisons because they carry the FPP mode fields
through CSV, JSON, resume keys, and aggregation.

Equivalent manifest snippet:

```text
methods=FPP-SAA
fpp_modes=fpp_base,fpp_cut,fpp_base_dominator_separator_greedy,fpp_cut_dominator_separator_greedy
enable_local_search=false
```

After every FPP-SAA solve with a feasible incumbent, the dispatcher evaluates
the final selected firebreaks with `FppRecourseEvaluator` and records
`evaluator_objective`, absolute and relative objective differences, and
`validation_status`. Production runs warn in notes rather than crashing if the
solver objective and evaluator objective differ beyond tolerance.

### FPP Projected LLBI Scaling Experiment

The projected LLBI scaling infrastructure compares eight FPP method families
under expected, CVaR, and mean-CVaR objectives:

```text
FPP-SAA-fpp_base
FPP-Branch-Benders-RootCuts
FPP-Branch-Benders-LLBI-RootCuts
FPP-Branch-Benders-ProjectedCoverageLLBI-poly-RootCuts
FPP-Branch-Benders-ProjectedPathLLBI-poly-RootCuts
FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-Combinatorial
```

Default grid:

```text
Sub20
train_count = 100,200,400,800
alpha = 0.01,0.02,0.03
test_count = 200
num_cases = 5
time_limit = 1800
threads = 1
```

This is `8 method families x 3 objectives x 4 train counts x 3 alphas x 5
cases = 1440` solves. The launcher splits by instance block, not by method:
one worker handles a fixed `(train_count, alpha, case_id)` and runs all 24
method/objective rows on the same train/test split. With default settings this
creates `60` workers with `24` rows each.

On a 16-core laptop, the launcher defaults to process-level parallelism:

```bash
bash scripts/run_fpp_projected_llbi_scaling_experiment.sh
```

Default parallelism is `MAX_PARALLEL_JOBS=12` and `THREADS=1`. To use 10
cores:

```bash
MAX_PARALLEL_JOBS=10 bash scripts/run_fpp_projected_llbi_scaling_experiment.sh
```

The run is resumable. Completed worker rows are skipped unless
`RERUN_EXISTING=1` is set. Outputs are written under:

```text
results/batch/fpp_projected_llbi_scaling/
  manifests/
  splits/
  workers/worker_*/
  logs/
  batch_results_all.csv
  summary_by_objective_alpha_traincount_method.csv
  method_vs_fpp_saa_base.csv
  projected_llbi_comparison.csv
  rootcuts_llbi_impact.csv
  validation_report.txt
```

Run the tiny smoke test first:

```bash
bash scripts/smoke_fpp_projected_llbi_scaling_experiment.sh
```

Detailed design notes are in
`docs/FPP_PROJECTED_LLBI_SCALING_EXPERIMENT.md`.

### FPP 12-Mode Benchmark

The moderate FPP mode benchmark compares all 12 expected-burned-area FPP-SAA
variants on shared Sub20 train/test splits:

- alpha values: `0.01,0.02,0.03`;
- train scenarios: `100`;
- test scenarios: `100`;
- cases per alpha: `5`;
- method: `FPP-SAA`;
- time limit: `600` seconds per solve;
- MIP gap: `0.001`;
- CPLEX threads: `1`.

The helper script runs one batch per alpha, writes alpha-specific logs, and then
builds comparison tables:

```bash
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex

# Sequential by default.
scripts/run_fpp_12mode_benchmark.sh

# Or process-level parallelism by alpha, while each solve still uses one CPLEX thread.
PARALLEL=1 scripts/run_fpp_12mode_benchmark.sh
```

Outputs:

- `results/batch/fpp_12mode_benchmark_alpha001/batch_results.csv`
- `results/batch/fpp_12mode_benchmark_alpha002/batch_results.csv`
- `results/batch/fpp_12mode_benchmark_alpha003/batch_results.csv`
- `results/batch/fpp_12mode_benchmark_logs/*.log`
- `results/batch/fpp_12mode_benchmark_summary.csv`
- `results/batch/fpp_12mode_benchmark_summary_by_alpha.csv`
- `results/batch/fpp_12mode_benchmark_mode_vs_base.csv`
- `results/batch/fpp_12mode_benchmark_bound_impact.csv`
- `results/batch/fpp_12mode_benchmark_root_bounds.csv`
- `results/batch/fpp_12mode_benchmark_validation_report.txt`

This is a minimization problem, so stronger formulation bounds are larger lower
bounds. The analysis reports final bound improvement as:

```text
best_bound(mode) - best_bound(fpp_base)
```

and gap reduction as:

```text
mip_gap(fpp_base) - mip_gap(mode)
```

Greedy modes should be interpreted primarily as incumbent/runtime changes, not
as formulation strengthening. Dominator and separator modes are the relevant
bound-strengthening comparisons. Root bounds are not exposed by the production
`run-batch-oos` path; the benchmark records final best bounds and writes a
root-bound availability note.

Related validation tests:

```bash
./build_gpp/test_reachability_greedy_warm_start
./build_gpp/test_fpp_cut_reachability_model
./build_gpp/test_fpp_recourse_evaluator
./build_gpp/test_batch_experiment_config
./build_gpp/test_experiment_manifest
./build_gpp/test_solution_io
```

The cut-formulation tests verify full-start values for reached firebreak nodes,
the root convention, CPLEX `addMIPStart` acceptance, and compatibility with
dominator and separator cuts. CPLEX does not expose a portable per-start
"used by optimizer" flag here, so the tests assert that the MIP start is added
successfully and the final objective still matches recourse evaluation.

### FPP LP Valid-Inequality Diagnostic

The MIP benchmark compares final incumbents, best bounds, and runtimes, but
final MIP bounds can mix formulation strength with branch-and-bound behavior.
The LP valid-inequality diagnostic isolates formulation strength by solving LP
relaxations directly. It relaxes `y` to continuous `[0,1]`; base-formulation
`x` variables, cut-formulation `x/q` variables, and all valid inequalities are
then evaluated without branching or MIP starts.

The diagnostic solves, for each alpha/case/formulation:

- `native_lp`: LP relaxation with no optional valid inequalities.
- `lp_plus_firebreak_upper_bound`: adds `x[s][i] + y[i] <= 1` where it is not
  already part of the formulation; for the cut formulation this is already
  native for eligible non-root observed nodes.
- `lp_plus_dominator`: adds the implemented static individual and aggregate
  dominator cuts under the configured per-scenario limits.
- `lp_plus_separator_offline`: solves the native LP, uses
  `SeparatorCutSeparator` directly on the fractional `xbar/ybar`, adds violated
  separator cuts offline, and resolves for up to the configured round limit.
- `lp_plus_dominator_plus_separator_offline`: adds dominator cuts first, then
  runs the same offline separator rounds.

Separator LP diagnostics deliberately do not use the CPLEX callback. This makes
the experiment deterministic and avoids the ambiguity that tiny or easy MIPs may
not invoke relaxation callbacks. Because the model minimizes expected burned
area, a larger LP objective is a stronger lower bound.

Run the diagnostic with:

```bash
./build_gpp.sh
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex
scripts/run_fpp_lp_vi_diagnostic.sh
```

The script uses `Sub20`, `train_count=100`, `test_count=100`, alphas
`0.01,0.02,0.03`, five cases per alpha, `seed_base=20260520`, and one CPLEX
thread. It writes:

- `results/batch/fpp_lp_vi_diagnostic/raw_lp_results.csv`
- `results/batch/fpp_lp_vi_diagnostic/round_results.csv`
- `results/batch/fpp_lp_vi_diagnostic/summary_by_alpha_formulation_variant.csv`
- `results/batch/fpp_lp_vi_diagnostic/vi_impact_summary.csv`
- `results/batch/fpp_lp_vi_diagnostic/separator_round_summary.csv`
- `results/batch/fpp_lp_vi_diagnostic/validation_report.txt`
- `results/batch/fpp_lp_vi_diagnostic/README_or_notes.txt`

Current caveats: the diagnostic still uses the implemented cut caps, separator
round caps, and scenario/target screening. It is an LP-bound study, not an
integer-solve benchmark, and it should not be interpreted as a runtime study.

### FPP Benders Master LP Relaxation Diagnostics

The `diagnose-fpp-master-lp` command solves LP relaxations only. It does not
start branch-and-bound, callbacks, integer search, lazy Benders cuts, or
out-of-sample evaluation. It is used to isolate the lower-bound strength and
model-size cost of Benders-master strengthening modules.

Supported variants include:

```text
fpp_saa_lp_base
master_lp_none
master_lp_llbi
master_lp_coverage_llbi
master_lp_path_llbi
master_lp_projected_coverage_poly
master_lp_projected_path_poly
master_lp_projected_coverage_exp
master_lp_projected_path_exp
```

The extended CoverageLLBI and PathLLBI variants may create `zeta` and `b`
variables. Projected variants should report zero for those variable counts.
The projected `exp` variants use root-round separation over the exponential
projected valid-inequality family; the projected `poly` variants use the fixed
polynomial-size projected subset.

Sub20 diagnostic scripts:

```bash
bash scripts/run_sub20_alpha001_002_fpp_master_lp_relaxation.sh
bash scripts/run_sub20_projected_llbi_lp_diagnostic.sh
```

Validation and analysis:

```bash
python3 scripts/validate_sub20_projected_llbi_lp_diagnostic.py \
  --results-dir results/diagnostics/sub20_alpha001_002_projected_llbi_lp_relaxation \
  --expected-rows 90 \
  --seed-base 20260601

python3 scripts/analyze_sub20_projected_llbi_lp_diagnostic.py \
  --results-dir results/diagnostics/sub20_alpha001_002_projected_llbi_lp_relaxation
```

The main interpretation rule is that this is a minimization lower-bound study:
larger LP objective values indicate stronger valid lower bounds, provided the
LP remains feasible and bounded. See
`docs/SUB20_ALPHA001_002_FPP_MASTER_LP_RELAXATION_DIAGNOSTIC.md`,
`docs/SUB20_PROJECTED_LLBI_LP_RELAXATION_DIAGNOSTIC.md`, and
`docs/PROJECTED_COVERAGE_PATH_LLBI_IMPLEMENTATION.md`.

### FPP Global Dominance Preprocessing Diagnostic

`diagnose-fpp-dominance-preprocessing` runs only the global dominance
preprocessor and exits. It does not solve an optimization model. The diagnostic
reports how many eligible firebreak candidates can be removed safely before
optimization.

Example:

```bash
./build_gpp/firebreak_cpp diagnose-fpp-dominance-preprocessing \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --train-ids 1-100 \
  --alpha 0.01 \
  --case-id case00 \
  --seed-base 20260601 \
  --seed 20260601 \
  --output-json results/diagnostics/sub20_global_dominance_case00.json \
  --output-csv results/diagnostics/sub20_global_dominance.csv
```

The Sub20 helper script reuses the alpha `0.01/0.02` instance blocks:

```bash
bash scripts/run_sub20_global_dominance_preprocessing_diagnostic.sh
python3 scripts/analyze_sub20_global_dominance_preprocessing.py \
  --results-dir results/diagnostics/sub20_alpha001_002_global_dominance_preprocessing
```

### FPP Dominator Valid Inequalities

Dominator preprocessing is available as an optional strengthening for FPP-SAA.
For each directed propagation graph, it finds eligible non-root nodes `u` that
dominate reachable nodes `v`, meaning every path from the scenario ignition to
`v` passes through `u`. When `u` is selected as a firebreak, those dominated
nodes cannot burn, so the solver can add valid inequalities involving `x[s][v]`
and `y[u]`.

Individual cut:

```text
x[s][v] + y[u] <= 1
```

Aggregate cut for a dominated set `D_s(u)`:

```text
sum_{v in D_s(u)} x[s][v] + |D_s(u)| * y[u] <= |D_s(u)|
```

Main implementation files:

- `include/cuts/DominatorCuts.hpp`
- `src/cuts/DominatorCuts.cpp`
- `tests/test_dominator_cuts.cpp`
- integration points in `FppSaaCplexModel` and `FppCutReachabilityCplexModel`

The main public pieces are `DominatorPreprocessor`,
`add_dominator_cuts_to_model`, `DominatorCutOptions`, and
`DominatorCutStats`. The model builders pass lightweight `x/y` variable
accessors to the cut generator so the same code works for the base and
cut/reachability formulations.

Configuration:

```text
enable_dominator_cuts=true
max_aggregate_dominator_cuts_per_scenario=50
max_individual_dominator_cuts_per_scenario=100
```

The default remains `enable_dominator_cuts=false`. When enabled, result JSON
and CSV include total, aggregate, and individual cut counts, DAG/fallback
scenario counts, and `dominator_preprocessing_time_sec`.

Modeling conventions:

- Dominator cuts are generated only for eligible firebreak nodes with a `y`
  variable.
- The scenario ignition/root is never used as a dominator cut source because a
  root firebreak does not block root propagation in this project.
- Reachability and dominators are computed on compact global node indices, with
  scenario-local indexing used internally only for bitsets.
- DAG scenarios use a topological-order dominator algorithm; cyclic scenarios
  fall back to an iterative fixed-point algorithm.
- Unit burned-area weights are assumed.

Run the related tests with:

```bash
./build_gpp.sh
./build_gpp/test_dominator_cuts

CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex
./build_gpp/test_dominator_cuts
./build_gpp/test_fpp_cut_reachability_model
```

Known limitations:

- Dominator cuts are static model-building cuts only.
- Cut generation is intentionally capped per scenario by the aggregate and
  individual dominator limits.
- Weighted burned-area dominator cuts are not implemented.

### FPP Separator User Cuts

Dynamic separator cuts are available as optional CPLEX user cuts for FPP-SAA.
For a scenario `s`, target node `v`, and eligible non-root node separator `C`
between the scenario ignition and `v`, the callback separates:

```text
x[s][v] + sum_{i in C} y[i] <= |C|
```

The inequality is globally valid for the FPP burn variables: if every node in
`C` is selected as a firebreak, `v` cannot burn. These cuts are added as generic
relaxation-context user cuts, not lazy constraints, because they strengthen the
LP relaxation and do not define missing integer-feasibility constraints.

Main implementation files:

- `include/graph/DinicMaxFlow.hpp`
- `src/graph/DinicMaxFlow.cpp`
- `include/cuts/NodeSeparatorMinCut.hpp`
- `src/cuts/NodeSeparatorMinCut.cpp`
- `include/cuts/SeparatorCutSeparator.hpp`
- `src/cuts/SeparatorCutSeparator.cpp`
- `include/cuts/SeparatorContextCallback.hpp`
- `src/cuts/SeparatorContextCallback.cpp`
- `tests/test_node_separator_min_cut.cpp`
- `tests/test_separator_context_callback.cpp`

The public API is `firebreak::graph::DinicMaxFlow` and
`firebreak::cuts::NodeSeparatorMinCut` for standalone min-cut separation. The
separator implementation then has two layers:

- `firebreak::cuts::SeparatorCutSeparator` is a CPLEX-independent separator
  core. It accepts artificial or callback-provided fractional `ybar/xbar`
  values, screens scenarios and targets, calls `NodeSeparatorMinCut`, checks
  violation, filters duplicate cuts, and returns candidate cuts.
- `firebreak::cuts::SeparatorContextCallback` is the CPLEX generic Context
  adapter. It extracts relaxation values, calls `SeparatorCutSeparator`, and
  adds globally valid user cuts through
  `IloCplex::Callback::Context::addUserCut`.

The direct separator-core tests are intentional: tiny CPLEX MIPs may solve in
presolve or at root without invoking a relaxation callback, so correctness of
violated-cut discovery is validated independently from CPLEX callback
scheduling. The CPLEX integration test also includes a controlled diagnostic
MIP with test-only presolve/heuristic settings to verify that the generic
relaxation callback can be invoked and can add at least one user cut.

Configuration:

```text
enable_separator_cuts=true
sep_at_root=true
sep_frequency_nodes=50
sep_max_scenarios_per_call=10
sep_max_nodes_per_scenario=20
sep_max_cuts_per_call=100
sep_min_violation=1e-5
sep_max_cut_cardinality=50
```

The default remains `enable_separator_cuts=false`. When enabled, separator
statistics are written to result JSON and CSV through `separator_cuts_added`,
`separator_min_cut_calls`, `separator_callback_invocations`,
`separator_duplicate_cuts_skipped`, `separator_large_cuts_skipped`,
`separator_time_sec`, and `max_cut_violation`.

Modeling conventions:

- Scenario arcs use compact global node indices.
- Each observed node has an entrance and exit node in the split graph.
- Eligible non-root node arcs have capacity `1 - ybar[i]`.
- Non-eligible nodes and the scenario root have finite `INF` capacity, so they
  are not returned as useful separators.
- The target node is allowed in the separator.
- Root targets are skipped because the root always burns and propagates.
- Supported formulations are `fpp_formulation=base` and `fpp_formulation=cut`.
- When separator cuts are enabled, CPLEX `Threads` is temporarily forced to `1`
  so the callback cut cache is deterministic and single-thread safe.

Run the related tests with:

```bash
./build_gpp.sh
./build_gpp/test_node_separator_min_cut
./build_gpp/test_separator_context_callback
./build_gpp/test_dominator_cuts

CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex
./build_gpp/test_node_separator_min_cut
./build_gpp/test_separator_context_callback
./build_gpp/test_fpp_cut_reachability_model
```

`test_node_separator_min_cut` verifies split-node min-cut recovery.
`test_separator_context_callback` verifies direct violated separator discovery
with artificial `xbar/ybar`, objective preservation with separator cuts enabled,
and CPLEX generic callback invocation on a controlled diagnostic MIP.

Known limitations:

- Separator cuts are user cuts only; no lazy constraints are added.
- The cut cache is per solve and not designed for multi-threaded callbacks yet.
- There is no max-flow residual reuse across callback invocations.
- Local search is still not implemented.
- It assumes unit burned-area weights and compact global node IDs.

### Shared Splits Across Alpha Values

By default, batch split files are local to the experiment output directory and
keep the historical naming convention that includes alpha. For parallel
one-alpha manifests, use shared splits so the same `case_id` and `train_count`
compare alpha values on exactly the same train/test scenario IDs.

Enable this with:

```text
shared_splits=true
split_dir=results/batch/sub20_exact_shared_splits/splits
```

When enabled, split generation depends on landscape, train count, test count,
case ID, and seed base. It does not depend on alpha. If both expected shared
split files already exist, they are loaded and validated. If neither exists,
they are generated and saved. A partial pair fails clearly.

Shared split filenames do not include alpha:

```text
Sub20_seed9000_train5_test100_case0_train.csv
Sub20_seed9000_train5_test100_case0_test.csv
```

The batch runner logs each shared split use, for example:

```text
Using shared split:
  train_count=5, case_id=0, alpha=0.01
  split files: ...
Using shared split:
  train_count=5, case_id=0, alpha=0.02
  split files: ...
```

Batch runs use resume/skip behavior by default. If the output CSV already has a
completed row for the same experiment, landscape, alpha, train count, test
count, case ID, and method, the run is skipped and reported as
`SKIPPED existing result`. Use `--rerun-existing` to force reruns. Existing JSON,
CSV, solution, and split files are not deleted.

## Batch Aggregation

The `aggregate-batch` command reads a batch CSV and writes method-level,
FPP-vs-DPV, runtime, and human-readable summaries:

```bash
./build_gpp/firebreak_cpp aggregate-batch \
  --input-csv results/batch/sub20_phase10_smoke/batch_results.csv \
  --output-dir results/batch/sub20_phase10_smoke/summary
```

Generated files:

- `summary_by_method.csv`
- `pairwise_comparison_fpp_vs_dpv.csv`
- `runtime_summary.csv`
- `summary_report.txt`

When `fpp_mode` is present in the input CSV, `summary_by_method.csv` groups
FPP-SAA rows by mode using labels such as `FPP-SAA:fpp_cut_separator`.
`pairwise_comparison_fpp_vs_dpv.csv` includes an `fpp_mode` column and writes
one FPP-vs-DPV comparison row per FPP mode for each matching case.

Small smoke batches are plumbing checks. They are not scientific evidence for
the paper hypothesis; larger controlled batches are needed before drawing
empirical conclusions.

## Manifest-Based Experiments

The `run-manifest` command reads a reproducible key-value experiment manifest,
runs the batch experiment, aggregates the CSV, and writes a human-readable
summary report.

```bash
./build_gpp/firebreak_cpp run-manifest \
  --manifest config/phase11_sub20_debug.txt
```

Manifest files use `key=value` lines. Blank lines and lines starting with `#`
are ignored.

```text
experiment_name=phase11_sub20_debug
landscape=Sub20
forest_path=../sample_test/data/CanadianFBP/Sub20
results_path=../sample_test/Sub20
alphas=0.01
train_counts=2
test_count=3
num_cases=2
seed_base=123
methods=FPP-SAA,DPV-SAA,Static-DPV,Greedy-DPV3
time_limit=60
mip_gap=0.001
threads=1
output_dir=results/batch/phase11_sub20_debug
warm_start_policy=none
shared_splits=true
split_dir=results/batch/sub20_exact_shared_splits/splits
resume=true
```

The manifest runner writes:

- `manifest_used.txt`
- `run_metadata.txt`
- `batch_results.csv`
- `summary/summary_by_method.csv`
- `summary/pairwise_comparison_fpp_vs_dpv.csv`
- `summary/runtime_summary.csv`
- `summary/summary_report.txt`
- per-method JSONs, solution files, and split files

Manifest `resume=true` is the default. It prevents duplicate completed rows on
reruns. Pass `--rerun-existing` to `run-manifest` to override this.

Example parallel alpha runs with shared splits:

```bash
./build_gpp/firebreak_cpp run-manifest --manifest config/sub20_exact_alpha001_shared.txt
./build_gpp/firebreak_cpp run-manifest --manifest config/sub20_exact_alpha002_shared.txt
./build_gpp/firebreak_cpp run-manifest --manifest config/sub20_exact_alpha003_shared.txt
```

These manifests write separate alpha-specific output directories but use the
same `results/batch/sub20_exact_shared_splits/splits` directory for split CSVs.

Included validation and calibration manifests:

- `config/phase11_sub20_debug.txt`: small validation manifest.
- `config/phase11_sub20_moderate.txt`: moderate Sub20 manifest with more cases
  and test scenarios. Monitor runtime before launching this study.
- `config/phase12_sub20_small_test_calibration.txt`: small exact-method
  calibration with reduced test count.
- `config/phase12_sub20_exact_calibration.txt`: FPP-SAA and DPV-SAA calibration
  across three alpha values and three train counts.
- `config/phase12_sub20_alpha02_methods_calibration.txt`: one-budget
  comparison across exact, static, and DPV-style greedy methods.
- `config/sub20_exact_alpha001_shared.txt`,
  `config/sub20_exact_alpha002_shared.txt`, and
  `config/sub20_exact_alpha003_shared.txt`: parallel one-alpha exact-method
  manifests that reuse shared train/test split files.

The Phase 12 calibration manifests should be monitored for runtime. They are
smaller than the Phase 11 moderate manifest but DPV-SAA can still dominate
runtime as train counts increase.

## Graph Diagnostics

The `analyze-graphs` command classifies loaded Cell2Fire scenario graphs as
rooted arborescences, DAGs that are not trees, general directed graphs,
not-fully-reachable graphs, or empty/invalid graphs. It is solver-independent
and does not require CPLEX.

```bash
./build_gpp/firebreak_cpp analyze-graphs \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --scenario-ids 1,2,3,4,5 \
  --output results/graph_diagnostics_sub20_1_5.json
```

Scenario ranges are also supported:

```bash
./build_gpp/firebreak_cpp analyze-graphs \
  --landscape Sub20 \
  --forest-path ../sample_test/data/CanadianFBP/Sub20 \
  --results-path ../sample_test/Sub20 \
  --scenario-range 1:1000 \
  --output results/graph_diagnostics_sub20_all.json
```

Diagnostics include reachability from ignition, in-degree and out-degree
structure, duplicate arcs, directed cycle detection, weak components, arc
excess, and basic edge time/ROS attribute counts. Structural graph tests use
distinct directed arcs; duplicate arcs are reported separately.

Out-of-sample experiment JSON/CSV outputs also include graph classification
ratios for the train and test scenario sets, using `RT`, `ADAG`, `GDG`, `NFR`,
and `EMPTY` abbreviations. For example, `RT=0.500000;ADAG=0.500000;...` means
half of the loaded scenario graphs were rooted arborescences and half were DAGs
that were not rooted arborescences.

## What The Smoke Test Reports

- landscape name
- forest and Cell2Fire results paths
- detected message file count
- minimum and maximum scenario IDs
- requested scenario IDs
- parsed `fuels.asc` rows, columns, and `NCells`
- available node count when it can be inferred safely
- ignition node per requested scenario
- message filename per scenario
- directed graph edge count per scenario
- observed node count per scenario
- parsing warnings

## Current Parsing Assumptions

- Message files are detected by exact filenames matching
  `MessagesFileXXXX.csv`, where `XXXX` is numeric. Temporary lock files such as
  `.~lock.MessagesFile0001.csv#` are ignored.
- Message rows use the first two numeric columns as directed arc endpoints
  `(u, v)`. The third and fourth numeric columns are stored as optional `time`
  and `ros` values when present.
- Header rows, empty rows, and non-arc rows are skipped.
- For one-column `IgnitionsHistory/ignitions_log.csv` files, the reader assumes
  the line number is the scenario ID and the value is the ignition node.
- For two-column ignition files, the reader assumes the first numeric column is
  scenario ID and the second is ignition node.
- Forest size is read from `fuels.asc` using `nrows` and `ncols`.
- Available nodes are inferred only when non-fuel values can be recognized from
  `fbp_lookup_table.csv`.

## Current Limitations

- CPLEX is optional and disabled in the default build.
- Direct FPP-SAA, direct DPV-SAA, Static-DPV, greedy heuristic baselines,
  explicit-loop FPP/DPV Benders, callback FPP/DPV Branch-and-Benders,
  restricted-candidate FPP Branch-and-Benders, and combinatorial FPP
  Branch-and-Benders are implemented.
- A callback-based DPV-SAA Branch-and-Benders-cut command is implemented for
  CPLEX builds.
- Batch out-of-sample runner and CSV aggregation are implemented.
- Manifest-based batch execution and human-readable summary reports are
  implemented.
- Batch resume/skip behavior and runtime profiling summaries are implemented.
- Scenario retention and scenario reduction models are not implemented yet.
- CVaR and mean-CVaR optimization are implemented only for FPP burned-area
  methods, including the callback, restricted-candidate, and combinatorial
  Branch-and-Benders variants. DPV-CVaR is unsupported.
- FPP LLBI and FPP root user cuts are optional strengthening modules and are
  disabled by default.
- The JSON output is written manually with standard library streams.
- Fuel parsing is intentionally minimal and only supports the data needed for
  smoke checks.
- Scenario split utilities are available in code, but there is not yet a
  dedicated split CLI command.
- FPP-SAA separator cuts use CPLEX generic Context callbacks only when
  `enable_separator_cuts=true`. DPV-SAA Benders has both the explicit-loop
  validation path and the separate Phase 18 candidate-callback lazy-cut path.
- Phase 6 DPV-SAA uses unit cell weights for the DPV surrogate.
- Phase 7 Static-DPV uses the unit-weight graph score
  `out_degree * closed reachability size`.
- Static-DPV-MIP uses the static downstream-value score `closed reachability
  size` under current unit downstream values.
- Phase 8 greedy betweenness recomputes unweighted directed Brandes centrality
  from scratch each greedy step and is slower than the other smoke heuristics.
- Base FPP-SAA and DPV-SAA warm starts are y-only CPLEX MIP starts. The
  cut/reachability FPP formulation supports full `y/x/q` starts. DPV `x/z`
  initialization is not implemented.
- Local search is intentionally not implemented.
- Separator cut caching is single-thread oriented; separator-enabled solves
  currently force CPLEX `Threads=1`.
- Separator min-cut separation rebuilds max-flow instances and does not reuse
  residual graphs across callback invocations.
- DPV methods are expected-value methods. CVaR optimization is supported only
  for FPP burned-area methods.
- Tiny or easy MIPs may solve without invoking separator relaxation callbacks;
  direct separator-core tests cover violated-cut discovery independently.
- Graph diagnostics classify the loaded message-file graph structure only; they
  do not infer a temporal propagation structure beyond the reported edge
  attributes.
