# Weighted Landscapes: Consolidated Guide

This is the single authoritative description of the weighted-landscape
system as it exists today (Phase 10). It describes the current system, not
the chronology of how it was built — see `docs/WEIGHTED_LANDSCAPES_PHASE*.md`
for the historical record and `docs/WEIGHTED_LANDSCAPES_MIGRATION.md` for
upgrading legacy experiments.

## 1. Conceptual model

The Firebreak Placement Problem (FPP) selects a budget-constrained set of
firebreak cells to minimize wildfire loss over a stochastic set of ignition
scenarios. Historically every burned cell counted equally (unit weight).
Weighted landscapes replace that with a per-cell weight `w_k` representing
the relative loss value of cell `k` (e.g. structures, high ecological
value), so the optimization and every evaluation metric reflect *loss*, not
raw burned-cell count.

## 2. Wildfire-loss weights

For a firebreak placement `y` and scenario `s`, weighted wildfire loss is:

```
L_s(y) = sum_k  w_k * x_k^s(y)
```

where `x_k^s(y) ∈ {0,1}` indicates cell `k` burns under scenario `s` given
placement `y`. This is the single canonical objective quantity threaded
through every method family. A separate, always-available quantity is the
**unweighted physical burned-cell count** (`sum_k x_k^s(y)`), which is
never conflated with `L_s(y)` — see section 20 (terminology).

## 3. Canonical physical-cell identity

Every physical landscape (a Cell2Fire forest/results pair) has a
**canonical landscape identity** independent of method, objective, or
scenario split: `core::CanonicalLandscape` derives `grid_rows`, `grid_cols`,
`cell_count`, and an FNV-1a-64 `universe_hash` over the sorted
`(original_cell_id, row, col)` triples, combined into
`canonical_landscape_id = "<family>__<rows>x<cols>__<hash16>"`. A reduced
instance and its paired reburn instance share the same physical universe
and therefore the same `canonical_landscape_id`.

## 4. Propagation convention

