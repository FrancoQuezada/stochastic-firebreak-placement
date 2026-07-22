# Phase 9A: Canonical Result Schema, Merge Integrity, and Provenance Validation

Phase 9A builds a canonical, versioned result schema and a new merge pipeline
on top of the data Phases 1-8B already produce. It does not run solvers,
regenerate weight maps, change selected solutions, or compute final
performance profiles / publication tables — that is Phase 9B / Phase 10.

## 1. Audit of existing result and merge paths

### 1.1 The master row shape: `StandardExperimentResult`

`include/io/ExperimentResultWriter.hpp` declares `StandardExperimentResult`
(~560 fields), written as both JSON and CSV by every `*OutOfSampleRunner.cpp`
and by `MethodDispatcher.cpp`. Relevant to Phase 9A's objective-space
separation:

- `objective_in_sample` / `best_bound` / `mip_gap`: whatever the dispatched
  solver actually optimized. For FPP-exact methods this **is** the exact
  weighted-FPP objective. For DPV-surrogate methods (`DPV-SAA`,
  `DPV-Benders`, `DPV-Branch-Benders`, `Static-DPV`, `Greedy`) it is the
  surrogate's own value.
- `dpv_surrogate_objective` / `_best_bound` / `_gap`: an explicit, separate
  field for the DPV surrogate value. Already named exactly as Phase 9A
  section 4.2 requires.
- `solver_weighted_objective`: **confirmed to be overloaded**. For FPP-exact
  rows it equals the true weighted loss. For DPV-surrogate rows (verified
  against `results/weighted_phase8b_smoke/manifest_homogeneous/.../json/worker_000_task_002.json`,
  method `DPV-SAA`) it is populated with the **same value as
  `dpv_surrogate_objective`** (11385.5), not the true weighted loss
  (270.625, which lives in `evaluator_weighted_objective` /
  `train_expected_weighted_burn_loss`). A naive consumer reading
  `solver_weighted_objective` at face value for a DPV row would believe the
  surrogate score is the real wildfire loss. This is the concrete case
  Phase 9A section 4 exists to prevent, and it is why the canonical
  `solver_objective` field is never derived from `solver_weighted_objective`
  (see section 4 below).
- `train_expected_weighted_burn_loss` / `test_expected_weighted_burn_loss`,
  `train_weighted_cvar` / `test_weighted_cvar`, `train_weighted_var` /
  `test_weighted_var`: the true, exact weighted-FPP evaluation of the
  selected solution, computed uniformly for every method family. These are
  the canonical `weighted_fpp_expected_*` / `weighted_fpp_cvar_*` sources for
  the `in_sample` / `out_of_sample` namespaces.
- `train_expected_burned_area` / `test_expected_burned_area` (and
  `*_worst_10pct_burned_area`, `*_empirical_var_90pct_burned_area`,
  `*_empirical_cvar_90pct_burned_area`): **unweighted, physical** burned-cell
  counts, produced by `eval::BurnedAreaEvaluator` (see
  `include/eval/BurnedAreaEvaluator.hpp`), which never reads a weight map.
  These are the canonical `mean_burned_cells_*` / `burned_cells_*` sources —
  named "burned_area" in the C++ layer but confirmed unweighted, hence the
  rename in the canonical schema (never label them as weighted loss).
- `objective_validation_passed` / `_abs_difference` / `_rel_difference`:
  cross-checks the solver's own objective against the evaluator's. Confirmed
  (task_002.json, DPV-SAA row) that this reads `False` for a **structurally
  correct** DPV-surrogate row, because the surrogate is never expected to
  equal the true weighted loss. This field is therefore only a meaningful
  correctness signal for FPP-exact rows; the Phase 9A validator only treats
  `objective_validation_passed == False` as an error for FPP-exact rows (see
  section 4/17).
- No `paired_reburn_*` field exists in this C++ struct at all — that
  provenance is added entirely by the Python worker/manifest layer.

