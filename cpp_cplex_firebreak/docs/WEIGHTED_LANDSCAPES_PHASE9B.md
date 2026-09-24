# Phase 9B: Scientific Comparison, Best-Known References, Gaps, Profiles, and Publication Tables

Phase 9B builds a deterministic analysis layer on top of Phase 9A's
`merged_current_valid.csv`. It computes best-known feasible values and lower
bounds (exact-FPP methods only), certification gaps, per-row optimality/
quality gaps, out-of-sample and paired-reburn observed regret, method-level
summary tables, win/tie/loss counts, paired statistical comparisons with
multiple-comparison correction, Dolan-More performance profiles, quality
profiles, and publication-ready CSV/LaTeX tables. It never runs a solver,
never regenerates a weight map, and never computes a final manuscript
conclusion — that remains Phase 10.

## 1. Audit of existing analysis/plotting/best-known precedents

### 1.1 Phase 9A modules (reused, not duplicated)

`scripts/weighted_result_schema.py`, `weighted_result_normalize.py`,
`weighted_result_merge.py`, `merge_weighted_experiment_results.py` are
imported directly by every Phase 9B module. Phase 9B adds exactly one
additive extension to the Phase 9A schema module rather than a parallel
implementation:

- **`read_canonical_csv()` / `rehydrate_canonical_row()`** (new): the
  inverse of the merge CLI's `_stringify()` — reads `merged_current_valid.csv`
  / `merged_all_attempts.csv` back into typed records using the *same*
  `CANONICAL_FIELDS` dtype declarations Phase 9A already owns.
  `require_valid=True` (the default, used for `merged_current_valid.csv`)
  raises `CanonicalRowRejected` if any row's `result_schema_version` or
  `validation_classification` is not itself Phase-9A-valid — this is how
  Phase 9B "rejects input that does not pass Phase 9A validation" (section 3).
- **`ComparisonGroupKey` extended** with `canonical_landscape_id`,
  `instance_family`, `weight_profile`, `weight_replicate`, and
  `train_scenario_count` — Phase 9A section 23 explicitly deferred the full
  best-known-FPP group key shape to Phase 9B; this is that continuation.

### 1.2 A real Phase 9A defect found and fixed during this phase

Cross-checking `is_fully_paired_valid_result()` / `validate_record()`
against the real `results/weighted_phase8b_smoke/` fixture found that
**`paired_evaluation_enabled` is `false` for every row in that fixture**,
while `paired_reburn_evaluation_status` is `ok` for every row and real
paired-reburn data was collected throughout. The worker resolves and runs
the paired-reburn evaluation by instance-naming convention
(`resolve_paired_reburn_instance`) **independently** of the manifest's
`paired_evaluation_enabled` flag — that flag only affects whether a
*failed* resolution is treated as an error. Gating Phase 9A's paired
validation on `paired_evaluation_enabled` therefore silently produced zero
paired-valid rows on real data. Fixed at the source
(`weighted_result_schema.is_fully_paired_valid_result`,
`weighted_result_merge.validate_record`, and the new
`_paired_evaluation_attempted()` helper) to gate on
`paired_reburn_evaluation_status not in ("", "n/a")` instead — the real
signal. All 9 Phase 9A test files (105 assertions) still pass after this
fix; two test fixtures were updated to set `paired_reburn_status="ok"`
explicitly where they exercise paired validation, since the default
fixture value changed from `"ok"` to `"n/a"` to keep unrelated tests
decoupled from paired concerns under the corrected gate.

### 1.3 Legacy analysis/plotting precedents (git history, not present on disk)

No live Python best-known/performance-profile/plotting script exists in the
current checkout. Two deleted scripts were recovered from git history
(`git log --all --diff-filter=D`) and used as design precedent, not code:

- `scripts/plot_sub20_alpha001_002_performance_profiles.py` (deleted at
  `e939cbc2`): the exact Dolan-More step-function convention Phase 9B's
  `profile_points()` reuses — sorted unique finite ratios, log2 x-axis,
  `x_max = max(finite) * 1.08`, unsolved counted as `ratio=inf` in the
  denominator. Phase 9B's `weighted_analysis_profiles.profile_points()` is
  a direct, deliberate continuation of this convention.
