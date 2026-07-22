# C++ Stochastic Firebreak Placement - New Instances Experiment

This checkout is trimmed to run the `new_instances` FPP scaling experiment.
Historical experiment launchers, old config files, generated results, and logs
are intentionally excluded from the active `scripts/` and `config/` folders.

## Weighted Landscapes

The solver, DPV heuristics, and result/analysis pipeline all support
**non-uniform per-cell wildfire-loss weights** `w_k`, so the optimization
objective and every evaluation metric reflect loss-weighted burned area
(`sum_k w_k x_k^s(y)`), not raw burned-cell count. Three weight profiles are
supported (`homogeneous`, `heterogeneous`, `clustered`), generated
deterministically and stored in a content-addressed map registry so every
result row is traceable back to the exact weight map that produced it.

Principal entry points:

```bash
# Generate and register a weight map for one physical landscape (once, before any solve).
./build_gpp/firebreak_cpp ensure-weight-map \
    --landscape new20x20 --weight-profile heterogeneous --weight-replicate 0 \
    --weight-registry weight_maps/

# Direct exact FPP solve against that weight map.
./build_gpp/firebreak_cpp run-fpp-saa-oos \
    --landscape new20x20 --train-ids 1-8 --test-ids 21-24 --alpha 0.02 \
    --risk-measure expected --weight-map-file weight_maps/.../weights.csv \
    --output-json result.json --output-csv result.csv

# DPV surrogate optimization against the same weight map.
./build_gpp/firebreak_cpp run-dpv-saa-oos \
    --landscape new20x20 --train-ids 1-8 --test-ids 21-24 --alpha 0.02 \
    --weight-map-file weight_maps/.../weights.csv --dpv-ignition-policy fpp-safe \
    --output-json dpv_result.json --output-csv dpv_result.csv

# Full paired (reduced + reburn) manifest + worker experiment.
python3 scripts/generate_fpp_new_instances_scaling_manifests.py \
    --output-dir results/my_experiment --weight-profiles homogeneous,heterogeneous,clustered \
    --weight-registry weight_maps/ --generate-missing-weight-maps --paired-reburn-evaluation
python3 scripts/run_fpp_new_instances_scaling_manifest_worker.py \
    --worker-id worker_000 --manifest results/my_experiment/manifests/worker_000_manifest.csv \
    --binary ./build_gpp/firebreak_cpp

# Merge (Phase 9A) and analyze (Phase 9B) the results.
python3 scripts/merge_weighted_experiment_results.py \
    --input-root results/my_experiment --output-dir results/my_experiment/merged --strict
python3 scripts/analyze_weighted_experiment_results.py \
    --merged-current-valid results/my_experiment/merged/merged_current_valid.csv \
    --merged-all-attempts results/my_experiment/merged/merged_all_attempts.csv \
    --output-dir results/my_experiment/analysis
```

Full conceptual model, supported method/risk/LLBI/combinatorial coverage,
known unsupported combinations, and troubleshooting:
**[`docs/WEIGHTED_LANDSCAPES.md`](docs/WEIGHTED_LANDSCAPES.md)** (staged with
`git add -f` since `docs/` is gitignored). Legacy homogeneous experiments
require no changes — omitting every `--weight-map-file`/`--weight-profile`
flag resolves to the homogeneous profile automatically.

Known limitations (see the guide for the full list): the restricted-candidate
solver does not support LLBI/combinatorial/dominance strengthening for
non-homogeneous maps; combinatorial Benders does not combine with LLBI,
dominance, or root user cuts; conditional zero-benefit fixing is a diagnostic
detector only (applies no actual variable fixings); DPV-CVaR optimization is
not implemented.

## Required Files

The experiment launcher is:

```bash
scripts/run_fpp_new_instances_scaling_experiment.sh
```

There is also a focused 20x20 launcher used for smaller or incremental panels:

```bash
scripts/run_fpp_new_instances_scaling_experiment_sub20.sh
```

The minimal script chain is:

```text
scripts/run_fpp_new_instances_scaling_experiment.sh
scripts/run_fpp_new_instances_scaling_experiment_sub20.sh
scripts/generate_fpp_new_instances_scaling_manifests.py
scripts/run_fpp_new_instances_scaling_manifest_worker.py
scripts/merge_fpp_new_instances_scaling_experiment.py
scripts/analyze_fpp_new_instances_scaling_experiment.py
scripts/fpp_new_instances_scaling_compact_schema.py
```

The required config files are:

```text
config/fpp_new_instances_scaling_instances.csv
config/fpp_new_instances_scaling_methods.txt
```