### 1.2 The `evaluate` CLI already computes the weighted paired-reburn loss — but the worker drops it

`src/experiments/EvaluationRunner.cpp` (the `evaluate` subcommand invoked by
the worker for paired-reburn evaluation) computes **both** the unweighted
`InstanceBurnedAreaResult` and, when `--weight-map-file` is supplied (which
the worker always passes for paired reburn), the weighted
`eval::FppRecourseResult` — and writes both into the same output JSON
(`expected_burned_area` **and** `expected_weighted_burn_loss` /
`weighted_var` / `weighted_cvar`, confirmed by inspecting
`.../json/worker_000_task_000_paired_reburn_eval.json`).

`scripts/run_fpp_new_instances_scaling_manifest_worker.py:parse_paired_reburn_evaluation_json`
(lines 590-606) only extracts the **unweighted** fields
(`expected_burned_area`, `worst_10pct_burned_area`,
`empirical_var_90pct_burned_area`, `empirical_cvar_90pct_burned_area`) into
the worker CSV. The weighted paired-reburn loss the C++ layer already
computed is silently discarded before it ever reaches a CSV row.

This is a real, confirmed gap, and it is the reason Phase 9A's normalization
layer (`weighted_result_normalize.py`) reads the per-task
`<task_id>_paired_reburn_eval.json` file directly for
`weighted_fpp_expected_paired_reburn` / `weighted_fpp_cvar_paired_reburn` —
there is no CSV column to fall back on for these two fields today. This is
exactly the "prefer JSON as lossless source" case Phase 9A section 18
anticipates, not a hypothetical one. **The worker script itself was not
modified** — this is deliberately a read path added on top of existing
output, not a change to the live experiment-running pipeline.

### 1.3 `method_family` is currently a verbatim copy of `method`

Confirmed against the Phase 8B smoke worker CSV: the `method_family` column
is populated identically to `method` for every row (`FPP-SAA` /
`FPP-SAA`, `DPV-SAA` / `DPV-SAA`, ...). It carries no independent grouping
information today. `weighted_result_schema.derive_method_family()` only
trusts the raw `method_family` value when it actually differs from
`method`; otherwise it derives a real family (`FPP` / `DPV` / the method
string) from the presence of `dpv_surrogate_objective`.

### 1.4 The old merge script (`merge_fpp_new_instances_scaling_experiment.py`) has known gaps

- `OMIT_FIELDS` still strips `run_id`, `output_json`, `solution_dir`,
  `worker_return_code`, `worker_status` from its merged CSV, even though
  these were deliberately kept in the *worker's own* `OMIT_FIELDS` during the
  Phase 8B resume-bug fix. Its merged CSV therefore has no `run_id` to join
  back to logs/provenance, while its own compact CSV (written from the same
  in-memory rows, before `OMIT_FIELDS` is applied) does.
- `logical_key()` has zero retry/attempt awareness: any duplicate logical
  key is an unconditional `SystemExit`, with no distinction between an exact
  duplicate, a retry attempt, and a genuine conflict.
- This script is left in place (still used by the Phase 8B smoke
  shell launchers) and is not modified by Phase 9A. Phase 9A adds a new,
  parallel pipeline (`merge_weighted_experiment_results.py`) that reads the
  same per-worker CSVs directly, rather than reworking the old script's
  contract.

### 1.5 `fpp_new_instances_scaling_compact_schema.py`'s `numeric_or_blank` is intentionally lenient

`numeric_or_blank()` silently passes a malformed non-numeric string through
unchanged (so a human can spot it in the compact CSV) rather than rejecting
it. That is the wrong behavior for a *validator*. Phase 9A adds a separate,
strict parser (`weighted_result_schema.parse_numeric_strict` /
`parse_int_strict`) used only by the new validator; the lenient compact-CSV
behavior is untouched (it still serves its original purpose for the old
pipeline).

