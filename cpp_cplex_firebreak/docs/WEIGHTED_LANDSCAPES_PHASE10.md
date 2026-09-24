# Phase 10: Final Regression, Documentation Consolidation, Release Readiness

Phase 10 verifies that Phases 0-9B work together as one coherent system,
consolidates documentation into one authoritative guide, closes a small
number of safely-fixable gaps identified during Phase 9A/9B, and produces a
release-validation script and report. It introduces no new solver
formulations and launches no uncontrolled large experiment.

## 1. Initial repository state (recorded before any Phase 10 change)

```
branch: feature/instance-generation-updates
HEAD:   b59cc2ca "Add weighted experiment analysis pipeline"
git status: clean (before Phase 10 edits began)
```

Last 5 commits at Phase 10 start:
```
b59cc2ca Add weighted experiment analysis pipeline        (Phase 9B)
2d5f4902 Add canonical weighted result merge pipeline      (Phase 9A)
dab5c450 Complete weighted batch and paired evaluation integration (Phase 8B)
422cc48f Add weighted DPV optimization models               (Phase 7B2)
fd5a04d8 Add canonical weighted experiment registry          (Phase 8A)
```

`git diff --check` / `git diff --cached --check`: both clean.

**Untracked/ignored inventory** (`git status --porcelain --ignored`):
- `docs/` is entirely gitignored (confirmed via `.gitignore:29`) and contains
  ~90 markdown files. Only 27 of them (`WEIGHTED_LANDSCAPES_AUDIT.md`,
  `WEIGHTED_LANDSCAPES_PHASE{1,2,3,4,5A,5B,5C1,5C2A,5C2B1,5C2B2,6A,6B1,
  6B2A,6B2B,6B3A,6B3B,6C1,6C2A,6C2B,6C2C,7A,7B1,7B2,8A,8B,9A,9B}.md`) belong
  to the weighted-landscapes roadmap this project tracks; the rest
  (`PHASE1_SUMMARY.md` ... `PHASE35_...md`, `SUB20_...md`, etc.) belong to an
  earlier, unrelated pre-weighted roadmap and are out of scope for this
  consolidation.
- `results/` (gitignored except `.gitkeep`/`splits/`) contains ~1.1 GB of
  accumulated historical smoke/experiment output spanning the entire
  project history (pre-weighted phase smoke dirs, `weighted_phaseNsmoke/`
  fixtures for every weighted phase, the Phase 9A/9B `weighted_phase8b_smoke/`
  fixture, and unrelated computational-study directories). This is treated
  as the user's accumulated work and is **not deleted or modified** by
  Phase 10.
- `build_gpp/` (gitignored, ~1.5 GB): compiled object/binary output for both
  the CPLEX and non-CPLEX build variants. Regenerated freely by `make
  build`/`make cplex`; not a concern for cleanliness since it's fully
  ignored and reproducible.
- No stray untracked *non-ignored* files were found (`git status
  --porcelain=v1 --untracked-files=all` was empty at the start of Phase 10).

No repository state was reset, discarded, or overwritten in the course of
this audit.

## 2. Phase documentation review and reconciliation

A dedicated research pass read `WEIGHTED_LANDSCAPES_AUDIT.md` through
`WEIGHTED_LANDSCAPES_PHASE8B.md` in full (Phase 9A/9B excluded — already
authored this session with full first-hand knowledge). Full synthesis
retained in the session transcript; key findings folded into
`docs/WEIGHTED_LANDSCAPES.md` (section "Known unsupported combinations")
and summarized here:

### 2.1 Confirmed still-standing limitations (never resolved through 8B, and not addressed in 9A/9B either since those phases are result-schema/analysis work, not solver work)

- Restricted-candidate LLBI (standard/Coverage/Path/projected), combinatorial
  Benders, global dominance, and conditional zero-benefit fixing remain
  **rejected** for the restricted-candidate solver family for non-homogeneous
  weight maps. Only candidate scoring/activation/maintenance was ever
  converted for the restricted path (Phase 5C2A/5C2B1).