Cell2Fire scenario message files are treated as directed propagation graphs
(fire spreads along directed arcs from ignition; a cell either burns or
doesn't per scenario). This convention is unchanged by weighting — weights
attach to cells, not to the propagation graph structure.

## 5. Supported landscape profiles

Three weight-generation profiles, each a first-class registry entry (no
"empty"/implicit special case):

- **homogeneous** — every cell weight 1.0 (a real, hashed map file, not a
  skipped step).
- **heterogeneous** — i.i.d. draw per cell in `[weight_min, weight_max]`.
- **clustered** — background weight everywhere, plus a configurable number
  of high-value clusters (8-neighbor grid adjacency, Chebyshev-distance
  seed separation) with elevated weight.

All profiles are mean-1 normalized by default.

## 6. Weight-map generation

```bash
./build_gpp/firebreak_cpp generate-weight-map \
    --landscape new20x20 --weight-profile clustered --weight-seed 12345 \
    --weight-cluster-count 3 --weight-cluster-fraction 0.1 \
    --output-csv weights.csv --output-json weights_meta.json
```

Key flags: `--landscape`, `--weight-profile {homogeneous,heterogeneous,clustered}`,
`--weight-seed`, `--weight-min`/`--weight-max` (heterogeneous),
`--weight-cluster-count`/`--weight-cluster-fraction`/
`--weight-background-min`/`--weight-background-max`/`--weight-cluster-min`/
`--weight-cluster-max`/`--weight-cluster-min-separation` (clustered),
`--weight-normalize`, `--forest-path`, `--results-path`,
`--output-csv`/`--output-json`.

Seeds are never wall-clock-derived: `derive_weight_generation_seed()` folds
`canonical_landscape_id + profile + replicate + generator_version` through
FNV-1a-64, so the same logical request always regenerates the same map.

## 7. Map registry

```bash
./build_gpp/firebreak_cpp ensure-weight-map \
    --landscape new20x20 --weight-profile heterogeneous --weight-replicate 0 \
    --weight-registry weight_maps/
```

`experiments::WeightMapRegistry` stores entries at
`weight_maps/<canonical_landscape_id>/<profile>/replicate_<r>/{weights.csv,metadata.json}`
(no method/objective in the path — the registry is purely physical-map
identity). `ensure()` is idempotent: validate-or-generate, atomic
temp-file+rename write, **hard error on any parameter mismatch, never a
silent regenerate**. Concurrent generation of the *same* logical entry from
two processes is not supported — pre-generate maps in a single process
before launching parallel workers (`--generate-missing-weight-maps` on the
Python manifest generator does this once, up front).

## 8. Reduced/reburn mapping

`experiments::PairedInstanceWeightMapping` maps one canonical weight map
onto a (possibly reduced) instance's compact universe by **original
Cell2Fire cell ID** (never compact index). `compare_reduced_reburn()`
asserts `shared_weight_mismatch_count == 0` between a reduced instance and
its reburn pair — both must read the identical weight for every physically
shared cell. A coordinate-based fallback mapping exists for when stable IDs
are unavailable, and is always explicit (never a silent best-effort guess).
The worker resolves the reburn instance by naming convention
(`resolve_paired_reburn_instance`) and transfers the reduced instance's
*selected* firebreaks by original cell ID; a selected cell absent from the
reburn universe is a hard failure under
`--require-full-firebreak-coverage`, never a silently dropped cell.

## 9. Supported FPP methods

| Method (CLI command / method label) | Weighted? | Notes |
|---|---|---|
| `solve-fpp-saa` / `run-fpp-saa-oos` (`FPP-SAA`) | Yes | direct CPLEX model |
| `run-fpp-benders-oos` (`FPP-Benders`) | Yes | explicit-loop Benders |
| `run-fpp-branch-benders-oos` (`FPP-Branch-Benders`, `...-Combinatorial`) | Yes | callback Branch-and-Benders; combinatorial variant via `--use-combinatorial-benders` |
| `run-fpp-restricted-branch-benders-oos` (`FPP-Restricted-...`) | Yes, with restrictions | see section 15 |
| Greedy warm-start / local search | Yes | via `--enable-greedy-warm-start`/`--enable-local-search` on Branch-Benders |

## 10. Supported DPV methods

| Method (CLI command / label) | Objective space | Notes |
|---|---|---|
| `run-static-dpv-oos` (`Static-DPV`) | heuristic construction | uses weighted DPV score, ranks by destination-weighted downstream value |
| `run-static-dpv-mip-oos` (`Static-DPV-MIP`) | heuristic construction | MIP selection over DPV scores |
| `run-greedy-oos --metric DPV2` / `DPV3` (`Greedy-DPV2`/`Greedy-DPV3`) | heuristic construction | greedy marginal-DPV selection |
| `run-dpv-saa-oos` (`DPV-SAA`) | DPV surrogate optimization | direct CPLEX model over the DPV surrogate objective |
| `run-dpv-benders-oos` (`DPV-Benders`) | DPV surrogate optimization | explicit-loop Benders decomposition of DPV-SAA |
| `run-dpv-branch-benders-oos` (`DPV-Branch-Benders`) | DPV surrogate optimization | callback Branch-and-Benders decomposition; supports DPV LLBI |

All DPV variants use the **product-pair** destination-weighting rule
`w(descendant) * z[source,successor,descendant]` (never a descendant-set
sum — that would double- or under-count multiplicity). `--dpv-ignition-policy
{fpp-safe,legacy}` controls ignition-cell protection semantics; production
default is `fpp-safe`.

## 11. Supported risk measures

`--risk-measure {expected,cvar,mean-cvar}`, `--cvar-beta` (tail confidence),
`--cvar-lambda` (mean-CVaR blend weight). Threaded consistently through
every solver family from the direct SAA model through callback
Branch-and-Benders, restricted-candidate, and combinatorial variants, all
cross-validated against `FppRecourseEvaluator`'s independent evaluation
within tolerance `abs_diff <= 1e-6 OR rel_diff <= 1e-6`
(`objective_validation_passed` in the result schema — **only a meaningful
correctness signal for exact-FPP rows**, see section 20).

## 12. LLBI support

Four LLBI (Lifted Lower Bound Inequalities — despite the name, these
compute a downstream-union/coverage/path lower bound, not an exact
per-singleton LP solve) families, all weighted for the **callback and
explicit-loop, non-restricted** solver paths only:

- Standard LLBI (`--use-lifted-lower-bounds`) — downstream-union bound.
- CoverageLLBI (`--use-coverage-llbi`) — per-cell-capped downstream coverage bound.
- PathLLBI (`--use-path-llbi`) — directed-simple-path burning lower bound.
- Projected CoverageLLBI/PathLLBI (`--use-projected-coverage-llbi-{exp,poly}`,
  `--use-projected-path-llbi-{exp,poly}`) — root-separated (`exp`) or static
  (`poly`) variants; projected CoverageLLBI and PathLLBI are mutually
  exclusive (only one projected family active at a time).

**None of the four LLBI families are supported for the restricted-candidate
solver** with a non-homogeneous weight map (section 15) — the restricted
master only holds variables for active candidates, and no safe mechanism
retains coefficients for inactive ones.

## 13. Combinatorial support

`--use-combinatorial-benders` enables an integer-incumbent path-activation
cut for the callback Branch-and-Benders solver, weighted throughout:

- Lifting (`--combinatorial-benders-lift {none,posterior,heuristic}`):
  **both non-`none` modes are conservative path-level cut deduplication,
  not true sequential/exact lifting or a heuristic coefficient estimator**
  — the option names are legacy labels; there is no `exact`/`sequential`
  mode in the parser.
- Initial cuts (`--combinatorial-benders-initial-cuts`) — pre-solve, static,
  globally valid.
- Fractional cuts (`--combinatorial-benders-separate-fractional`) —
  relaxation-callback user cuts, convex-hull-valid.
- Scenario ordering (`--combinatorial-benders-scenario-order {eta-asc,eta-desc}`)
  and exact sampling-first separation
  (`--combinatorial-benders-cut-sampling-ratio r`, `r ∈ (0,1]`, with
  mandatory full-fallback verification before any cut is accepted — there
  is no heuristic incomplete-sampling mode).

**There is no dedicated combinatorial root-only cut mechanism.**
`--use-root-user-cuts` is a separate, LP-dual Benders mechanism and is
**rejected in combination with combinatorial Benders**, as are LLBI, global
dominance, and conditional zero-benefit fixing combined with combinatorial
Benders, and combinatorial Benders on the restricted-candidate path.

## 14. Dominance support

`--use-global-dominance-preprocessing` — set-inclusion-based candidate
dominance, weight-independent by construction (any positive weights),
supported for callback and restricted-candidate FPP, **never combined with
combinatorial Benders**. `--use-conditional-zero-benefit-fixing` — a
structural detector only: it correctly identifies weight-safe zero-benefit
candidates but applies **zero actual variable fixings** (the CPLEX generic
callback path does not safely expose the required node-local bound
tightening). Neither combines with combinatorial Benders.

## 15. Known unsupported combinations

| Combination | Status |
|---|---|
| Restricted-candidate + any LLBI family | Unsupported (non-homogeneous maps) |
| Restricted-candidate + combinatorial Benders | Unsupported |
| Restricted-candidate + global dominance / conditional zero-benefit | Unsupported |
| Restricted-candidate permanent candidate pruning/bounds (weighted) | Disabled by design — no valid safe bound proven for arbitrary weights |
| Combinatorial Benders + LLBI (any family) | Unsupported |
| Combinatorial Benders + global dominance | Unsupported |
| Combinatorial Benders + conditional zero-benefit fixing | Unsupported |
| Combinatorial Benders + `--use-root-user-cuts` | Unsupported (no combinatorial root-cut mechanism exists) |
| DPV-CVaR (CVaR risk measure with any DPV surrogate optimization) | Not implemented — rejected explicitly ("DPV-CVaR optimization is out of scope and not implemented") |

Every rejection above is enforced by
`experiments::weighted_method_capability()` (see section 8 of the CLI/
capability audit) and produces an explicit error, never a silent fallback
to unit weights or a silently-ignored flag.

## 16. Experiment workflow

```
generate/ensure weight map (once, pre-launch)
  -> generate manifest (Python; resolves splits, weight entries, capability filtering)
  -> run worker(s) over manifest rows (resume/retry-aware)
  -> merge (Phase 9A) -> analyze (Phase 9B)
```

## 17. Manifest workflow

`scripts/generate_fpp_new_instances_scaling_manifests.py` resolves
instance/method/split/weight combinations into a `full_task_manifest.csv`
plus per-worker manifest shards. Weight-relevant flags:
`--weight-profiles`, `--weight-replicates`, `--weight-seed-base`,
`--weight-registry`, `--generate-missing-weight-maps`,
`--paired-reburn-evaluation`. Rows whose (method, profile, objective,
strengthening) combination is unsupported per `WeightedMethodCapability`
are filtered out of the manifest before any worker runs, not filtered at
solve time.

## 18. Worker workflow

`scripts/run_fpp_new_instances_scaling_manifest_worker.py` executes one
manifest shard, one task at a time: dispatches the C++ binary, writes
atomic (temp+fsync+rename) CSV rows, computes a stable logical `run_id`
(suffixed `_wp<profile>_wr<replicate>_wh<hash8>` when weighted), and
supports resume (skip completed rows), `--retry-failed` (rerun only failed
rows, preserving the logical `run_id`, incrementing `attempt`), and
`--rerun-existing` (mutually exclusive with `--retry-failed`). The worker
**never regenerates a weight map** — a missing registry entry is a hard
per-row failure, not a silent on-the-fly generation.

## 19. Paired evaluation

For every worker row, the worker attempts to resolve a paired reburn
instance by naming convention (`<instance>_reburn`) **regardless of any
manifest-level "enabled" flag** — see section 20's terminology entry for
why this matters. On successful resolution it runs the same canonical
weight map against the reburn instance, transfers the reduced instance's
selected firebreaks by original cell ID, and requires zero missing
transferred cells. Paired-reburn metrics are reported in a namespace
entirely separate from standard out-of-sample (OOS) metrics — never
overwritten, never mixed (section 20).

## 20. Result schema

Every row is normalized into the Phase 9A canonical schema
(`scripts/weighted_result_schema.py`, `result_schema_version =
"weighted-result-9a.1"`). Central terminology, unambiguous across C++
JSON/CSV, manifests, scripts, and documentation:

- **Weighted wildfire loss** `L_s(y) = sum_k w_k x_k^s(y)` — the true
  scientific objective. Canonical fields: `weighted_fpp_expected_in_sample`
  / `_out_of_sample` / `_paired_reburn`, and the CVaR/mean-CVaR
  counterparts.
- **Physical burned-cell count** — unweighted `sum_k x_k^s(y)`. Canonical
  fields: `mean_burned_cells_in_sample` / `_out_of_sample` / `_paired_reburn`.
  Never labeled as, or conflated with, weighted loss.
- **DPV surrogate objective** — a structural proxy objective solved by DPV
  decomposition methods, *not* wildfire loss. Canonical field:
  `dpv_surrogate_objective` (+ `_best_bound`/`_gap`). As of this phase, the
  generic `solver_weighted_objective` output field is populated **only**
  for exact-FPP rows and is left blank (NaN) for DPV rows at the source
  (section 10 of the Phase 10 completion report) — it is no longer
  necessary to infer this distinction purely at the Python normalization
  layer, though the normalization layer's own defensive check (deriving
  `solver_objective` from `dpv_surrogate_objective`'s presence, never
  trusting `solver_weighted_objective` at face value) is retained as
  defense in depth.