### 1.6 `analyze_fpp_new_instances_scaling_experiment.py`'s grouping keys omit weight provenance

`controlled_key` / `cross_alpha_key` / `comparison_key` do not include
`weight_profile` / `weight_replicate` / `weight_map_hash`, so a naive
aggregation across those keys would silently pool rows solved under
different weight maps. Phase 9A's `comparison_group_key()` /
`comparison_group_is_valid()` (section 23) is the replacement primitive for
Phase 9B; the old analyze script is not modified.

### 1.7 On-disk legacy result shapes (all confirmed present)

- `results/new_instances_smoke_summary.csv`: a 30-column instance/graph
  validation summary. **Not** a `StandardExperimentResult`-derived schema at
  all (no `objective_in_sample`, no weight columns) — a different domain
  entirely, out of scope for Phase 9A's result-schema migration.
- `results/weighted_phase5a_smoke/phase5a_benders_smoke.csv`: 172 columns,
  has `weight_profile` / `weight_map_hash` / `weight_normalized` etc. but
  lacks every Phase 8A/8B canonical-registry field (`canonical_landscape_id`,
  `weight_replicate`, `weight_generation_seed`, `weight_generator_version`,
  `weight_source_universe_hash`, `weight_mapping_method`) and any
  `paired_reburn_*` column. This is the concrete "legacy-weighted-pre8a"
  shape (`LEGACY_SCHEMA_WEIGHTED_PRE8A`).
- `results/weighted_phase8b_smoke/manifest_{homogeneous,heterogeneous,clustered}/workers/worker_000/batch_results_worker_000.csv`:
  450-column modern rows (manifest columns + `WORKER_FIELDS` +
  `StandardExperimentResult` columns), 4 rows each (one per method:
  `FPP-SAA`, `FPP-Branch-Benders-Combinatorial`, `DPV-SAA`, `Static-DPV`) —
  this is the integration-smoke fixture used in section 28 below. Each
  task's row is a stateful, resume/retry-updated record (one row per
  `task_id`, `attempt` incremented in place), not an append-only attempt log
  — so this fixture cannot itself demonstrate two *separate on-disk rows*
  for the same task at two different attempts; that scenario is covered by
  the synthetic unit test `scripts/test_weighted_result_retry_selection.py`
  instead.
- `run_id` uniqueness is only guaranteed **within one manifest-generation
  invocation** (`generate_fpp_new_instances_scaling_manifests.py:verify_manifests`),
  not across invocations. Two separate manifest-generation runs covering
  overlapping instance/method/weight combinations could in principle produce
  colliding `run_id`s if ever merged together — the merge pipeline's
  "legacy collision" duplicate category exists to catch exactly this if it
  ever happens (see section 13).

## 2-3. Canonical schema and version

`scripts/weighted_result_schema.py` defines `RESULT_SCHEMA_VERSION =
"weighted-result-9a.1"`. Every row this pipeline normalizes is stamped with
either this value or one of two documented legacy versions:

- `legacy-weighted-pre8a` — has `weight_profile`/`weight_map_hash` but not
  the Phase 8A registry columns (matches `phase5a_benders_smoke.csv`).
- `legacy-unweighted-pre8a` — has neither.

Detection (`detect_legacy_schema_version`) only runs when a row has **no**
explicit `result_schema_version` field at all; it is never used to downgrade
a modern row that merely leaves an optional field blank. An explicit,
unsupported `result_schema_version` value (anything outside
`SUPPORTED_SCHEMA_VERSIONS`) is rejected as `invalid_schema`, never silently
coerced.

## 4. Objective-space separation

