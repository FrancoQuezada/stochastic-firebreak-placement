# Weighted Landscapes: Changelog

User-facing summary of the weighted-landscape capability added to this
project. Not a commit-by-commit log — see `git log` for that.

## Canonical cell weights

Every firebreak-placement solve, DPV heuristic, and evaluation now supports
a per-cell wildfire-loss weight `w_k`, so results reflect loss-weighted
burned area (`sum_k w_k x_k^s(y)`) rather than raw burned-cell count.
Legacy unweighted (homogeneous) experiments require no changes.

## Supported weight profiles

Three deterministic, hashed, and registry-backed profiles: `homogeneous`,
`heterogeneous` (i.i.d. per-cell draw), `clustered` (background + high-value
clusters). Every result row records the exact `weight_map_hash` that
produced it.

## Exact weighted evaluation

`FppRecourseEvaluator` independently evaluates any selected firebreak set's
true weighted loss, VaR, CVaR, and high-value-cell coverage — used to
cross-validate every solver's own objective within a documented tolerance.

## Method coverage

Weighted support was added across FPP-SAA, FPP-Benders (explicit-loop and
callback Branch-and-Benders), restricted-candidate Branch-and-Benders
(scoring/maintenance, not strengthening cuts), all four LLBI families,
combinatorial Benders (lifting, initial/fractional cuts, scenario
ordering/sampling), global candidate dominance, and every DPV method
(Static-DPV, Static-DPV-MIP, Greedy-DPV2/3, DPV-SAA, DPV-Benders, DPV
Branch-and-Benders, DPV LLBI). See `docs/WEIGHTED_LANDSCAPES.md` for the
full per-method breakdown and known unsupported combinations.

## Canonical experiment registry

Physical landscape identity, weight-map registry (content-addressed,
idempotent, never silently regenerated), and reduced/reburn paired-instance
mapping by original cell ID, so every weighted experiment is fully
traceable and reproducible.

## Paired reburn evaluation

Selected firebreaks transfer from a reduced training instance to its
physically paired reburn instance by original cell ID, with a hard failure
on any unmapped cell (never a silently dropped cell).

## Canonical result schema and merge pipeline

A single versioned result schema (`weighted-result-9a.1`) normalizes every
raw result row, separating solver objective / DPV surrogate objective /
exact weighted evaluation / physical burned-cell count into unambiguous
fields. A merge pipeline validates, deduplicates (exact/retry/conflicting),
and migrates legacy rows, producing deterministic, audited output.

## Scientific analysis pipeline

Best-known feasible values and lower bounds (exact-FPP methods only),
certification gaps, cross-method gaps, out-of-sample and paired-reburn
observed regret (never called "optimal"), method summary tables (profile-
stratified by default), win/tie/loss, paired statistical comparisons with
multiple-comparison correction, Dolan-More performance profiles, quality
profiles, and publication-ready CSV/LaTeX tables.

## Compatibility considerations

- Legacy homogeneous manifests and worker CSVs continue to work unchanged.
- `run_id` gained a deterministic weight-profile/replicate/hash suffix for
  weighted rows; legacy (unweighted) `run_id`s are untouched.
- A pre-existing conflation where the generic `solver_weighted_objective`
  output field could hold a DPV surrogate value instead of being left
  blank for DPV rows was fixed at the source this phase (Phase 10) without
  removing the field or changing any numerical solve.

## Known limitations

See `docs/WEIGHTED_LANDSCAPES.md` section 15 and the completion report's
known-limitations section for the full, current list (restricted-candidate
strengthening-cut support, combinatorial-Benders combination restrictions,
conditional zero-benefit fixing being detector-only, DPV-CVaR not being
implemented, and an open, documented anomaly in `DPV-SAA`'s
weight-sensitivity on one small toy instance that needs further
investigation).
