# Weighted Landscapes — Phase 8B

Complete batch-worker integration, experiment-family execution, paired reburn
evaluation, and resume/retry behavior on top of the Phase 8A canonical registry.
Phase 8B connects the already-existing Phase 8A infrastructure (canonical landscape
identity, weight-map registry, paired reduced/reburn mapping, capability matrix) to
every relevant execution path: the C++ `BatchExperimentConfig`/`BatchExperimentRunner`
path, and the production Python manifest generator/worker pipeline used by
`scripts/run_fpp_new_instances_scaling_experiment.sh`.

This document also records that this Phase 8B implementation was interrupted mid-session
by a compute-capacity limit and resumed in a second session; Section 0 records exactly
what state was recovered and what remained broken at the resume point.

---

## 0. Recovered interrupted state (audit before continuing)

At resume time, `git status` showed the branch `feature/instance-generation-updates`
with:

- All Phase 8A work already committed (`fd5a04d8 Add canonical weighted experiment
  registry`).
- Phase 7B2 (weighted DPV optimization) still present as **uncommitted, untouched**
  changes to `include/benders/Dpv*`, `src/benders/Dpv*`, `include/opt/WeightedDpvScoring.*`,
  `src/solver/DpvSaaCplexModel.cpp`, `tests/test_weighted_dpv_heuristic_reporting.cpp`,
  and the untracked `tests/test_weighted_dpv_optimization.cpp`. These were **not**
  modified in Phase 8B and remain exactly as found.
- A full Phase 8B C++ implementation already in place and unstaged: `BatchExperimentConfig`
  weight fields, `ExperimentManifest` key parsing, `BatchExperimentRunner` wiring,
  `MethodDispatcher` hash verification and result pass-through, subset-tolerant
  `attach_weight_map_to_optimization_instance`, `EvaluationRunner` strict firebreak
  coverage, and three new C++ tests (`test_weighted_batch_config`,
  `test_weighted_subset_attachment`, `test_weighted_capability_cross_check`), all already
  registered in the Makefile and — per the transcript preceding the interruption —
  already validated with a full `make build` / `make cplex` / `make test` pass (91 test
  binaries, exit 0).
- A partially reworked Python worker (`scripts/run_fpp_new_instances_scaling_manifest_worker.py`)
  and generator (`scripts/generate_fpp_new_instances_scaling_manifests.py`), with a
  **known, actively-being-diagnosed bug**: the worker's `OMIT_FIELDS` set still stripped
  `run_id`, `output_json`, `solution_dir`, `worker_status`, and `worker_return_code` from
  the persisted per-worker CSV, so the new strict `row_complete_and_valid()` validator
  could never see a prior run's outcome on reload — resume silently reran every row on
  every invocation. This was mid-diagnosis (the exact broken field had just been located)
  when the session was interrupted.
- `scripts/merge_fpp_new_instances_scaling_experiment.py`,
  `scripts/fpp_new_instances_scaling_compact_schema.py`, and the two shell launchers
  (`run_fpp_new_instances_scaling_experiment.sh`, `_sub20.sh`) — **not yet touched**.
- No Phase 8B Python self-tests yet existed under `scripts/`.
- `docs/WEIGHTED_LANDSCAPES_PHASE8B.md` did not yet exist.

Nothing was staged. This continuation left all prior uncommitted work exactly as found,
fixed the identified resume bug, and completed the remaining unimplemented items.

---

## 1. Objective

> Complete batch-worker integration, experiment-family execution, paired reburn
> evaluation, and resume/retry behavior using the Phase 8A canonical registry.

Phase 8B does not implement Phase 9 aggregation/analysis or Phase 10 release
validation, and does not launch a large experiment matrix.

---

## 2. C++ `BatchExperimentConfig` weighted integration

`include/experiments/BatchExperimentConfig.hpp` gained (additive, default-empty):

```text
weight_map_file, weight_profile, weight_replicate, weight_generation_seed,
weight_generator_version, canonical_landscape_id, paired_landscape_id,
weight_map_hash, weight_source_universe_hash, paired_reburn_instance_id,
paired_evaluation_enabled
```