| Canonical field | Source | Notes |
|---|---|---|
| `solver_objective` / `_best_bound` / `_gap` | `objective_in_sample` / `best_bound` / `mip_gap`, **only when the row is not a DPV-surrogate row** | Never populated from a DPV surrogate solve (`weighted_result_normalize.derive_solver_objective_fields`) |
| `dpv_surrogate_objective` / `_best_bound` / `_gap` | `dpv_surrogate_objective` / `_best_bound` / `_gap` (already unambiguous in the C++ struct) | Copied through verbatim |
| `weighted_fpp_expected_in_sample` / `_out_of_sample` | `train_expected_weighted_burn_loss` / `test_expected_weighted_burn_loss` | Computed uniformly for every method family |
| `weighted_fpp_cvar_in_sample` / `_out_of_sample` | `train_weighted_cvar` / `test_weighted_cvar` | |
| `weighted_fpp_mean_cvar_*` | derived: `lambda * expected + (1-lambda) * cvar`, only when `risk_measure` indicates mean-CVaR and all three inputs are present | Otherwise left blank, never fabricated |
| `weighted_fpp_expected_paired_reburn` / `_cvar_paired_reburn` | **JSON-only**: `<task>_paired_reburn_eval.json`'s `expected_weighted_burn_loss` / `weighted_cvar` | See section 1.2 — no CSV column exists for these today |

`is_dpv_surrogate_row()` determines DPV-surrogate status from the presence
of a finite `dpv_surrogate_objective` — the one explicit, unambiguous field
for this — never by string-matching the method name.

## 5. Physical metric separation

`mean_burned_cells_in_sample` / `_out_of_sample` / `_paired_reburn` source
from `train_expected_burned_area` / `test_expected_burned_area` /
`paired_reburn_train_expected_burned_area` respectively — all confirmed
unweighted (section 1.1). `burned_cells_var_*` / `_cvar_*` / `_worst10_*`
and `percentage_landscape_value_burned_*` /
`percentage_high_value_weight_burned_*` round out the physical-metric group.

## 6. Evaluation namespaces

Three explicit namespaces (`in_sample`, `out_of_sample`, `paired_reburn`),
each with its own `*_evaluation_status`, `*_scenario_count`,
`*_scenario_ids`, `weighted_fpp_expected_*`, `weighted_fpp_cvar_*`,
`weighted_fpp_mean_cvar_*`, `mean_burned_cells_*`, and
`*_evaluation_time_sec` / weight-hash field. Verified end-to-end
(`test_weighted_result_paired_validation.py::test_oos_vs_paired_distinction`)
that the paired-reburn evaluation never overwrites the out-of-sample fields
— they are populated from entirely independent source columns/JSON keys.

`paired_reburn_scenario_ids` / `_scenario_count` are documented aliases of
`train_ids` / `train_scenario_count`: the paired-reburn evaluation currently
re-evaluates the *same* train scenario IDs against the reburn instance —
there is no independent paired-reburn scenario ID list on disk today. This
is stated explicitly rather than left to look like an independent source.

## 7. Risk metadata

`risk_measure`, `cvar_beta`, `mean_cvar_lambda` (aliasing the raw
`cvar_lambda` column), `alpha`, and `budget` are preserved as distinct
canonical fields — `alpha` (a firebreak budget fraction in this codebase,
confirmed via `MethodDispatchRequest::alpha` and its use as a resource
constraint, not a CVaR confidence level) is never conflated with
`cvar_beta` (the actual CVaR tail parameter).

## 8. Weight provenance

All fields from Phase 9A's list are canonical fields:
`canonical_landscape_id`, `paired_landscape_id`, `weight_profile`,
`weight_replicate`, `weight_generation_seed`, `weight_generator_version`,
`weight_map_path`, `weight_map_hash`, `weight_source_universe_hash`,
`weight_normalization_mode` (derived from the raw boolean
`weight_normalized`), `weight_mapping_method`, `weight_mapping_hash`. Plus
the per-stage hash fields: `manifest_weight_map_hash` (alias of
`weight_map_hash`), `optimization_weight_map_hash`,
`in_sample_weight_map_hash` (documented alias of
`optimization_weight_map_hash` — the optimization and in-sample evaluation
share one weight map by construction), `out_of_sample_weight_map_hash`,
`paired_reburn_weight_map_hash`. Verified (section 15/17) that a mismatch
among any of these is rejected as `invalid_weight_provenance` before a row
can enter the valid dataset (`test_weighted_result_provenance.py`).