- Weighted permanent candidate-pruning/bounds for restricted-candidate
  remain **disabled** (Phase 5C2B2 found counterexamples showing raw cell
  weight is not a safe benefit bound; no later phase supplies a valid one).
- Conditional zero-benefit fixing remains **detector-only**: it reports
  "structurally weight-safe" but applies zero actual local variable
  fixings, because the CPLEX generic callback path does not safely expose
  node-local bound tightening (Phase 6A).
- Combinatorial Benders combined with LLBI, global dominance, conditional
  zero-benefit fixing, or `--use-root-user-cuts` remains **rejected**; no
  dedicated combinatorial root-only cut mechanism was ever built (Phase
  6C1 through 6C2C, reaffirmed at each step, never revisited later).
- Combinatorial lifting modes `posterior`/`heuristic` are conservative
  path-level deduplication only — **not** true sequential/exact lifting or a
  heuristic coefficient estimator, despite the option names (Phase 6C2A's
  own explicit correction). `--combinatorial-benders-lift` accepts exactly
  `none|posterior|heuristic` in the current parser
  (`parse_fpp_combinatorial_benders_lift_mode` in
  `include/benders/FppCombinatorialBenders.hpp`) — no `exact`/`sequential`
  value exists.

### 2.2 Confirmed resolved between phases (verified, not just claimed)

- Phase 8A's registry/manifest infrastructure gaps (no C++
  `BatchExperimentConfig` wiring, no subset-tolerant `attach_weight_map`, no
  end-to-end worker integration) were all resolved in Phase 8B, verified by
  reading both docs and by the fact that `results/weighted_phase8b_smoke/`
  exists and passes `scripts/weighted_phase8b_batch_smoke.sh` today (rerun
  in this phase, section 20 below).
- The Phase 9A `paired_evaluation_enabled` gating defect (documented in
  `WEIGHTED_LANDSCAPES_PHASE9B.md` section 1.2) is fixed and regression-
  tested further in this phase (section 11).

### 2.3 Documentation errors found (historical docs left as-is; noted here and corrected in the consolidated guide)

- `WEIGHTED_LANDSCAPES_PHASE5C2B1.md`'s example commands use `--weight-map`
  and `--initial-candidates`. **Verified against `src/main.cpp`: the actual,
  and only ever, flag names are `--weight-map-file` and
  `--initial-candidate-list`** (confirmed by every other phase doc and by
  grepping the live argument parser). This is a documentation typo in that
  one phase doc, not a real historical rename. Per Phase 10 instructions,
  historical phase docs are not rewritten; the consolidated guide
  (`docs/WEIGHTED_LANDSCAPES.md`) uses the verified, correct flag names
  throughout.
- The diagnostic field names `combinatorial_root_cuts_enabled` /
  `combinatorial_root_rounds` / etc. (added in Phase 6C2B) could be misread
  as evidence of a combinatorial-specific root-cut mechanism; Phase 6C2B's
  own text clarifies these fields exist specifically to document its
  **absence** (`combinatorial_root_skipped_reason`). Called out explicitly
  in the consolidated guide to prevent this misreading.
- Weight-map hashes for the "Sub20 heterogeneous/clustered" smoke fixture
  differ between Phase 5A and Phase 6C1/6C2A docs (different generation
  runs, not a byte-identical fixture reused across phases) — this is
  expected (map generation is deterministic *given* a seed/config, but nothing
  guarantees the exact same seed was used verbatim across independently
  invoked smoke scripts months apart); not a defect, but worth noting for
  anyone trying to bit-for-bit reproduce an old phase's exact numbers from
  the doc alone rather than regenerating.

## 4. CLI inventory (excerpt) and capability-matrix validation