`validate_batch_experiment_config` rejects `weight_profile` set without `weight_map_file`,
and negative `weight_replicate`. `src/experiments/ExperimentManifest.cpp` parses matching
`key=value` manifest keys (same names, snake_case) and `describe_manifest_config` prints
the resolved weight profile/map file/canonical id. `MethodDispatchRequest`
(`include/experiments/MethodDispatcher.hpp`) gained the identical field set.
`src/experiments/BatchExperimentRunner.cpp` threads every field from
`BatchExperimentConfig` into `MethodDispatchRequest`, and `resume_key`/
`load_completed_resume_keys` now include `weight_profile`/`weight_replicate`/
`weight_map_hash` so a weighted row can never collide with (or wrongly resume from) a
legacy homogeneous row solved under the same method/alpha/train/test/case. CLI flags
`--weight-map-file --weight-profile --weight-replicate --weight-generation-seed
--weight-generator-version --canonical-landscape-id --paired-landscape-id
--weight-map-hash --weight-source-universe-hash --paired-reburn-instance-id
--paired-evaluation-enabled` were added to `run-batch-oos` in `src/main.cpp`.

## 3. Map pre-generation before workers

Unchanged from Phase 8A's `ensure-weight-map` CLI command and
`experiments::WeightMapRegistry`; Phase 8B's Python generator invokes it via
`--generate-missing-weight-maps` (`resolve_weight_entry` in
`scripts/generate_fpp_new_instances_scaling_manifests.py`), which is idempotent
(re-validates and reuses an existing entry rather than regenerating it) and is intended
to run once, single-process, before any worker launches. Clustered-profile generation
parameters (`--weight-cluster-count 3 --weight-cluster-fraction 0.15`) are supplied by
the generator itself when calling `ensure-weight-map`, not by the caller.

## 4. Worker no-regeneration enforcement

`weight_map_file(row)` in the worker (unchanged from Phase 8A, re-verified this phase)
requires the manifest-named `weight_map_path` to exist and raises a clear
`RuntimeError` otherwise; it never calls `ensure-weight-map` or any generation code.
Phase 8B fixed the enforcement path: `build_command()` (which calls `weight_map_file()`)
is now wrapped in a per-row `try/except` in `main()`, so a missing/invalid map is
recorded as `failure_stage=weight_map_loading` for **that row only** and the worker
continues to the remaining rows, instead of crashing the whole worker process with an
uncaught traceback (the bug found and fixed this phase — see Section 10.2).

## 5. Subset-tolerant map attachment

`solver::attach_weight_map_to_optimization_instance` (`include/solver/FppWeightedLossUtils.hpp`,
`src/solver/FppWeightedLossUtils.cpp`) no longer passes the instance's compact universe
as an exact-equality `expected_original_cell_ids` set when loading the CSV; it loads the
map unconstrained and relies on `core::build_compact_weight_vector` (which already
requires only that every compact node's original ID be present in the map — never that
the map be limited to that set) for coverage. New optional parameters:
`expected_weight_map_hash` (verified against the loaded map's `deterministic_hash`,
throwing on mismatch) and an out-param `WeightMapAttachmentDiagnostics*`
(`mapping_method`, `canonical_cell_count`, `instance_cell_count`,
`mapped_instance_cell_count`, `unused_canonical_cell_count`,
`missing_instance_cell_count`, `duplicate_instance_original_id_count`). Both are
default-valued, so all ~10 existing call sites across the FPP/DPV out-of-sample runners
compile unchanged. `MethodDispatcher::run_method` passes `request.weight_map_hash` and
captures diagnostics for the train-side attach call. Audited: no currently-supported
instance exercises a genuine subset case (`new_instances/*` compact universe always
equals the full physical `1..NCells`; the one legacy dataset with real non-fuel
exclusion, Sub20, has `node_mapper == available_nodes` exactly). This is confirmed
defensive/future-proofing work, validated by `test_weighted_subset_attachment.cpp`
using a synthetic instance whose canonical map genuinely has more cells than the
instance's compact universe.