## 9. Scenario provenance

`train_ids` / `test_ids` (parsed as ordered integer lists, deterministic
serialization via `parse_list_int`), `train_scenario_count` /
`test_scenario_count`. Two rows with different `train_ids` are always
distinct logical runs and are never pooled (verified:
`test_weighted_result_provenance.py::test_different_train_splits_stay_separate`).

## 10. Method identity

`method` (original label, untouched), `method_family` (derived — see
section 1.3), `method_variant`, `solver_formulation`, `llbi_type` (derived
from the boolean LLBI flags + `projected_family()`, reusing
`fpp_new_instances_scaling_compact_schema` helpers rather than duplicating
that logic), `combinatorial_mode`, `lifting_mode`, `scenario_order`,
`sampling_ratio`, `dpv_variant`, `dpv_ignition_policy`,
`restricted_candidate_mode` (derived from the three
`restricted_candidate_*_mode_enabled` booleans).

## 11. Execution identity

`run_id` (logical, stable across retries per the Phase 8B `weighted_run_id`
suffix scheme), `attempt`, `execution_status`, `solver_status`,
`resume_action`, `failure_stage`/`_type`/`_message`, `worker_exit_code`,
`worker_return_code`. The current-valid selection (section 13) always picks
the highest `attempt` among rows classified `valid`/`valid_legacy_migrated`
for a logical run — never the first or the last unconditionally.

## 12. Canonical row key

`record["logical_run_key"]` is `run_id` when present and non-empty;
otherwise a `legacy::field=value|...` string built from
`LEGACY_MIGRATION_KEY_FIELDS` (canonical landscape, instance, method
identity, risk configuration, scenario split, weight-map identity) — never
the CSV row position.

## 13. Merge duplicate policy

Implemented in `weighted_result_merge.classify_duplicates()`:

- **Exact duplicate**: identical `content_hash` (a SHA-256 over every
  canonical field's value) within one logical run → one kept, the rest
  flagged `exact_duplicate`, count reported.
- **Retry attempt**: different `attempt` values within one logical run →
  all preserved in `merged_all_attempts`; the highest-`attempt` row
  classified valid/valid_legacy_migrated is selected as current-valid.
- **Conflicting duplicate**: same `run_id` + `attempt`, different
  `content_hash` → every row in the conflict is reclassified
  `conflicting_duplicate` and excluded from `merged_current_valid` — never
  silently resolved by keeping the first or last.
- **Legacy collision**: same `run_id` (or derived key) shared by rows whose
  core identity fields (`method`, `instance_id`, `alpha`,
  `train_scenario_count`, `weight_map_hash`) genuinely differ → each
  distinct configuration gets its own `::collision_N` suffix and is
  reclassified `conflicting_duplicate`, with a count reported.

## 14-17. Validation

`weighted_result_merge.validate_record()` runs, in order: schema-version
check → strict numeric-parse-error check (section 17) → required-identity
check → execution-completeness check → weight-hash consistency check
(section 15) → FPP-exact objective cross-check (section 4) → paired
evaluation completeness (section 16). First failing check determines the
classification; the taxonomy exactly matches
`weighted_result_schema.VALIDATION_CLASSIFICATIONS`. `objective_validation_passed
== False` is only ever treated as an error for FPP-exact rows (see section
1.1) — a DPV-surrogate or heuristic row failing this check is expected
behavior, not a defect.

## 18. CSV/JSON normalization

`weighted_result_normalize.normalize_row()` is the single entry point.
`prefer_json=True` (the default) reads the sibling per-task solver JSON and
paired-reburn-evaluation JSON when they exist and prefers their values for
any canonical field whose worker-CSV value is missing (and, for
`weighted_fpp_expected_paired_reburn` / `_cvar_paired_reburn`, JSON is the
*only* source — see section 1.2). List fields (`train_ids`, `test_ids`,
...) serialize deterministically via `parse_list_int`. Malformed numeric
strings are never coerced to zero — they are reported in
`record["_numeric_errors"]` and the field is left `None`
(`parse_numeric_strict`/`parse_int_strict`).

## 19-20. Canonical output artifacts and diagnostics

`scripts/merge_weighted_experiment_results.py` writes, every run:
`merged_all_attempts.csv`, `merged_current_valid.csv`, `merged_invalid.csv`,
`merge_diagnostics.json` (with `input_files`, `input_rows`, `parsed_rows`,
`valid_rows`, `legacy_migrated_rows`, `invalid_rows`, `incomplete_rows`,
`exact_duplicates`, `retry_attempts`, `conflicting_duplicates`,
`legacy_collision_rows`, `logical_runs`, `current_valid_runs`,
`failed_runs`, `weight_hash_mismatches`, `paired_validation_failures`,
`schema_versions`, `method_counts`, `weight_profile_counts`,
`classification_counts`, and `invalid_source_files`). Invalid rows are
never discarded without a reason attached (`validation_reasons` column).

## 21. Compact schema

`scripts/fpp_new_instances_scaling_compact_schema.py` is left in place
(still used by the old pipeline) and is reused, not duplicated: the new
canonical schema module imports its helper functions
(`is_missing`/`first_value`/`bool_value`/`any_bool`/`method_tokens`/
`projected_family`) rather than re-implementing that coercion logic a
second time. The canonical schema's own field/type declarations
(`CANONICAL_FIELDS` in `weighted_result_schema.py`) are the single
authoritative list for the new pipeline; the manifest schema (workers'
`WORKER_FIELDS`) and this result schema remain intentionally distinct
concepts.

## 22-23. Aggregation-ready fields and comparison-group keys

Every field listed in Phase 9A section 22 is a canonical field (see the
table above and `weighted_result_schema.CANONICAL_FIELDS`).
`comparison_group_key()` builds a key from instance, train scenario IDs,
risk configuration, weight-map hash, and budget; `comparison_group_is_valid()`
additionally rejects any group mixing FPP-exact and DPV-surrogate objective
spaces. No best-known-value computation or performance profile is
implemented in Phase 9A — this is explicitly Phase 9B's job, built on top of
these primitives.

## 24. Status filters

`is_valid_exact_result`, `is_valid_heuristic_result`, `is_valid_dpv_result`,
`is_fully_paired_valid_result` in `weighted_result_schema.py`, reusable
as-is by Phase 9B. Note the distinction from merge-level "valid"
classification: a row can be `valid` (mergeable, real data) without being
`is_valid_exact_result` (e.g. a time-limited feasible-but-not-optimal FPP
solve is still a valid merged row; it just isn't the strict "optimal exact
result" an analysis might require).

## 25. Backward compatibility

Legacy rows (both `legacy-unweighted-pre8a` and `legacy-weighted-pre8a`) are
migrated with an explicit `migration_status` and
`legacy_unmigrated_fields` list (which canonical fields could not be
populated) rather than silently left blank. `--allow-legacy` on the CLI
gates whether they are migrated into `valid_legacy_migrated` at all (default:
rejected as `invalid_schema`, forcing an explicit opt-in). A legacy row
never collides with its modern weighted counterpart because the modern
`run_id` carries the Phase 8B weight suffix the legacy one lacks
(`test_weighted_result_legacy_migration.py::test_legacy_never_collides_with_modern`).

## 26. CLI

`scripts/merge_weighted_experiment_results.py`:

```bash
python3 scripts/merge_weighted_experiment_results.py \
    --input-root results/weighted_phase8b_smoke \
    --output-dir results/weighted_phase8b_smoke/merged \
    --strict
```

Flags: `--input-root` (repeatable, recursively discovers
`batch_results_worker_*.csv`), `--input-csv` / `--legacy-csv` (repeatable,
explicit files), `--output-dir` (required), `--strict` (non-zero exit if
any modern row is invalid), `--allow-legacy`, `--include-failed`,
`--prefer-json` / `--no-prefer-json` (default on), `--schema-version`,
`--validate-paired` / `--no-validate-paired` (default on). No solver
execution, no weight-map generation; deterministic output column order
(`CANONICAL_FIELD_ORDER` + `VALIDATION_META_FIELD_ORDER`) regardless of
input order.

## 27. Tests

Nine focused Python test scripts under `scripts/` (co-located with the
existing `scripts/test_weighted_manifest_worker.py` self-test convention;
not placed under `tests/`, which is exclusively the C++ gtest source tree
built by `make test`):

| File | Mandatory case(s) covered |
|---|---|
| `test_weighted_result_schema.py` | field-list integrity, strict numeric parsing, comparison-group key/status-filter unit checks |
| `test_weighted_result_normalization.py` | 27.1, 27.8, 27.9, 27.11, 27.14 |
| `test_weighted_result_merge.py` | 27.15, end-to-end CLI + corrupted-row isolation |
| `test_weighted_result_retry_selection.py` | 27.3 |
| `test_weighted_result_duplicate_detection.py` | 27.2, 27.4 |
| `test_weighted_result_provenance.py` | 27.5, 27.12, 27.13 |
| `test_weighted_result_paired_validation.py` | 27.6, 27.7 |
| `test_weighted_result_legacy_migration.py` | 27.10 |
| `test_weighted_comparison_group_keys.py` | section 23 group-validity rules |

All 9 scripts pass (105 assertions total, 0 failures) as of this writing.
Run with `python3 scripts/<name>.py` (no build required — pure in-memory
unit tests over synthetic rows via `weighted_result_test_helpers.py`).

## 28. Integration smoke

Run against a scratch copy of `results/weighted_phase8b_smoke/` (the real
fixture on disk was never modified — verified via `git status`/`git diff`
showing zero changes there):

- Clean copy, `--strict`: 12/12 rows valid, 12 logical runs, 12
  current-valid rows, one distinct `weight_map_hash` per profile
  (`homogeneous`/`heterogeneous`/`clustered`), `solver_objective` populated
  only for the two FPP-exact methods and blank for the two DPV-family
  methods (and vice versa for `dpv_surrogate_objective`),
  `weighted_fpp_expected_out_of_sample` and
  `weighted_fpp_expected_paired_reburn` independently populated and
  differing per row (recovered from the paired-reburn-eval JSON in every
  case). Exit code 0.
- One row (`FPP-SAA`, heterogeneous profile) corrupted in place
  (`objective_in_sample` set to a non-numeric string): non-strict run
  produces 11 valid + 1 invalid (`invalid_evaluation`, reason names the
  unparseable field), the corrupted row appears **only** in
  `merged_invalid.csv` (confirmed absent from `merged_current_valid.csv`),
  and `--strict` correctly exits 1.

## 29. This document

## 30-32. Validation commands, deviations, and remaining Phase 9B work

See the completion report delivered at the end of this phase for full
command output, skipped items, and an explicit per-criterion acceptance
check. Deviations from the spec, all deliberate and documented above:
`method_variant` and `restricted_candidate_mode` are derived rather than
copied from a single existing raw column (no such column exists);
`weighted_fpp_mean_cvar_*` is best-effort-derived and frequently blank
(no raw source computes it today); paired-reburn scenario IDs are a
documented alias of the train scenario IDs, not an independent list.

Phase 9B work this phase deliberately does not do: best-known-value
computation, performance profiles, statistical hypothesis tests,
publication tables, final benchmarking/release validation.