The launcher also requires the project root markers:

```text
Makefile
src/
include/
```

and a CPLEX-enabled binary:

```text
build_gpp/firebreak_cpp
```

Build it with:

```bash
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 make cplex
```

## Required Data Layout

By default the launcher reads:

```text
new_instances/
```

Each selected instance folder must contain:

```text
new_instances/<folder>/ignition_and_weather_log.csv
new_instances/<folder>/run.log
new_instances/<folder>/Messages/MessagesFile00001.csv
...
new_instances/<folder>/Messages/MessagesFile10000.csv
```

Configured instance IDs:

```text
new20x20
new20x20_reburn
new40x40
new40x40_reburn
new100x100
new100x100_reburn
```

The current `100x100` and `100x100_reburn` folders are treated as blocked by
default unless their metadata is corrected. Diagnostics currently identify them
as likely 40x40 data: declared 10000 cells, inferred 1600 cells.

## Methods

The default method file has 21 labels:

```text
6 method families x 3 objectives + 3 eta-desc combinatorial variants = 21 methods
```

Objectives:

```text
Expected
CVaR
MeanCVaR
```

The projected LLBI variants used by this experiment are the `exp` variants
only. The old `poly` variants are not part of this experiment.

The full method file is:

```text
config/fpp_new_instances_scaling_methods.txt
```

It includes, for each objective family, the root-cut baseline, lifted lower
bound inequalities, projected coverage LLBI, projected path LLBI, and
combinatorial Benders variants.

Weighted landscape status:

- Standard FPP LLBI is weight-aware for explicit-loop and callback
  Branch-and-Benders (`docs/WEIGHTED_LANDSCAPES_PHASE6B1.md`).
- The weighted standard LLBI formula uses downstream empty-burned weighted mass,
  not exact singleton LP subproblem solves.
- Extended CoverageLLBI is weight-aware for explicit-loop and callback
  Branch-and-Benders using per-cell capped downstream coverage
  (`docs/WEIGHTED_LANDSCAPES_PHASE6B2A.md`).
- Extended PathLLBI is weight-aware for explicit-loop and callback
  Branch-and-Benders using directed simple ignition-to-node path lower bounds
  (`docs/WEIGHTED_LANDSCAPES_PHASE6B2B.md`).
- Projected CoverageLLBI is weight-aware in callback Branch-and-Benders for
  both `exp` root separation and the static `poly` subset
  (`docs/WEIGHTED_LANDSCAPES_PHASE6B3A.md`).
- Projected PathLLBI is weight-aware in callback Branch-and-Benders for both
  `exp` root separation and the static `poly` first-stored-path subset
  (`docs/WEIGHTED_LANDSCAPES_PHASE6B3B.md`).
- Combinatorial Benders is weight-aware in callback Branch-and-Benders for
  integer incumbents, binary initial cuts, and fractional path user cuts with
  `lift_mode=none`, `heuristic`, or `posterior`. Scenario ordering
  `eta-asc|eta-desc` and `cut_sampling_ratio in (0,1]` are exact for integer
  candidates through sampling-first separation with full fallback before
  candidate acceptance
  (`docs/WEIGHTED_LANDSCAPES_PHASE6C1.md`,
  `docs/WEIGHTED_LANDSCAPES_PHASE6C2A.md`,
  `docs/WEIGHTED_LANDSCAPES_PHASE6C2B.md`,
  `docs/WEIGHTED_LANDSCAPES_PHASE6C2C.md`).
- LP-dual root user cuts combined with combinatorial Benders,
  restricted-candidate combinatorial Benders, DPV, and Static-DPV remain
  blocked for non-homogeneous weights until separately validated.

Expected objective methods:

```text
FPP-SAA
FPP-Branch-Benders-RootCuts
FPP-Branch-Benders-LLBI-RootCuts
FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-Combinatorial
FPP-Branch-Benders-Combinatorial-EtaDesc
```

CVaR objective methods:

```text
FPP-SAA-CVaR
FPP-Branch-Benders-CVaR-RootCuts
FPP-Branch-Benders-CVaR-LLBI-RootCuts
FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-Combinatorial-CVaR
FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc
```

Mean-CVaR objective methods:

```text
FPP-SAA-MeanCVaR
FPP-Branch-Benders-MeanCVaR-RootCuts
FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts
FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-Combinatorial-MeanCVaR
FPP-Branch-Benders-Combinatorial-MeanCVaR-EtaDesc
```