## 6. Method CLI propagation

Audited and fixed a real, previously-latent gap: the Python generator's `method_flags()`
only recognized `FPP-SAA*`/`FPP-Branch-Benders*` labels — it had **no dispatch at all**
for DPV/Static-DPV/Greedy method families (discovered when the Phase 8B smoke's `DPV-SAA`
row was silently routed to `run-fpp-branch-benders-oos`). Fixed by adding an explicit
`dpv_command_by_method` map (`Static-DPV → run-static-dpv-oos`, `Static-DPV-MIP →
run-static-dpv-mip-oos`, `Greedy-DPV2`/`Greedy-DPV3 → run-greedy-oos` with a new
`greedy_metric` manifest column, `DPV-SAA → run-dpv-saa-oos`, `DPV-Benders →
run-dpv-benders-oos`, `DPV-Branch-Benders* → run-dpv-branch-benders-oos`) and a dedicated
worker-side `build_dpv_family_command()` that emits the minimal argument set these
commands actually accept (audited from `src/main.cpp`: none of them accept
`--risk-measure`/`--cvar-beta`/`--cvar-lambda` — DPV-CVaR is out of scope by design — and
the pure-heuristic commands additionally accept no `--time-limit`/`--mip-gap`/
`--threads`). `--weight-map-file` and `--dpv-ignition-policy fpp-safe` are always passed
for the DPV family.

## 7. Capability filtering

Unchanged mechanism from Phase 8A (`weighted_method_supported()` in the generator,
mirroring `experiments::weighted_method_capability` in C++); a new
`test_weighted_capability_cross_check.cpp` asserts the C++ capability matrix's rejection
wording for non-homogeneous combinatorial+LLBI/dominance stays keyword-consistent with
the real guard strings in `src/benders/FppCombinatorialBenders.cpp`, and that homogeneous
combinatorial+LLBI/dominance remain supported (the guard is non-homogeneous-only).

## 8. Train/test ID propagation

Unchanged: the worker reads `train_split_path`/`test_split_path` written once by the
generator and never resamples; `validate_split` enforces exact pool membership,
disjointness, and no duplicates. `train_ids`/`test_ids` are recorded verbatim in every
result (JSON and CSV).

## 9. Paired-instance resolution

`resolve_paired_reburn_instance()` (new, worker) replaces the Phase 8A
`pair_to_reburn_matrix()` (removed as dead code once superseded). In addition to the
`_reburn` folder-suffix convention, it cross-checks `declared_cells` between the reduced
and reburn instance-config rows and returns one of `resolved | unavailable |
cell_count_mismatch | not_applicable`, recorded verbatim as
`paired_reburn_instance_requested`, `paired_reburn_instance_resolved`,
`paired_reburn_resolution_method`, `paired_reburn_resolution_status`. When
`paired_evaluation_enabled` is set in the manifest but resolution is rejected, the row is
marked `paired_reburn_status=unavailable` with a `failure_stage=paired_instance_resolution`
— it never silently proceeds as unpaired. Deeper cross-validation (re-deriving and
comparing the reburn instance's own canonical identity via an extra `ensure-weight-map`
call) is a possible Phase 9 hardening, deferred here since the registry only guarantees
one identity per family by construction whenever both members are generated from the
same instance-config family key.

## 10. Firebreak transfer by original Cell2Fire ID

### 10.1 A real, previously-latent bug found and fixed

`build_paired_reburn_evaluation_command()` passed the *path* to the solution CSV as the
CLI `--firebreaks` value, but `core::FirebreakSolution::from_csv` (despite its name)
parses a **literal comma-separated ID string**, never a file path — so every paired
reburn evaluation the worker ever attempted failed with "Invalid firebreak node token:
<path>". This was invisible in Phase 8A because that phase's smoke called the CLI
directly with correctly-formatted literal IDs, never exercising the worker's own
command-building code. Fixed: a new `read_selected_firebreak_ids()` reads the solution
CSV (a single comma-separated line of original Cell2Fire IDs — never compact indices,
confirmed against `io::save_firebreak_solution_csv`), validates no duplicates, and the
worker passes that literal ID list to `--firebreaks`. The selected IDs are also recorded
verbatim as `selected_firebreak_original_ids` on the primary result row.