- `scripts/plot_sub20_alpha001_002_gap_profiles.py` (same commit): the
  precedent for a cumulative fraction-within-threshold curve, generalized
  in Phase 9B into `build_cactus_coverage()` and the quality-profile ratio
  `q = 1 + max(0, gap)`.

`results/computational_study_fpp_risk_pilot/summaries/fpp_risk_performance_profile_data.csv`
(and its `_full`/`_smoke` siblings, still on disk) is an older, pre-weighted
performance-profile data convention (`best_objective_for_group`,
`objective_gap_to_best`, `runtime_ratio_to_best`,
`solved_to_target_quality_Xpct`) that conflated gap-to-best and runtime
ratio in one row. Phase 9B deliberately keeps these as **separate** artifacts
(`runtime_performance_profile.csv` vs. `quality_profile_*.csv`), per its own
explicit section 21/22 requirement.

The C++ `analysis::ExperimentAggregator` / `RuntimeProfiler` /
`BatchSummaryReporter` classes (`include/analysis/*.hpp`) operate on the
old, pre-weighted, non-canonical CSV schema and compute a simple
FPP-vs-DPV pairwise win/tie/loss (`AggregationSummary`). They are prior art
for the win/tie/loss *concept* but are not reusable code (different
language, different schema) — Phase 9B's `win_tie_loss()` is a fresh,
schema-aware implementation over the canonical Phase 9A record shape.

## 2-3. Pipeline shape and infrastructure reuse

```
raw results -> Phase 9A normalization/validation -> merged_current_valid.csv
            -> weighted_result_schema.read_canonical_csv() [rejects anything
               that isn't itself Phase-9A-valid]
            -> Phase 9B core analysis (weighted_analysis_core.py)
            -> summaries / statistics / profiles / tables / plots
```

No new result parser, schema, legacy migrator, or comparison-group
implementation was created. `weighted_analysis_core.py` imports
`weighted_result_schema` for `comparison_group_key()`,
`is_valid_exact_result()`, `is_valid_dpv_result()`,
`is_valid_heuristic_result()`, and the canonical field list; it adds only
namespace-generalized comparison keys (`NamespaceComparisonKey` for OOS/
paired) that Phase 9A never defined.

## 4. Canonical FPP comparison objective

`weighted_analysis_core.select_comparison_objective()` picks exactly one
field per row based on `risk_measure`:

| risk_measure contains | source field |
|---|---|
| "mean" and "cvar" | `weighted_fpp_mean_cvar_in_sample` |
| "cvar" | `weighted_fpp_cvar_in_sample` |
| otherwise (expected) | `weighted_fpp_expected_in_sample` |

Exposed as `fpp_comparison_objective_in_sample` (value),
`fpp_comparison_objective_in_sample_source` (which field was used), and
`fpp_comparison_objective_in_sample_rejection_reason` (set, never silently
blank, when the required metric is missing). Never the DPV surrogate
objective, never a physical burned-cell count, never an OOS metric.

## 5. Objective-space classification

`weighted_analysis_core.classify_method()` — precedence order, documented
in full in the function's own docstring:

1. `execution_status == "heuristic_completed"` → **heuristic** (checked
   first: Static-DPV also populates `dpv_surrogate_objective`, but is a
   construction heuristic, not a DPV MIP/Benders solve).
2. `dpv_surrogate_objective` populated (and not heuristic) → **dpv_optimization**.
3. `restricted_candidate_mode == "heuristic"` → **approximate_fpp**, even
   for an `FPP-`-prefixed method label.
4. method starts with `FPP` and `restricted_candidate_mode` in
   `{"none", "exact"}` → **exact_fpp**.
5. method starts with `FPP` but `restricted_candidate_mode == "enabled"`
   (ambiguous — restriction active, neither exact nor heuristic confirmed)
   → **approximate_fpp** (never assume exactness without positive
   confirmation).
