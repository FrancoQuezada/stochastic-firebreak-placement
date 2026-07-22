# Weighted Landscapes: Known Limitations (Authoritative, Phase 10)

Each item classified as **Unsupported** (rejected explicitly, no path to
enable it today), **Supported with restrictions** (works, but not in every
combination), **Experimental** (works but not independently validated at
scale), or **Deferred future work** (a real gap, not yet scheduled).

| Limitation | Classification | Notes |
|---|---|---|
| Conditional zero-benefit fixing applies no actual variable fixings | Supported with restrictions | Detector-only: correctly identifies weight-safe zero-benefit candidates but the CPLEX generic callback path doesn't safely expose the required node-local bound tightening (Phase 6A). |
| Restricted-candidate combinatorial Benders | Unsupported | Rejected by both the C++ and Python capability matrices. |
| Restricted-candidate + any LLBI family (standard/Coverage/Path/projected) | Unsupported | Rejected at runtime inside `FppRestrictedCandidateBranchBendersSolver.cpp`; **not currently mirrored in either capability-matrix implementation** (a verified, currently-dormant gap — see `docs/WEIGHTED_LANDSCAPES_PHASE10.md` section 4; the default method panel never triggers it). |
| Restricted-candidate + global dominance / conditional zero-benefit | Unsupported | Same runtime guard as above. |
| Restricted-candidate weighted permanent candidate pruning/bounds | Unsupported by design | Phase 5C2B2 found counterexamples proving raw cell weight is not a safe benefit bound; no valid bound has been supplied since. |
| Combinatorial Benders + LLBI (any family) | Unsupported | Non-homogeneous weighted combinatorial Benders does not combine with LLBI. |
| Combinatorial Benders + global dominance | Unsupported | Kept disabled pending a combinatorial-separator-remapping validation that was never performed in any subsequent phase. |
| Combinatorial Benders + conditional zero-benefit fixing | Unsupported | Detector-only status makes this combination meaningless as well as unimplemented. |
| Combinatorial Benders + `--use-root-user-cuts` | Unsupported | No dedicated combinatorial root-only cut mechanism exists in the repository at all. |
| Combinatorial lifting modes `posterior`/`heuristic` | Supported with restrictions | Both are conservative path-level cut deduplication, **not** true sequential/exact lifting or a heuristic coefficient estimator, despite the option names. No `exact`/`sequential` mode exists. |
| DPV-CVaR optimization (CVaR risk measure + any DPV surrogate method) | Unsupported | Explicitly rejected ("DPV-CVaR optimization is out of scope and not implemented"). |
| Mean-CVaR fields (`weighted_fpp_mean_cvar_*`) | Supported with restrictions | Computed via the documented closed form only when `risk_measure` declares mean-CVaR and both the expected and CVaR components are available; left missing otherwise, never inferred or fabricated (Phase 10 section 14 audit). |
| Coordinate-based fallback weight-to-instance mapping | Experimental | Used only when stable original-cell IDs are unavailable; always explicit, never a silent best-effort guess, but not independently stress-tested beyond the existing unit tests. |
| Weight-map registry pre-generation requirement | Supported with restrictions | Concurrent generation of the *same* logical registry entry from two processes is not supported; maps must be pre-generated in a single process before parallel workers launch. |
| Legacy schema migration | Supported with restrictions | Two legacy shapes are migrated (`legacy-weighted-pre8a`, `legacy-unweighted-pre8a`); anything older or structurally different is rejected as `invalid_schema`, not guessed at. |
| Statistical power for small experiments | Supported with restrictions | Paired statistical comparisons require >= 6 paired observations (configurable) and are explicitly skipped (not run with an unreliable n) below that; a single-instance-per-profile smoke fixture will skip essentially all comparisons — this is correct, not a bug. |
| Replicate-aggregated tables | Experimental | Implemented and unit-tested against a synthetic multi-replicate fixture (Phase 10); not yet exercised against a real multi-replicate production dataset (none exists in the current fixture set). |
| `DPV-SAA` weight-sensitivity on the reference 20x20 toy instance | **Deferred future work — open anomaly, not yet diagnosed** | `DPV-SAA`'s selected firebreak set is bit-for-bit identical (`[1..8]`, exactly sequential original IDs) across all three weight profiles in `results/weighted_phase8b_smoke/`, despite the reported surrogate objective differing by profile. Confirmed pre-existing (not introduced by Phase 10), confirmed not a compact-index/JSON-CSV bug. Root cause (genuine weight-insensitivity defect vs. a toy-instance/tie-breaking coincidence) requires solver-level investigation out of scope for this phase. See `docs/WEIGHTED_LANDSCAPES_PHASE10.md` section 7. |
| Duplicate original-firebreak-ID rejection | Deferred future work | No explicit test found for `core::FirebreakSolution::from_csv` rejecting a duplicate ID within one selected set; existing validation covers empty/non-numeric/non-positive tokens. Small, well-scoped gap for a future phase. |
| `--dpv-ignition-policy` / `--combinatorial-benders-cut-sampling-ratio` CLI validation | Deferred future work | Neither is validated against its expected value/range at parse time; both are only exercised via the two/one values every current script ever passes. Not a currently-triggered defect. |