### 10.2 Missing-cell handling

`EvaluationRunner` gained `EvaluationOptions::require_full_firebreak_coverage` and CLI
`--require-full-firebreak-coverage`. `compact_firebreaks_for_recourse` now always reports
`paired_selected_firebreak_count/mapped/missing` and a `missing_original_ids` list in the
evaluation JSON; when strict mode is set (always used by the paired reburn call), any
missing selected ID is a hard `runtime_error` naming the exact missing IDs, never a
silent drop. Verified manually: strict mode raises
`"...1 selected original cell ID(s) are absent...: 999999."`; lenient mode reports
`paired_selected_firebreaks_missing: 1` in the JSON instead of silently dropping.

## 11. Standard OOS vs. paired reburn evaluation

Kept structurally separate as in Phase 8A: `train_expected_burned_area`/
`test_expected_burned_area`/etc. (standard OOS, from the solver's own in-process
evaluation) are never overwritten by `paired_reburn_train_*` fields (populated only from
the separate `evaluate` subprocess call's JSON). New fields this phase:
`optimization_weight_map_hash`, `out_of_sample_weight_map_hash`,
`paired_reburn_weight_map_hash` — verified identical to `weight_map_hash` (the manifest's
expected hash) for every row in the smoke.

## 12. Atomic result writing

`write_worker_csv()` now writes to `<path>.tmp<pid>`, flushes, `fsync`s, then
`os.replace()`s onto the final path — an interrupted worker leaves an identifiable
`.tmp<pid>` artifact, never a partially-written final CSV.

## 13. Resume, retry, and status taxonomy

`row_complete_and_valid(row)` (replaces the Phase 8A/legacy `row_complete`, which
returned `bool(solver_status or status)` regardless of its own return-code checks — a
pre-existing dead-branch bug masked because the loose check happened to still "work") now
requires: `worker_return_code == "0"`; the result JSON exists and parses; its `run_id`
matches the manifest row's; its `weight_map_hash` matches the expected hash (when
nonempty); a nonempty `solver_status`/`status`; `objective_validation_passed` present in
the JSON; the solution CSV exists; and (`paired_evaluation_enabled` ⇒
`paired_reburn_status == "ok"`).

Per-row classification in `main()`:

- **complete + valid** → skip (`SKIP complete`).
- **missing** → run (`resume_action=run_missing`, `attempt=1`).
- **recorded clean failure** (`worker_status=failed` and nonzero `worker_return_code`) →
  skip by default (`SKIP failed (rerun with --retry-failed)`); rerun only with
  `--retry-failed` (`resume_action=retry_failed`, `attempt` incremented, **same
  `run_id`/train_ids/weight_map_path preserved** — nothing about the row's logical
  identity changes).
- **anything else present but not valid** (malformed JSON, mismatched `run_id`, missing
  solution CSV, etc.) → always rerun regardless of `--retry-failed`
  (`resume_action=rerun_invalid`) — "clean safely" per the spec, since it is not a clean
  recorded failure.

New `--retry-failed` CLI flag (mutually exclusive with `--rerun-existing`, which still
discards *all* rows including successes). Status taxonomy: `execution_status` is
`heuristic_completed` when `solver_status` is `NotApplicable`/empty (pure heuristics:
`Static-DPV`, `Static-DPV-MIP`, `Greedy-DPV2/3`), otherwise the solver's own status
string (`Optimal`, `Feasible`, etc.) — heuristics are never labeled "optimal".

## 14. Failure-stage reporting