6. anything else → **unclassified_method**, reported in diagnostics, never
   silently treated as exact.

This uses the Phase 9A `restricted_candidate_mode` / `dpv_surrogate_objective`
/ `execution_status` metadata fields, not bare substring matching, wherever
that metadata exists.

## 6. Comparison-group definition

`schema.comparison_group_key()` (extended in this phase, see section 1.1)
pins: `canonical_landscape_id`, `instance_family`, `instance_id`,
`train_scenario_ids`, `train_scenario_count`, `weight_profile`,
`weight_replicate`, `weight_map_hash`, `risk_measure`, `alpha`, `cvar_beta`,
`mean_cvar_lambda`, `budget`. Method is deliberately excluded. Verified by
test: different map hash / train split / risk configuration each produce
separate groups (`test_weighted_best_known_values.py`).

OOS and paired-reburn comparisons use a **separate**
`NamespaceComparisonKey` (namespace-scoped: `out_of_sample` uses
`test_ids`/`out_of_sample_weight_map_hash`; `paired_reburn` uses
`paired_reburn_scenario_ids`/`paired_reburn_weight_map_hash`) — reduced and
reburn evaluations never share a group with the in-sample key or with each
other.

## 7. Best-known feasible FPP value

`weighted_analysis_core.best_known_feasible()`: eligible set = rows with
`objective_space == exact_fpp`, `schema.is_valid_exact_result()` true, and a
finite `fpp_comparison_objective_in_sample`. Minimum value wins; ties
(within 1e-9) are all reported via `best_known_feasible_tied_run_ids`, with
a deterministic (method, run_id)-sorted winner for the single-value fields.
Returns `None`/empty when no exact reference exists (section 7: "do not
invent a reference").

## 8. Best-known lower bound

`best_known_lower_bound()`: same eligible-row filter as above, restricted
further to a finite `solver_best_bound` (non-finite values are already
excluded upstream by Phase 9A's strict numeric parser, so no additional
`math.isfinite` guard is needed here beyond the `None` check). Maximum
(tightest) bound wins, ties reported the same way.
`validate_bound_consistency()` checks `LB_bk <= z_bk + tolerance` and
reports groups that violate it (`bound_consistency_violations` in
diagnostics) rather than silently accepting an unsound bound.

## 9. Certification gap

`certification_gap()`: `Gap^BK = (z_bk - LB_bk) / max(|z_bk|, eps)`.
Certified optimal when this gap is within `--optimality-tolerance` (default
`1e-4`, documented in `weighted_analysis_core.DEFAULT_OPTIMALITY_TOLERANCE`)
**or** when the caller passes `proven_optimal=True` (used when the winning
best-known row's own `solver_status == "Optimal"`). A best-known feasible
value is never called optimal outside these two conditions.

## 10. Per-row gap to best-known feasible

`gap_to_best_known_feasible()`: minimization convention,
`(z - z_bk) / max(|z_bk|, eps)`. A raw value within
`--gap-tolerance` (default `1e-6`) below zero is reported as exactly `0.0`.
A **materially** negative gap (a DPV/heuristic row genuinely beating the
exact-only best-known reference — legitimate, since best-known is
deliberately exact-only) is preserved at its raw value and flagged via
`gap_to_best_known_feasible_flagged_negative=True`, never silently hidden
or clamped. This is a deliberate interpretation of the spec's "materially
negative gaps trigger a best-known recomputation or validation error":
Phase 9B does not retroactively fold a non-exact row into the exact-only
best-known reference (section 7 forbids this), so the row is flagged for
human/Phase-9B-consumer attention instead. Missing when no exact reference
exists (`gap=None`, never zero).

## 11. Gap to best-known lower bound

`gap_to_best_known_lower_bound()`: `(z - LB_bk) / max(|z|, eps)` — note the
denominator is the row's **own** value, not the best-known value (matching
the spec's formula exactly, distinct from section 10's denominator
convention). Never labeled a solver optimality gap for non-exact methods.

## 12. Out-of-sample observed-regret computation

`best_observed()` / `regret_to_best_observed()` over an OOS-namespace group
(`out_of_sample_comparison_key()`): pools **all** objective spaces (exact,
DPV, heuristic) — unlike best-known-feasible, "best observed" never
requires exactness and is never called optimal. Risk-consistent metric
selected the same way as section 4 (`oos_metric_field()`).

## 13. Paired-reburn observed-regret computation

Same `best_observed()`/`regret_to_best_observed()` machinery, restricted to
rows passing `paired_eligible()` (status `"ok"` and zero missing
transferred firebreaks — the real gate fixed in section 1.2). Uses
`paired_reburn_comparison_key()`. Never combined with the OOS namespace;
never called optimal.

## 14. Physical metric summaries

`weighted_analysis_summaries.physical_metric_summary()` is a fully separate
table from the weighted-loss method summary — verified by test
(`test_weighted_method_summary.py`) that a method can rank differently
under weighted loss vs. unweighted burned-cell count, and that neither
table's columns leak into the other.

## 15. Method summary tables

`method_summary()`: one row per (method, method_family, weight_profile,
risk_measure), reporting `instances_total/valid/optimal/feasible/
time_limit/failed`, runtime mean/median/geometric-mean, solver-gap mean/
median, gap-to-best-known mean/median/max, OOS/paired regret mean/median.
Never pools weight profiles or risk measures. `exact_method_summary()` /
`heuristic_dpv_summary()` provide the section 17/18-specified columns
(aggregated per method across profiles, per the spec's own column list,
which does not include weight_profile/risk_measure for these two tables —
documented as a deliberate scope choice; `method_summary()` remains the
fully disaggregated table when profile/risk granularity is needed).

## 16. Win/tie/loss

`win_tie_loss()` / `all_win_tie_loss()`: four separate comparisons
(in-sample weighted-FPP objective, OOS weighted objective, paired-reburn
weighted objective, runtime among mutually successful exact runs), each
using the appropriate namespace's comparison-group key so only genuinely
comparable rows are paired. A tolerance (`--gap-tolerance`, reused) decides
ties. Groups missing one of the two methods being compared are excluded
from `paired_observations`, never counted as a win or loss.

## 17. Statistical comparisons

`weighted_analysis_statistics.py`: SciPy availability is checked once at
import (`SCIPY_AVAILABLE`); Wilcoxon signed-rank
(`scipy.stats.wilcoxon(..., mode="auto")`) is used only when SciPy is
available **and** there are at least `min_pairs` observations **and** at
least two nonzero differences **and** the nonzero differences don't all
share one tied magnitude (a degenerate case where Wilcoxon reduces to the
sign test anyway). Otherwise, a fully dependency-free **exact two-sided
binomial sign test** (`math.comb`-based) is used — this is the deterministic
fallback, verified to run correctly with SciPy artificially disabled
(`test_weighted_statistical_comparisons.py`). The effect size (matched-pairs
rank-biserial correlation) is computed manually in both cases, so it is
directly comparable regardless of which test produced the p-value.
`DEFAULT_MIN_PAIRS = 6`; comparisons below this are skipped and reported as
such (`skipped=True`, `skip_reason` names the count), never silently run
anyway or silently omitted.

## 18. Multiple-comparison correction

`holm_correct_family()`: standard Holm-Bonferroni step-down procedure,
applied **within each (metric, weight_profile, risk_measure) family**
separately (`run_family_comparisons()` in `weighted_analysis_statistics.py`)
— heterogeneous and clustered profiles, or different risk measures, are
never pooled into one correction family. Skipped comparisons are excluded
from `m` and left with `adjusted_p_value=None`.

## 19. Runtime performance profiles

`weighted_analysis_profiles.build_runtime_performance_profiles()` builds a
**separate** profile per `(weight_profile, risk_measure)` stratum (a real
pooling bug across strata was caught and fixed during development — see the
git history of this file — before any test or plot was written against
it). Success criteria: `"optimal"` (solver_status == Optimal only) or
`"feasible_tolerance"` (any row with a `gap_to_best_known_feasible` within
`--gap-tolerance`/`feasible_tolerance`, regardless of the exact status
label — this is what makes a documented, gap-within-tolerance time-limited
row count as solved under that criterion, per section 21's "distinguish
optimal-only and feasible-within-tolerance profiles"). A method with no row
at all for an expected group (present via `merged_all_attempts.csv` for
other groups in the same stratum) is assigned `ratio = inf` (unsolved),
never dropped from the series. `profile_points()` reuses the exact
Dolan-More step-function convention recovered from git history (section
1.3).

## 20. Quality profiles

`build_quality_profiles()`: `q = 1 + max(0, gap)`, built separately per
`(weight_profile, risk_measure)` stratum (same pooling-bug fix as above)
and separately per namespace (`insample` uses `gap_to_best_known_feasible`;
`oos`/`paired` use the corresponding best-observed regret). Only
approximate/DPV/heuristic rows are included — `exact_fpp` rows never
appear in a quality profile. Never generated when no valid rows exist for
that namespace (an empty dict is returned, not a fabricated zero-row
profile).

## 21. Missing-data policy

- Rows with a required in-sample metric missing are excluded from
  best-known/gap computation with `fpp_comparison_objective_in_sample=None`
  and a recorded rejection reason, never coerced to a number.
- Runtime-performance-profile "expected" methods are derived per
  `(weight_profile, risk_measure)` stratum from the union of methods seen
  in `merged_current_valid.csv` **and** `merged_all_attempts.csv` for that
  stratum (documented, pragmatic heuristic — no separate "manifest of
  expected rows" is passed to the CLI). A method expected in a stratum but
  entirely absent for one specific group is still assigned `ratio = inf`.
- Paired statistical tests only ever use matched (both-methods-present)
  observations; the reduced `paired_n` is always reported, never inflated.
- Phase 9A's `merged_all_attempts.csv` vs. `merged_current_valid.csv`
  split is what prevents retry attempts from being double-counted in any
  Phase 9B summary — Phase 9B only ever reads `merged_current_valid.csv`
  for its row-level analysis (verified by
  `test_weighted_method_summary.py::test_retry_history_only_current_valid_enters_summaries`).

## 22. Replicate aggregation

Not implemented as a separate `replicate_aggregated` artifact in this
phase: the Phase 8B/9A smoke fixture has exactly one replicate (`replicate_0`)
per profile, so there is no real multi-replicate data to validate an
aggregation implementation against, and Phase 9B's comparison-group key
already keeps `weight_replicate` as a first-class, un-collapsed dimension
in every `replicate_level` table produced (the default, and the only mode
implemented). This is an explicit, documented deviation — see section 35
below and the completion report.

## 23. Output artifacts

```
analysis/
  analysis_diagnostics.json        (also duplicated under diagnostics/)
  diagnostics/analysis_diagnostics.json
  best_known/comparison_groups.csv
  best_known/best_known_values.csv
  gaps/row_level_gaps.csv
  summaries/method_summary.csv
  summaries/exact_method_summary.csv
  summaries/heuristic_dpv_summary.csv
  summaries/out_of_sample_summary.csv
  summaries/paired_reburn_summary.csv
  summaries/physical_metric_summary.csv
  summaries/win_tie_loss.csv
  statistics/statistical_tests.csv
  profiles/runtime_performance_profile.csv
  profiles/quality_profile_insample.csv
  profiles/quality_profile_oos.csv
  profiles/quality_profile_paired.csv
  profiles/cactus_coverage.csv
  tables/table_{exact_methods,heuristic_dpv_methods,out_of_sample,
                paired_reburn,best_known,statistical_comparisons}.{csv,tex}
  plots/performance_profile_runtime_<profile>_<risk>.{png,pdf}   (--generate-plots only)
  plots/quality_profile_{insample,oos,paired}_<profile>_<risk>.{png,pdf}  (--generate-plots only)
```

Every CSV in the mandatory list (Phase 9B section 29) is always produced;
`.tex` tables are produced by default (`--generate-latex`, on by default);
plots are opt-in (`--generate-plots`, off by default) since their
underlying CSV/profile data is already always available without them.

## 24. Analysis diagnostics

`analysis_diagnostics.json` reports: `schema_version`, `input_rows`,
`valid_rows`, `comparison_groups`, `groups_with_exact_reference`,
`groups_without_exact_reference`, `groups_with_certified_optimum`,
`groups_with_uncertified_best_known`, `rows_with_gap`, `rows_without_gap`,
`oos_groups`, `paired_groups`, `bound_consistency_violations` (with
reasons), `rows_flagged_negative_gap`, `statistical_comparisons_run`,
`statistical_comparisons_skipped` (+ `_skip_reasons`), `scipy_available`,
`performance_profiles_generated`, `quality_profiles_generated`,
`tables_generated`, `plots_generated`, `invalid_analysis_groups`,
`filters_applied`, and the tolerances/flags used for the run.

## 25. CLI

`scripts/analyze_weighted_experiment_results.py`:

```bash
python3 scripts/analyze_weighted_experiment_results.py \
    --merged-current-valid results/.../merged/merged_current_valid.csv \
    --merged-all-attempts results/.../merged/merged_all_attempts.csv \
    --output-dir results/.../analysis \
    --strict
```

Flags: `--weight-profile`/`--risk-measure`/`--scenario-count`/`--method`
(repeatable filters, recorded in diagnostics), `--gap-tolerance`,
`--optimality-tolerance`, `--statistical-min-pairs`, `--holm-correction`/
`--no-holm-correction` (default on), `--generate-plots` (default off),
`--generate-latex`/`--no-generate-latex` (default on), `--strict` (nonzero
exit if any comparison group fails `LB <= best-known + tolerance`). No
solver execution, no weight-map generation. `merged_current_valid.csv` is
read with `require_valid=True`, so any row that didn't pass Phase 9A
validation fails the run immediately with a clear error (exit code 2).

## 26. Tests

12 focused Python test scripts under `scripts/` (co-located with the
Phase 9A test-script convention), plus `weighted_analysis_test_helpers.py`
(synthetic canonical-record builders, not a test itself). All 22 mandatory
cases (32.1-32.22) are covered; 138 assertions total, all passing.
`test_weighted_analysis_end_to_end.py` additionally builds a 10-instance
synthetic fixture specifically because the real Phase 8B smoke fixture (one
instance per weight profile) cannot produce enough paired observations for
a real (non-skipped) statistical test — confirming at least one comparison
actually runs with a real `test_name` and a Holm-adjusted p-value.

## 27. Smoke commands

```bash
python3 scripts/merge_weighted_experiment_results.py \
    --input-root results/weighted_phase8b_smoke --output-dir <tmp>/merged --strict
python3 scripts/analyze_weighted_experiment_results.py \
    --merged-current-valid <tmp>/merged/merged_current_valid.csv \
    --merged-all-attempts <tmp>/merged/merged_all_attempts.csv \
    --output-dir <tmp>/analysis
```

Verified (also asserted in `test_weighted_analysis_end_to_end.py`): 12 input
rows, 3 comparison groups (one per weight profile), all 3 certified
optimal, best-known values defined only by `FPP-SAA`/
`FPP-Branch-Benders-Combinatorial`, DPV-SAA and Static-DPV both receive
evaluator-based gaps with the correct `objective_space` label, OOS and
paired summaries differ, runtime profiles stay separate by weight profile,
all six publication tables produced in both CSV and LaTeX form.

## 28. Limitations deferred to Phase 10

- Replicate aggregation (`replicate_aggregated` artifact) is designed for
  (comparison-group key retains `weight_replicate` un-collapsed) but not
  implemented as a distinct output, since no multi-replicate fixture exists
  yet to validate it against (section 22 above).
- No manuscript prose, narrative conclusions, or publication-ready written
  discussion — only data tables.
- No large production experiment matrix was run; all validation used the
  existing Phase 8B smoke fixture plus in-memory synthetic fixtures.
- Final release validation, changelog consolidation, and any remaining
  Phase 10 cleanup are explicitly out of scope for this phase.
