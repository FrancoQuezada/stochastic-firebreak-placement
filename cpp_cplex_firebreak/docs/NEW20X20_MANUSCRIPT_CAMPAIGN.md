# New20x20 manuscript campaign

This is the principal homogeneous/unit-weight FPP computational campaign for the
manuscript. It is additive: the legacy scaling launchers and detailed solver
diagnostics remain available.

## Scientific design

- Optimization instance: `new20x20` only.
- Paired evaluation instance: `new20x20_reburn` only; it is never optimized.
- Training sizes: `N = 100, 200, 400, 800`.
- Firebreak intensities: `alpha = 0.01, 0.02, 0.03`.
- Independent cases: `case00` through `case29`.
- Objectives: Expected, CVaR, and Mean-CVaR.
- Eight methods per objective, 24 labels total, from
  `config/fpp_new20x20_manuscript_methods.txt`.
- Total optimization runs: `4 * 3 * 30 * 24 = 8640`.
- Training pool: scenario IDs `1..9000`.
- Fixed OOS set: scenario IDs `9001..10000` (`test_count = 1000`).
- Split seed base: `20260529`.

For `new20x20`, the split seed is
`20260529 + 1000 * case_index + N`. A single deterministic training split is
generated for each `(N, case)` and reused across every alpha, objective, and
method. Training and OOS IDs are strictly disjoint.

The campaign uses no weight registry, heterogeneous profile, clustered profile,
or additional weight replicate. Cell loss is the ordinary homogeneous unit
weight.

## Objectives

With empirical burned-area loss `L`, `beta = 0.9`, and `lambda = 0.5`:

- Expected: `E[L]`.
- CVaR: empirical `CVaR_0.9(L)`.
- Mean-CVaR: `(1-lambda) * E[L] + lambda * CVaR_0.9(L)`.

The postprocessor writes an explicit train, OOS, and paired-reburn evaluation
objective for every incumbent. It does not infer the objective from aggregate
analysis later.

## Solver and method configuration

- CPLEX time limit: 1800 seconds.
- Target MIP gap: 0.001.
- Threads per solve: 1.
- Default maximum concurrent workers: 12.
- Projected LLBI: 100 root rounds, 100 cuts per round, violation tolerance
  `1e-3`, density limit `0`, and polynomial cut cap `100000`.
- Combinatorial Benders: heuristic lift, sampling ratio `0.10`, fractional
  separation enabled, initial cuts enabled, and ascending (`eta-asc`) scenario
  ordering.

The effective settings are materialized as `experiment_configuration.csv` and
`method_configuration.csv`. Non-applicable method settings are marked `N/A`.
Eta-desc combinatorial variants are excluded from this principal panel.

## Paired reburn evaluation

The manifest generator resolves `new20x20_reburn` from the complete enabled
instance configuration, independently of `INSTANCE_FILTER`. Consequently,
`INSTANCE_FILTER=new20x20` creates no reburn optimization tasks but every
optimization row still names and requires the paired evaluation.

The worker loads the instance configuration recorded in the manifest. For every
incumbent it transfers the selected original Cell2Fire IDs to the reburn
instance and evaluates exactly the same training scenario IDs. The evaluation
uses `--require-full-firebreak-coverage`; an unmapped selected cell is a hard
validation failure. This path works without `WEIGHT_REGISTRY`.

## Outputs

The dedicated results directory defaults to
`results/batch/fpp_new20x20_manuscript_campaign`.

`manuscript_results.csv` contains:

```text
experiment_id, run_id, case_id, seed, instance_id, N, alpha,
objective_type, risk_measure, cvar_beta, cvar_lambda, method,
time_limit_sec, threads, target_mip_gap, solver_status,
termination_reason, has_incumbent, objective_value, upper_bound,
lower_bound, mip_gap, runtime_sec, wall_time_sec,
train_expected_burned_area, train_cvar_burned_area,
train_worst10_burned_area, train_eval_objective_value,
oos_expected_burned_area, oos_cvar_burned_area,
oos_worst10_burned_area, oos_eval_objective_value,
reburn_expected_burned_area, reburn_cvar_burned_area,
reburn_worst10_burned_area, reburn_eval_objective_value,
selected_firebreak_count, train_objective_abs_discrepancy,
train_objective_rel_discrepancy, objective_validation_passed,
execution_status, attempt
```