New fields `failure_stage`, `failure_type`, `failure_message`, `worker_exit_code`,
`attempt`, `resume_action`. Stages recorded: `manifest_validation` (split loading),
`weight_map_loading` (missing/invalid map, caught around `build_command`),
`result_validation` (post-solve JSON/hash mismatch), `paired_instance_resolution`
(rejected pairing), `paired_evaluation` (nonzero reburn-eval exit). `classify_failure()`
also best-effort-classifies a nonzero solver exit by grepping the log tail for known
phrases (weight-map hash mismatch, missing weight map, missing firebreak coverage,
instance-mapping errors) before falling back to generic `solver_execution`/`nonzero_exit`.

## 15. Legacy homogeneous compatibility

A manifest generated without `--weight-registry` carries `weight_profile=homogeneous` and
an empty `weight_map_path`; `weight_map_file(row)` returns `""` and no `--weight-map-file`
flag is passed, so the solver uses its own default homogeneous behavior. Verified by
`test_legacy_homogeneous_row_executes` in
`scripts/test_weighted_manifest_worker.py`.

## 16. Batch-family integration (merge / compact schema / shell launchers)

- **`scripts/merge_fpp_new_instances_scaling_experiment.py`**: `logical_key()` now
  includes `weight_profile`/`weight_replicate`/`weight_map_hash` — without this fix,
  merging more than one weight profile/replicate of the same base row triggered a hard
  `"Duplicate logical rows found"` `SystemExit` (a real functional blocker discovered
  while preparing the multi-profile smoke). Also removed an over-constrained assertion
  (`expected != expected_methods`) that assumed every worker has the same row count as
  the manifest's global distinct-method count — false under capability filtering, since
  different weight profiles can legitimately filter different method subsets for the
  same instance/train/alpha/case group; the existing per-worker `len(rows) != expected`
  check already covers real corruption.
- **`scripts/fpp_new_instances_scaling_compact_schema.py`**: `COMPACT_CSV_FIELDS` gained
  the weight/paired-reburn/execution-status columns, and `compact_row()` now explicitly
  populates them (including the four `paired_reburn_train_*` fields, which were declared
  in the schema but never actually populated before this phase — a pre-existing gap fixed
  as part of this work since it directly affects "metadata survives to result").
- **Shell launchers** (`run_fpp_new_instances_scaling_experiment.sh` and
  `_sub20.sh`, kept in lockstep): new env vars `WEIGHT_PROFILES` (default
  `homogeneous`), `WEIGHT_REPLICATES` (`0`), `WEIGHT_SEED_BASE` (`12345`),
  `WEIGHT_REGISTRY` (unset ⇒ legacy homogeneous, no weight flags passed at all),
  `GENERATE_MISSING_WEIGHT_MAPS` (`0`), `PAIRED_REBURN_EVALUATION` (`0`), `RETRY_FAILED`
  (`0`, mapped to worker `--retry-failed`). `RERUN_EXISTING=1` + `RETRY_FAILED=1`
  together is rejected with a clear error before anything runs. The resolved-config echo
  block reports `weight profiles: ...` and `resume mode: ...`.

## 17. Tests

### C++ (already present pre-interruption, re-verified this phase)

`test_weighted_batch_config`, `test_weighted_subset_attachment`,
`test_weighted_capability_cross_check` — all registered in the Makefile, all pass.

### Python (new this phase)

`scripts/test_weighted_manifest_worker.py` — invokes the real
`generate_fpp_new_instances_scaling_manifests.py` + `run_fpp_new_instances_scaling_manifest_worker.py`
against a live `build_gpp/firebreak_cpp` (skips cleanly if unbuilt):

1. `test_missing_weight_map_fails_per_row_not_whole_worker` — moves the registry aside;
   verifies both rows fail with `failure_stage=weight_map_loading` and the worker never
   crashes with a raw traceback.
2. `test_resume_skips_completed_valid_rows` — a second invocation performs zero `START`s.
3. `test_retry_failed_reruns_only_failed_row_same_run_id` — corrupts one row to a clean
   failure; verifies default-skip, then `--retry-failed` reruns it with `attempt=2` and
   the identical `run_id`.
4. `test_malformed_result_reruns_without_retry_flag` — corrupts `run_id` in the CSV
   (simulating a stale/mismatched result); verifies it reruns without needing
   `--retry-failed`.