Full command list verified directly against `src/main.cpp`'s `if (command
== ...)` dispatch chain: `smoke`, `smoke-new-instances`,
`generate-weight-map`, `ensure-weight-map`, `evaluate`, `analyze-graphs`,
`run-fpp-saa-oos`, `run-fpp-benders-oos`, `run-fpp-branch-benders-oos`,
`run-fpp-restricted-branch-benders-oos`, `run-static-dpv-oos`/
`run-static-dpv-mip-oos`, `run-greedy-oos`, `run-dpv-saa-oos`,
`run-dpv-benders-oos`, `run-dpv-branch-benders-oos`, `run-batch-oos`,
`run-fpp-lp-diagnostic`, `aggregate-batch`, `run-manifest`, `solve-fpp-saa`,
`diagnose-fpp-master-lp`, `diagnose-fpp-dominance-preprocessing`.

Spot-checked options against the live parser (all confirmed present, exact
spelling, no aliases beyond what's documented):

| Option | Type | Notes |
|---|---|---|
| `--weight-map-file` | path | present on `evaluate` and every `run-*-oos`/`solve-fpp-saa` command; no value-format validation beyond file-readability at load time |
| `--dpv-ignition-policy` | string | accepts `fpp-safe`/`legacy`; **the CLI parser itself does not validate against this enum** — an unrecognized value is stored as-is and only rejected (or silently defaulted) deeper in DPV dispatch. Not a bug found active in practice (only these two values are ever passed by any script), but worth tightening in a future phase for a clearer early error message. |
| `--combinatorial-benders-scenario-order` | string | accepts exactly `eta-asc`/`eta-desc` via `parse_fpp_combinatorial_benders_scenario_order`; any other value throws immediately, clear error |
| `--combinatorial-benders-cut-sampling-ratio` | float | accepts `(0,1]`; no explicit CLI-level range check found — an out-of-range value is not rejected at parse time, only at first use inside the sampling logic (not independently verified beyond a source read) |
| `--paired-reburn-evaluation` | flag (Python generator only, not a C++ flag) | controls manifest-level `paired_evaluation_enabled`, which (per the Phase 9B finding) is **not** the actual signal the worker uses to decide whether to run paired evaluation — see `docs/WEIGHTED_LANDSCAPES.md` section 20 |
| `--combinatorial-benders-lift` | string | accepts exactly `none`/`posterior`/`heuristic`; **no `exact`/`sequential` value exists**, confirmed via `parse_fpp_combinatorial_benders_lift_mode` |

No misleading help text was found that needed correction for backward
compatibility reasons; no stale/undocumented options were found beyond the
`--dpv-ignition-policy` non-validation noted above.

**Capability-matrix cross-check (section 8):** `experiments::WeightedMethodCapability.cpp`'s
`weighted_method_capability()` and its Python mirror
(`generate_fpp_new_instances_scaling_manifests.py:weighted_method_supported()`)
are byte-for-byte equivalent in their guard logic (both reject: restricted +
combinatorial; combinatorial + LLBI under non-homogeneous; combinatorial +
dominance under non-homogeneous). **Verified gap**: neither the C++
capability matrix nor its Python mirror rejects restricted-candidate +
LLBI/dominance/conditional-zero-benefit combinations — that rejection only
happens as a runtime `throw std::runtime_error(...)` deep inside
`FppRestrictedCandidateBranchBendersSolver.cpp` (confirmed by direct source
read, message: *"restricted standard LLBI, restricted CoverageLLBI, and
restricted PathLLBI remain disabled..."*). This means, in principle, a
capability query could report a restricted-candidate+LLBI combination as
"supported" when execution would actually reject it — a real, verified
capability-matrix/dispatcher-guard mismatch. **Currently dormant in
practice**: `config/fpp_new_instances_scaling_methods.txt` contains no
restricted-candidate method labels, so this gap is never triggered by the
configured method panel today. Documented here rather than patched, given
the modification would touch compiled, previously-validated C++ capability
logic for a combination that isn't currently reachable — recommended as a
low-risk, well-scoped fix for a future phase (add the same guard already
proven correct in the solver's own runtime check to both capability-matrix
implementations).

## 5. Schema consistency audit

Cross-checked C++ struct fields (`ExperimentResultWriter.hpp`), JSON/CSV
writers (`ExperimentResultWriter.cpp`), the worker enrichment layer, the
Phase 9A canonical schema (`weighted_result_schema.py`), and the Phase 9B
analysis layer. Findings already documented in `docs/WEIGHTED_LANDSCAPES.md`
section 20 and `docs/WEIGHTED_LANDSCAPES_PHASE9A.md`/`PHASE9B.md`; the two
new findings from this phase:

- **`solver_weighted_objective` conflation (fixed at source this phase)** —
  see section 10 of the completion report.
- **`paired_evaluation_enabled` unreliability (fixed in Phase 9B, final
  regression added this phase)** — see section 11 of the completion report.

No new stale/duplicate/type-mismatched fields were found beyond what Phase
9A/9B already documented (`weighted_fpp_expected_paired_reburn`/`_cvar_paired_reburn`
remain JSON-only with no CSV column; this is unchanged and still the
correct, documented behavior — adding a CSV column for these would be a
schema change to the C++ writer, out of scope for a "safe, non-disruptive"
Phase 10 fix per the instructions' own risk framing).

## 6. Mean-CVaR coverage audit

No `mean_cvar`-specific field exists anywhere in the C++ result struct, and
no per-scenario loss vector is persisted in JSON/CSV output that would let
a downstream consumer independently recompute a mean-CVaR aggregate. The
only safe computation is the closed form `lambda * expected + (1-lambda) *
cvar`, applied per-namespace using that namespace's own expected/CVaR
values and the row's own `cvar_lambda` — exactly what
`weighted_result_normalize._mean_cvar()` already implements (Phase 9A).
This phase adds `scripts/test_weighted_mean_cvar_coverage.py` (4 focused
assertions) locking down: the value is computed only when `risk_measure`
actually declares mean-CVaR (never inferred from component presence alone
under a different risk measure), is left `None` (never fabricated) when a
required component is missing, and is computed independently per namespace
(in-sample/OOS never conflated).

## 7. Original-ID mapping regression — a real anomaly found, not fixed

Cross-method comparison of selected-firebreak CSVs in
`results/weighted_phase8b_smoke/` across all three weight profiles found:
`FPP-SAA`/`FPP-Branch-Benders-Combinatorial`/`Static-DPV` selections
correctly **differ** across homogeneous/heterogeneous/clustered profiles
(as expected — different weights should generally select different
firebreaks). **`DPV-SAA`'s selected firebreak set is bit-for-bit identical
across all three profiles** (`[1, 2, 3, 4, 5, 6, 7, 8]`, exactly sequential
original Cell2Fire IDs, budget=8) in both the pre-existing committed
fixture and a freshly regenerated smoke run performed in this phase (ruling
out the Phase 10 source edits as the cause — the pattern predates every
Phase 10 change). All IDs are valid, in-range original Cell2Fire IDs
(1-400 for this 20x20 instance), and JSON/CSV agree exactly with each other
for every row checked — so this is **not** a compact-index leak or a
JSON/CSV inconsistency. It is either (a) a genuine weight-insensitivity
defect specific to `DPV-SAA`'s selection logic, or (b) a coincidental
property of this specific tiny toy instance/budget combination (many tied
near-optimal DPV scores at this scale, with CPLEX's deterministic
tie-breaking always landing on the same solution regardless of the small
weight perturbations tested). Distinguishing (a) from (b) requires a
solver-level investigation (larger/varied instances, inspection of
`DpvSaaCplexModel.cpp`'s scoring coefficients under each profile) that is
out of scope for Phase 10 (no new solver formulations or deep solver
debugging authorized this phase). **Documented here as a verified,
reproducible anomaly and carried into the known-limitations list
(completion report section 38) for follow-up in a future phase** — not
silently ignored, not fixed without a confirmed diagnosis.

No other cross-method original-ID inconsistency was found:
`core::FirebreakSolution::from_csv` rejects empty tokens, non-numeric
tokens, and non-positive IDs at parse time (verified by source read); no
explicit duplicate-ID-rejection test was found in the existing C++ suite
(a gap worth closing in a future phase, not introduced here to avoid
touching solver-adjacent code without a full regression pass).

## 8. Production-scale dry run (no solves launched)

Ran `generate_fpp_new_instances_scaling_manifests.py` (manifest generation
+ `--generate-missing-weight-maps`, both cheap operations that never invoke
a solver) with a production-shaped configuration: 2 instance families
(`new20x20`, `new40x40`), train counts `{100, 200, 400, 800}`, 3 alphas
(`0.01, 0.02, 0.03`), 2 cases, 3 weight profiles, 1 replicate each, the full
21-method production panel, paired-reburn evaluation enabled:

```
Wrote 3024 manifest rows across 144 workers.
Total wall time: 0.39s (manifest generation + 6 weight-map generations, zero solves).
```

Verified:
- **Maps required**: 6 (`2 instances x 3 profiles x 1 replicate`), all
  successfully pre-generated; the *first* attempt without
  `--generate-missing-weight-maps` correctly and clearly failed with
  `"Missing weight-map registry entry for family new20x20 profile
  homogeneous replicate 0; pre-generate it or pass
  --generate-missing-weight-maps."` — demonstrating the "never regenerate
  silently" guarantee under a realistic multi-instance configuration, not
  just the single-instance smoke fixture.