- **Standard OOS evaluation** — evaluation on the manifest's configured
  test scenarios, on the *same* (possibly reduced) instance that was
  optimized. Canonical namespace: `out_of_sample`.
- **Paired reburn evaluation** — evaluation of the *same selected
  firebreaks* on the physically paired reburn graph (a different,
  typically larger/different instance sharing the same canonical
  landscape). Canonical namespace: `paired_reburn`. **Important, verified
  quirk**: the raw `paired_evaluation_enabled` field is not a reliable
  signal that this ran — the worker resolves and runs paired-reburn
  evaluation by instance-naming convention independent of that flag. The
  reliable signal is `paired_reburn_evaluation_status` (`"ok"`/`"failed"`/
  `"unavailable"`/`"n/a"`).
- **Solver gap** — `solver_gap`/`solver_best_bound`: the gap reported by
  *one* optimization model against *its own* bound. Never a cross-method
  comparison.
- **Gap to best-known value** — `gap_to_best_known_feasible` /
  `gap_to_best_known_lower_bound` (Phase 9B): a cross-method scientific
  comparison against the best value found by **exact-FPP methods only**.
  Never populated from a DPV surrogate or heuristic row's own solve.
- **Best observed OOS/paired value** — `best_observed_out_of_sample_value`
  / `best_observed_paired_reburn_value` (Phase 9B): the best value observed
  among *all* method types in a compatible group. Explicitly **not** an
  optimality claim — never called "optimal" or "best known" in any table,
  plot, or diagnostic.