For this minimization problem, `upper_bound` is the feasible incumbent objective
and `lower_bound` is the solver best bound. Unavailable quantities are blank,
never synthetic zeroes. `mip_gap` and `target_mip_gap` are fractions (for
example, `0.001` means 0.1%).

`solutions.csv` contains one row for every incumbent. The
`selected_firebreak_ids` field is a sorted, semicolon-separated list of original
Cell2Fire IDs, sufficient to reconstruct the 400-entry binary vector `y`.
Validation requires unique IDs in `1..400` and
`selected_firebreak_count <= floor(alpha * 400)`; budget equality is not
required.

Successful runs keep the worker CSV receipt, solution, and normal logs. Detailed
solver/paired-evaluation JSON and the redundant one-row solver CSV are removed
only after the worker row is durably written. Failed or incomplete runs retain
detailed diagnostics.
Set `KEEP_FULL_JSON=1` to retain successful JSON too. This compact behavior is
campaign-specific and does not change legacy launchers.

## Summary and comparisons

After workers finish, the launcher validates results before analysis and writes
`manuscript_summary.csv`, grouped by `N x alpha x objective_type x method`.
There are 288 groups and normally 30 rows per group. It reports run/incumbent/
optimal/time-limit/failure counts and rates, runtime mean/median/sample standard
deviation, final-gap mean/median, bound means, train/OOS/reburn objective means
and sample standard deviations, and OOS/reburn Expected and CVaR means.

Case-level paired outputs are also written:

- `method_vs_fpp_saa.csv`;
- `projected_llbi_paired_comparisons.csv`, with Coverage-poly vs Coverage-exp,
  Path-poly vs Path-exp, Coverage-poly vs Path-poly, and Coverage-exp vs Path-exp.

Every comparison uses the identical `(N, alpha, objective_type, case_id)` block.

## Validation

Full validation is the default. It verifies the exact grid, 8640 unique task and
run IDs, 24 methods per controlled block, 30 cases per summary group, exact
fixed OOS IDs, deterministic split reuse, no train/OOS overlap, no reburn solve,
paired resolution/mapping, incumbent solution existence and budget, all three
evaluation namespaces, and independent train-objective agreement with the
solver incumbent at absolute or relative tolerance `1e-6`.

Missing, duplicate, malformed, or incompletely evaluated incumbent rows prevent
the campaign from being declared complete. `--allow-partial` is used internally
by `ALLOW_PARTIAL=1` for intermediate inspection; malformed observed rows still
fail.

## Commands

Build:

```bash
make cplex
```

Run the complete 24-method panel on one `N=100`, `alpha=0.01`, `case00`
instance, with one method per worker and at most 12 workers concurrently:

```bash
DRY_RUN=1 scripts/run_fpp_new20x20_n100_alpha001_one_case.sh
CONFIRM_LONG_RUN=1 scripts/run_fpp_new20x20_n100_alpha001_one_case.sh
```

This small-test launcher defaults to a 60-second limit per method, performs the
full fixed OOS and paired-reburn evaluations, and then checks that all 24 methods
produced a valid incumbent and complete outputs. Set, for example,
`TIME_LIMIT=120 MAX_PARALLEL_JOBS=8` before the second command to adjust the
per-method limit and concurrency.

Generate and validate the full scientific manifest without solving:

```bash
DRY_RUN=1 scripts/run_fpp_new20x20_manuscript_campaign.sh
```

Launch the full campaign:

```bash
CONFIRM_LONG_RUN=1 scripts/run_fpp_new20x20_manuscript_campaign.sh
```

Retry clean recorded failures without rerunning completed rows:

```bash
CONFIRM_LONG_RUN=1 RETRY_FAILED=1 scripts/run_fpp_new20x20_manuscript_campaign.sh
```

Change concurrency:

```bash
CONFIRM_LONG_RUN=1 MAX_PARALLEL_JOBS=8 scripts/run_fpp_new20x20_manuscript_campaign.sh
```

Force every logical run to be solved again with the same deterministic splits:

```bash
CONFIRM_LONG_RUN=1 RERUN_EXISTING=1 scripts/run_fpp_new20x20_manuscript_campaign.sh
```

A non-scientific lightweight end-to-end smoke mode is available as:

```bash
CONFIRM_LONG_RUN=1 SMOKE_TEST=1 scripts/run_fpp_new20x20_manuscript_campaign.sh
```
