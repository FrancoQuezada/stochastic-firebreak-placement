# C++ Stochastic Firebreak Placement - New Instances Experiment

This checkout is trimmed to run the `new_instances` FPP scaling experiment.
Historical experiment launchers, old config files, generated results, and logs
are intentionally excluded from the active `scripts/` and `config/` folders.

## Required Files

The experiment launcher is:

```bash
scripts/run_fpp_new_instances_scaling_experiment.sh
```

The minimal script chain is:

```text
scripts/run_fpp_new_instances_scaling_experiment.sh
scripts/generate_fpp_new_instances_scaling_manifests.py
scripts/run_fpp_new_instances_scaling_manifest_worker.py
scripts/merge_fpp_new_instances_scaling_experiment.py
scripts/analyze_fpp_new_instances_scaling_experiment.py
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

The default method file has 18 labels:

```text
6 method families x 3 objectives = 18 methods
```

Objectives:

```text
Expected
CVaR
MeanCVaR
```

The projected LLBI variants used by this experiment are the `exp` variants
only. The old `poly` variants are not part of this experiment.

## Default Experiment Grid

```text
training pool: 1..9000
fixed OOS test set: 9001..10000
train counts: 100,200,400,800
alphas: 0.01,0.02,0.03
cases: 5
time limit: 1800 seconds
MIP gap: 0.001
threads per solve: 1
max parallel workers: 12
```

Expected row counts:

```text
all six folders valid: 6480 rows
four validated folders only: 4320 rows
20x20 and 20x20_reburn only: 2160 rows
```

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
merge_report_partial.txt
summary_by_instance_objective_alpha_traincount_method.csv
summary_by_instance_alpha_traincount_method.csv
method_vs_fpp_saa_base.csv
projected_llbi_comparison.csv
rootcuts_llbi_impact.csv
validation_report.txt
```

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