`METHOD_FILE` can point at a custom one-method-per-line file. Blank lines and
`#` comments are ignored. When `SMOKE_METHODS=1`, the launcher ignores
`METHOD_FILE` and writes a temporary method file under
`$OUTPUT_DIR/manifests/smoke_methods.txt`; use `SMOKE_METHODS=0` whenever you
want the explicit `METHOD_FILE` to be honored.

### Combinatorial Benders Cut Sampling

The combinatorial Branch-and-Benders methods use:

```text
combinatorial_benders_cut_sampling_ratio = 0.10
```

For integer candidate callbacks this ratio controls the number of scenarios
checked in the initial deterministic sample. The realized sample size is:

```text
ceil(combinatorial_benders_cut_sampling_ratio * train_scenario_count)
```

with a minimum of one scenario. If the initial sample contains a violated
scenario, the candidate is rejected and the remaining scenarios may be deferred
for that candidate. If the initial sample contains no violation, the callback
performs a full deterministic fallback sweep over all remaining scenarios before
allowing the candidate. Thus ratios below one change verification order and may
reject some candidates earlier, but do not allow accepting an incumbent with
unchecked scenarios.

For common training counts this means:

```text
train_count = 100 -> 10 initially sampled scenarios per callback
train_count = 200 -> 20 initially sampled scenarios per callback
train_count = 400 -> 40 initially sampled scenarios per callback
train_count = 600 -> 60 initially sampled scenarios per callback
```

The code does not sample randomly. It first orders scenarios by the current
master `eta_s` value, with deterministic tie-breaking by scenario ID, then uses
the first `ceil(ratio * train_count)` scenarios as the initial sample. The
ordered fallback list preserves every omitted scenario exactly once. For CVaR
methods such as `FPP-Branch-Benders-Combinatorial-CVaR`, this parameter does
not define the CVaR tail; all scenario recourse constraints remain subject to
full verification before candidate acceptance.

The scenario ordering is configurable with:

```text
--combinatorial-benders-scenario-order eta-asc|eta-desc
```

`eta-asc` is the default and preserves the previous behavior. The official
eta-desc variants are:

```text
FPP-Branch-Benders-Combinatorial-EtaDesc
FPP-Branch-Benders-Combinatorial-CVaR-EtaDesc
FPP-Branch-Benders-Combinatorial-MeanCVaR-EtaDesc
```

Each one is identical to its corresponding base combinatorial method except
that combinatorial cuts are searched after ordering scenarios by decreasing
current `eta_s`. `eta-asc` checks smaller current recourse estimates first;
`eta-desc` checks larger current recourse estimates first. Neither ordering
changes the risk objective, scenario probabilities, cut coefficients, or the
requirement that an accepted integer candidate be fully verified.

## Default Experiment Grid

The main launcher defaults to `new40x40` with the full method file:

```text
training pool: 1..9000
fixed OOS test set: 9001..10000
instance filter: new40x40
train counts: 100,200
alphas: 0.01,0.02,0.03
cases: 5
time limit: 1800 seconds
MIP gap: 0.001
threads per solve: 1
max parallel workers: 12
rerun existing: false
smoke methods: false
```

Expected row counts:

```text
default new40x40 run: 630 rows
all six folders valid: 3780 rows
four validated folders only: 2520 rows
20x20 and 20x20_reburn only: 1260 rows
```

The focused `sub20` launcher defaults to a single `new20x20` instance and is
intended for incremental method panels:

```text
instance filter: new20x20
train counts: 400,600
alphas: 0.01,0.02,0.03
cases: 10
time limit: 1800 seconds
MIP gap: 0.001
threads per solve: 1
max parallel workers: 10
rerun existing: true
smoke methods: true
```

With `SMOKE_METHODS=1`, `sub20` uses the methods currently listed in its
temporary heredoc. In the current script this panel focuses on SAA-MeanCVaR and
the LLBI/projected-LLBI root-cut methods:

```text
FPP-SAA-MeanCVaR
FPP-Branch-Benders-LLBI-RootCuts
FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-CVaR-RootCuts
FPP-Branch-Benders-CVaR-LLBI-RootCuts
FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-MeanCVaR-RootCuts
FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts
FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp-RootCuts
```

Set `SMOKE_METHODS=0 METHOD_FILE=...` to run a custom subset instead of that
temporary list.

## Validate Manifests Without Running Solves

Four currently validated folders:

```bash
INSTANCE_FILTER=new20x20,new20x20_reburn,new40x40,new40x40_reburn \
OUTPUT_DIR=results/batch/fpp_new_instances_scaling_manifest_check \
DRY_RUN=1 \
PARALLEL=0 \
scripts/run_fpp_new_instances_scaling_experiment.sh
```

Only the 20x20 folders:

```bash
INSTANCE_FILTER=new20x20,new20x20_reburn \
OUTPUT_DIR=results/batch/fpp_new20x20_scaling_manifest_check \
DRY_RUN=1 \
PARALLEL=0 \
scripts/run_fpp_new_instances_scaling_experiment.sh
```

## Launch Experiments

Safe launch for the four currently validated folders:

```bash
INSTANCE_FILTER=new20x20,new20x20_reburn,new40x40,new40x40_reburn \
MAX_PARALLEL_JOBS=12 \
PARALLEL=1 \
scripts/run_fpp_new_instances_scaling_experiment.sh
```

Only the 20x20 folders:

```bash
INSTANCE_FILTER=new20x20,new20x20_reburn \
OUTPUT_DIR=results/batch/fpp_new20x20_scaling \
MAX_PARALLEL_JOBS=12 \
PARALLEL=1 \
scripts/run_fpp_new_instances_scaling_experiment.sh
```

After the `100x100` folders are corrected, the full launch is:

```bash
MAX_PARALLEL_JOBS=12 \
PARALLEL=1 \
scripts/run_fpp_new_instances_scaling_experiment.sh
```

Completed worker rows are skipped by default. Use `RERUN_EXISTING=1` only when
you want to force reruns.

For `run_fpp_new_instances_scaling_experiment_sub20.sh`, `RERUN_EXISTING=1` is
the default. This is intentional for repeated incremental panels in the same
output directory: each launch solves the tasks specified by the current
manifest instead of reusing stale rows whose `task_id` may now refer to a
different method.

## Running A Custom Method Panel

To run only the LLBI and projected-LLBI root-cut methods for all three
objective families:

```bash
cat > /tmp/fpp_llbi_methods.txt <<'EOF'
FPP-Branch-Benders-LLBI-RootCuts
FPP-Branch-Benders-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-CVaR-LLBI-RootCuts
FPP-Branch-Benders-CVaR-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-CVaR-ProjectedPathLLBI-exp-RootCuts
FPP-Branch-Benders-MeanCVaR-LLBI-RootCuts
FPP-Branch-Benders-MeanCVaR-ProjectedCoverageLLBI-exp-RootCuts
FPP-Branch-Benders-MeanCVaR-ProjectedPathLLBI-exp-RootCuts
EOF

SMOKE_METHODS=0 \
METHOD_FILE=/tmp/fpp_llbi_methods.txt \
RERUN_EXISTING=1 \
scripts/run_fpp_new_instances_scaling_experiment_sub20.sh
```

Use a distinct `OUTPUT_DIR` when a run should be kept as a separate experiment.
Use the same `OUTPUT_DIR` only when you intentionally want to add a method panel
to an existing batch and consolidate it afterward.

## Partial Results

If a run is stopped early, merge only completed rows with:

```bash
python3 scripts/merge_fpp_new_instances_scaling_experiment.py \
  --results-dir results/batch/fpp_new20x20_scaling \
  --allow-partial

python3 scripts/analyze_fpp_new_instances_scaling_experiment.py \
  --results-dir results/batch/fpp_new20x20_scaling \
  --input-csv results/batch/fpp_new20x20_scaling/batch_results_partial.csv \
  --allow-partial
```

The partial merge writes:

```text
batch_results_partial.csv
batch_results_partial_compact.csv
merge_report_partial.txt
summary_by_instance_objective_alpha_traincount_method.csv
summary_by_instance_alpha_traincount_method.csv
method_vs_fpp_saa_base.csv
projected_llbi_comparison.csv
rootcuts_llbi_impact.csv
validation_report.txt
```

Full merges also keep the detailed `batch_results_all.csv` and write the
analysis-ready `batch_results_compact.csv`. The analysis script prefers the
compact file when it is present. Compact results keep aggregate graph-type
proportions as numeric train/test/instance columns, while scenario ID lists and
per-scenario graph classifications remain out of the compact CSV.

When adding incremental method panels into the same `OUTPUT_DIR`, preserve the
existing `batch_results_all.csv`/`batch_results_compact.csv` before running a
new panel. After the new worker CSVs are complete, append or merge the new
logical rows into the existing consolidated file and regenerate the compact CSV.
The logical row identity is:

```text
instance_id, landscape, alpha, train_count, test_count, case_id,
objective_family, method, fpp_mode
```

This avoids overwriting a previous method panel with the most recent manifest.

## Generated Files

Generated files should stay out of Git:

```text
results/
logs/
new_instances/
profiles_newInstancas_20x20/
build/
build_gpp/
```

The repository `.gitignore` excludes these paths.