- **Manifest row count**: 3024 = `2 instances x 4 train_counts x 3 alphas x
  2 cases x 21 methods x 3 profiles`, exactly as expected from the input
  grid.
- **Filtered unsupported count**: 0 (the generator only prints a "Filtered
  N unsupported combinations" line when `N > 0`; none of the 21 production
  methods combined with these 3 profiles hit a capability rejection,
  consistent with the section 4 finding that the default method panel
  contains no restricted-candidate or LLBI+combinatorial combinations that
  would trigger one).
- **Unique run IDs**: 3024 / 3024 rows have a distinct `run_id` (zero
  collisions).
- **Output-path collisions**: 3024 / 3024 rows have a distinct
  `output_json` path (zero collisions).
- **Expected worker commands**: 144 per-worker manifest CSVs were written
  under `manifests/`; no worker process was ever launched, confirmed by the
  0.39s total wall time (an actual FPP/DPV solve on this instance family
  alone takes ~0.2-0.8s per task per the smoke-run timings in section 22
  below, so 3024 real solves would take on the order of tens of minutes to
  hours depending on parallelism — this dry run cost none of that).
- **No worker map generation**: confirmed by design (the worker never
  calls `ensure-weight-map`; all 6 maps were generated exclusively by the
  generator's own pre-launch step).
- **Expected disk estimate**: extrapolating from the real smoke run's
  per-row footprint (~140 KB/row across JSON + logs + solution CSV,
  measured in section 22), 3024 rows would need on the order of 400-450 MB
  — a useful sizing number before launching a real batch, not itself a
  launch.

## 9. Resource smoke (operational safeguards only, not a benchmark)

From the `--full --with-cplex` validation run (section "Final full
validation" in the completion report): total wall time for `make cplex`
rebuild + the complete 3-profile x 4-method paired smoke workflow
(including a corrupt-and-retry cycle) + merge + analysis + a full
second merge/analysis pass for the reproducibility check was **72
seconds**. Output sizes: smoke logs/JSON/solutions ~1.6 MB, merged CSVs
~60 KB, analysis outputs (summaries/tables/profiles/diagnostics) ~192 KB
for 12 rows. No sign of quadratic blowup, no repeated map re-parsing
(registry `load()` is read-only per worker invocation), no unexpectedly
large CSV rows, no uncontrolled logging (stage log for the full run is a
few hundred lines). These are operational sanity numbers only — no
performance conclusions are drawn from a 12-row, single-replicate smoke
fixture.

## 10. Scope boundary for this phase

Consistent with the instructions, Phase 10 does not: introduce new solver
formulations, launch an uncontrolled large experiment matrix, rewrite
historical phase docs beyond noting factual errors, or delete accumulated
`results/` fixtures. All source-level changes in this phase are documented
individually in the completion report with their exact rationale and risk
assessment.