5. `test_one_hash_through_all_stages_and_firebreak_transfer` — verifies
   `optimization_weight_map_hash == out_of_sample_weight_map_hash ==
   paired_reburn_weight_map_hash == weight_map_hash`, `paired_reburn_status == "ok"`,
   zero missing firebreaks, and that `selected_firebreak_original_ids` is recorded.
6. `test_heuristic_execution_status_not_optimal` — `Static-DPV` reports
   `solver_status=NotApplicable`, `execution_status=heuristic_completed`.
7. `test_legacy_homogeneous_row_executes` — a manifest generated without
   `--weight-registry` executes successfully with an empty `weight_map_path`.

`scripts/test_weight_manifest_generation.py` (Phase 8A, re-verified unaffected by the
Phase 8B `method_flags()` changes).

## 18. Small end-to-end batch smoke

`scripts/weighted_phase8b_batch_smoke.sh` — for `new20x20`/`new20x20_reburn`, each of
homogeneous/heterogeneous/clustered, and the full method panel (`FPP-SAA`,
`FPP-Branch-Benders-Combinatorial`, `DPV-SAA`, `Static-DPV`), runs the **full
manifest-generator + worker pipeline** (not direct CLI calls) and verifies map
pre-generation, one hash across optimization/OOS/paired-reburn for every row, zero
missing firebreaks, a resume pass that skips every row, and a controlled
retry-failed cycle that reruns only the corrupted row with its original `run_id`.

```bash
scripts/weighted_phase8b_batch_smoke.sh
```

---

## 19. Deviations

- The C++ `BatchExperimentRunner::run()` retains its existing fail-fast behavior (a
  method-dispatch exception aborts the whole batch) — this was existing, established
  behavior in a secondary/less-exercised C++ path; the resume/retry/continue-on-failure
  work in this phase targets the Python worker pipeline, which is the actually-used
  production path (per `README.md`).
- Deeper reburn canonical-identity cross-validation (an extra `ensure-weight-map`
  subprocess call per row to independently re-derive and compare the reburn instance's
  canonical id) was scoped down to a cheap `declared_cells` cross-check; noted as a
  possible Phase 9 hardening.
- `run-fpp-restricted-branch-benders-oos` dispatch was not added to the generator/worker
  (not present in the default method panel; restricted-candidate combinatorial
  combinations remain unsupported per the capability matrix regardless).

## 20. Remaining Phase 9 work

Final cross-experiment aggregation/merged-schema redesign, scientific summary tables,
performance profiles, statistical tests, publication figures, and large experiment
matrices are explicitly out of scope and untouched.

## 21. Validation

```bash
python3 -m py_compile \
  scripts/generate_fpp_new_instances_scaling_manifests.py \
  scripts/run_fpp_new_instances_scaling_manifest_worker.py \
  scripts/merge_fpp_new_instances_scaling_experiment.py \
  scripts/analyze_fpp_new_instances_scaling_experiment.py \
  scripts/fpp_new_instances_scaling_compact_schema.py \
  scripts/test_weight_manifest_generation.py \
  scripts/test_weighted_manifest_worker.py
bash -n scripts/run_fpp_new_instances_scaling_experiment.sh
bash -n scripts/run_fpp_new_instances_scaling_experiment_sub20.sh
bash -n scripts/weighted_phase8b_batch_smoke.sh

make build
CPLEX_STUDIO_DIR=/opt/ibm/ILOG/CPLEX_Studio2211 make cplex
make test

python3 scripts/test_weight_manifest_generation.py
python3 scripts/test_weighted_manifest_worker.py
scripts/weighted_phase8b_batch_smoke.sh

git diff --check
git diff --cached --check
```

All commands above passed: `make test` ran 91 binaries with exit 0 (including
`test_weighted_batch_config`, `test_weighted_subset_attachment`,
`test_weighted_capability_cross_check`); both Python self-tests passed (7/7 and 3/3
cases respectively); the batch smoke passed for all three profiles; both whitespace
checks were clean.