## 21. Merge pipeline

```bash
python3 scripts/merge_weighted_experiment_results.py \
    --input-root results/<experiment>/ --output-dir results/<experiment>/merged --strict
```

Normalizes every raw worker CSV row (preferring per-task JSON when it adds
lossless data, e.g. the paired-reburn weighted metrics that have no CSV
column today), validates schema version/identity/weight provenance/
paired-evaluation completeness/numeric well-formedness, classifies
duplicates (exact / retry attempt / conflicting / legacy collision), and
writes four deterministic artifacts: `merged_all_attempts.csv`,
`merged_current_valid.csv` (one row per logical run, latest valid attempt),
`merged_invalid.csv` (with reasons), `merge_diagnostics.json`. `--strict`
fails the run if any modern row is invalid.

## 22. Analysis pipeline

```bash
python3 scripts/analyze_weighted_experiment_results.py \
    --merged-current-valid results/<experiment>/merged/merged_current_valid.csv \
    --merged-all-attempts results/<experiment>/merged/merged_all_attempts.csv \
    --output-dir results/<experiment>/analysis
```

Reads only `merged_current_valid.csv` (rejecting any row that isn't itself
Phase-9A-valid), classifies methods (`exact_fpp`/`approximate_fpp`/
`dpv_optimization`/`heuristic`), computes best-known feasible values/lower
bounds/certification gaps (exact-FPP only), per-row gaps, OOS/paired
observed regret, method summary tables, win/tie/loss, paired statistical
comparisons (Wilcoxon with a dependency-free exact sign-test fallback, Holm
correction), Dolan-More runtime performance profiles and quality profiles,
and publication-ready CSV+LaTeX tables. See
`docs/WEIGHTED_LANDSCAPES_PHASE9B.md` for full detail.

