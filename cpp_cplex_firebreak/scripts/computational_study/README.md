# FPP Risk-Objective Computational Study Scripts

These scripts prepare and run the Sub20 FPP risk-objective study for expected
value, mean-CVaR, and pure CVaR objectives. They generate shared train/test
splits, command manifests, grouped shell command files, runner logs, and
aggregation CSVs.

The scripts do not run optimization models during generation.

## Safe Checks

```bash
make study-fpp-risk-smoke
make study-fpp-risk-dry-run
```

The smoke check generates a tiny one-alpha, one-case manifest and verifies
lambda-to-risk flag mapping without solving any models.

## Generate The Default Study Manifest

```bash
make study-fpp-risk-generate-commands
```

Default grid:

- `landscape = Sub20`
- `alphas = 0.01,0.02,0.03,0.04,0.05`
- `lambdas = 0,0.5,1`
- `case_ids = 0,1,2,3,4`
- `train_count = 100`
- `test_count = 100`
- `time_limit = 1800`
- `threads = 1`

Generated files are written under:

```text
results/computational_study_fpp_risk/
```

## Run In Parallel

Build the CPLEX binary first:

```bash
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 ./build_gpp.sh --with-cplex
```

Run all exact baselines:

```bash
python3 scripts/computational_study/run_command_manifest.py \
  --command-file results/computational_study_fpp_risk/commands/commands_all_exact.sh \
  --parallel 8 \
  --skip-existing
```

Run all restricted heuristic variants:

```bash
python3 scripts/computational_study/run_command_manifest.py \
  --command-file results/computational_study_fpp_risk/commands/commands_all_heuristics.sh \
  --parallel 8 \
  --skip-existing
```

Run all commands only when intentionally launching the full grid:

```bash
python3 scripts/computational_study/run_command_manifest.py \
  --command-file results/computational_study_fpp_risk/commands/commands_all.sh \
  --parallel 8 \
  --skip-existing
```

## Aggregate Results

```bash
make study-fpp-risk-aggregate
```

Summary CSVs are written under:

```text
results/computational_study_fpp_risk/summaries/
```

The aggregation handles missing JSON outputs gracefully, so it can be run before
the full grid has completed.

## Risk Mapping

- `lambda = 0.0`: `--risk-measure expected`
- `lambda = 0.5`: `--risk-measure mean-cvar --cvar-beta 0.9 --cvar-lambda 0.5`
- `lambda = 1.0`: `--risk-measure cvar --cvar-beta 0.9`

The Phase 1S tail-blend restricted heuristic is generated only for pure CVaR
(`lambda = 1.0`) because the current implementation rejects tail-blend scoring
outside `risk_measure = cvar`.
