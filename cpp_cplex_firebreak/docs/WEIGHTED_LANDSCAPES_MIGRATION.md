# Weighted Landscapes: Migration Guide

For users of the pre-weighted (legacy homogeneous) experiment pipeline.

## 1. Running old homogeneous manifests unchanged

No change required. Every command and manifest column from before the
weighted-landscape work continues to work: omitting every
`--weight-map-file`/`--weight-profile`/`--weight-registry` flag resolves to
the homogeneous profile automatically, and the solver's numerical behavior
for a homogeneous (all-weight-1.0) map is identical to the old unit-weight
behavior.

## 2. Adding weight profiles to an existing experiment

Add three flags to your manifest-generation command:
`--weight-profiles homogeneous,heterogeneous,clustered`,
`--weight-replicates 0`, `--weight-registry <dir>`. Add
`--generate-missing-weight-maps` the first time (subsequent runs reuse the
registry without regenerating). See `docs/WEIGHTED_LANDSCAPES.md` sections
5-7 for the full generation/registry model.

## 3. Generating maps directly

```bash
./build_gpp/firebreak_cpp ensure-weight-map \
    --landscape <instance> --weight-profile <profile> --weight-replicate <r> \
    --weight-registry <dir>
```

Never regenerates an existing, matching entry; hard-errors on a parameter
mismatch rather than silently overwriting.

## 4. Updating manifest commands

Existing `--train-counts`/`--alphas`/`--method-file`/etc. flags are
unchanged. The only new manifest-generator flags are the weight-related
ones above and `--paired-reburn-evaluation` (opt-in; without it, no paired
reburn evaluation is attempted for that manifest run).

## 5. Locating new result fields

Every new field lives in the canonical schema
(`scripts/weighted_result_schema.py:CANONICAL_FIELDS`), documented in full
in `docs/WEIGHTED_LANDSCAPES.md` section 20. The short version: weighted
loss lives in `weighted_fpp_*`, physical burned-cell count in
`mean_burned_cells_*`, DPV surrogate in `dpv_surrogate_*`, weight
provenance in `canonical_landscape_id`/`weight_profile`/`weight_map_hash`/etc.

## 6. Interpreting DPV surrogate fields

`dpv_surrogate_objective` is a structural proxy score, not wildfire loss —
never use it as a substitute for `weighted_fpp_expected_in_sample` (the
true evaluated loss of the selected solution, always populated
independently of which method produced the selection).

## 7. Interpreting OOS and paired fields

`weighted_fpp_expected_out_of_sample` evaluates the selected solution on
the manifest's configured test scenarios of the *same* instance.
`weighted_fpp_expected_paired_reburn` evaluates the same selection on a
*different*, physically paired reburn instance. They are never the same
value by construction and are never combined into one metric. Check
`paired_reburn_evaluation_status` (not `paired_evaluation_enabled`, which
is not a reliable signal — see `docs/WEIGHTED_LANDSCAPES.md` section 20) to
know whether paired data is present for a row.

## 8. Running the new merge pipeline

Old: `scripts/merge_fpp_new_instances_scaling_experiment.py --results-dir <dir>`.
New (weighted, recommended for any weighted experiment):

```bash
python3 scripts/merge_weighted_experiment_results.py \
    --input-root <dir> --output-dir <dir>/merged --strict
```

The old script still works for unweighted legacy experiments; it is not
removed or modified.

## 9. Running the new analysis pipeline

```bash
python3 scripts/analyze_weighted_experiment_results.py \
    --merged-current-valid <dir>/merged/merged_current_valid.csv \
    --merged-all-attempts <dir>/merged/merged_all_attempts.csv \
    --output-dir <dir>/analysis
```

Replaces manually building comparison tables; produces best-known values,
gaps, summaries, statistics, profiles, and publication tables in one pass.
The old `analyze_fpp_new_instances_scaling_experiment.py` is unchanged and
still usable for legacy unweighted analyses.

## 10. Handling legacy CSVs

Pass a pre-Phase-8A/8B CSV to the new merge pipeline via `--legacy-csv
<path> --allow-legacy`. It will be migrated with an explicit
`migration_status=legacy_migrated` marker and a `legacy_unmigrated_fields`
list naming anything that couldn't be populated — never silently
represented as a modern row.

## 11. `run_id` changes

Weighted rows have a `run_id` suffix `_wp<profile>_wr<replicate>_wh<hash8>`;
legacy (unweighted) `run_id`s are unchanged. `run_id` is guaranteed unique
within one manifest-generation invocation, not across independently
generated manifest runs — the merge pipeline's "legacy collision" duplicate
category exists to catch a cross-invocation clash if one ever occurs.

## 12. Resume behavior

Unchanged: the worker skips already-completed rows by default. New this
line of work: `--retry-failed` (rerun only failed rows, preserving the
logical `run_id`, incrementing `attempt`) as an alternative to
`--rerun-existing` (reruns everything) — the two are mutually exclusive.