## 23. Reproducibility requirements

- Weight-map generation seeds are deterministically derived, never
  wall-clock-based.
- `run_id` is deterministic given (instance, method, split, alpha, weight
  profile/replicate/hash) **within one manifest-generation invocation**;
  cross-invocation uniqueness is not guaranteed (documented, Phase 9A audit)
  — do not merge two independently-generated manifest runs covering
  overlapping combinations without checking for `run_id` collisions (the
  merge pipeline's "legacy collision" duplicate category exists to catch
  this if it happens).
- All CSV/JSON writes are atomic (temp file + rename).
- Merge and analysis outputs have a fixed, declared column order
  regardless of input row order.
- Non-deterministic fields that legitimately vary between runs: wall-clock
  timestamps (`worker_started_at_epoch`, etc.), absolute file paths embedded
  in `weight_map_path`/log paths, and solver wall-clock runtime (CPLEX
  runtime is not bit-reproducible across machine load, though the selected
  solution and objective are, for deterministic seeds/tolerances).

## 24. Validation commands

```bash
# C++ regression (non-solver + solver-linked variants)
make build
make cplex
make test

# Python regression (Phase 9A + 9B + Phase 10 focused suites)
python3 -m py_compile scripts/*.py
for f in scripts/test_weighted_*.py; do python3 "$f"; done

# Static checks
git diff --check
git diff --cached --check

# Unified release validation (Phase 10)
scripts/validate_weighted_landscapes_release.sh --quick
scripts/validate_weighted_landscapes_release.sh --full --with-cplex
```

## 25. Troubleshooting

- **"Duplicate row for weight profile ... case ..."** during a manifest
  merge with the *old* `merge_fpp_new_instances_scaling_experiment.py`:
  that script's `logical_key()` was extended in Phase 8B to include weight
  fields; if you see this on modern data, check you're not mixing an old
  and a regenerated manifest. Prefer `merge_weighted_experiment_results.py`
  (Phase 9A) for weighted experiments.
- **A DPV row shows `objective_validation_passed=false`**: this is
  expected, not a bug — the DPV surrogate objective is never supposed to
  equal the true weighted loss. Only treat this as an error for `exact_fpp`
  rows.
- **`paired_evaluation_enabled=false` but paired metrics are populated
  anyway**: expected — see section 20's terminology entry. Check
  `paired_reburn_evaluation_status` instead.
- **A capability is rejected unexpectedly**: check
  `experiments::weighted_method_capability()` and section 15 above before
  assuming it's a bug — several combinations are deliberately unsupported.
- **Weight map "hard error on mismatch"**: the registry never regenerates
  silently; if parameters changed, use a different `--weight-replicate` or
  regenerate the registry entry explicitly (delete and re-`ensure`), don't
  expect in-place overwrite.
- **`merged_current_valid.csv` row rejected by the analysis CLI**: the
  analysis CLI requires every row to itself carry a valid
  `result_schema_version` and `validation_classification` — this should
  never happen for a file the merge pipeline itself produced; if it does,
  something modified the file after the merge step.
