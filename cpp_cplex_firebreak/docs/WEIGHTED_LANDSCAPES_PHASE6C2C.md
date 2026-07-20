# Weighted Landscapes Phase 6C2C: Scenario Ordering and Exact Sampling-First Cuts

Phase 6C2C enables weighted scenario ordering and cut sampling for FPP
combinatorial Branch-and-Benders without changing the weighted cut formulas
validated in Phases 6C1, 6C2A, and 6C2B.

Scope included:

- `eta-asc` and `eta-desc` ordering;
- `cut_sampling_ratio in (0,1]`;
- exact sampling-first integer incumbent separation with full fallback;
- interactions with lifting `none|heuristic|posterior`;
- interactions with initial cuts, fractional user cuts, expected, CVaR, and
  mean-CVaR.

Scope excluded:

- restricted-candidate combinatorial Benders;
- new LLBI combinations;
- dominance or conditional-fixing combinations;
- DPV and Static-DPV;
- experiment manifests and batch launchers.

## Pre-Change Audit

Existing options:

```text
--combinatorial-benders-scenario-order eta-asc|eta-desc
--combinatorial-benders-cut-sampling-ratio r
```

Audit results before Phase 6C2C:

```text
scenario-order options: eta-asc and eta-desc were parsed
weighted support: eta-desc was rejected for non-homogeneous combinatorial mode
sampling-ratio validation: 0 < r <= 1
sample-size rounding: ceil(r * scenario_count), minimum one
scenario-list construction: all scenarios sorted by current eta
determinism: deterministic, no random seed, no random_device
same subset every callback: subset depended on current eta ordering
scenario probabilities: not used for ordering or sampling
omitted scenarios before acceptance: if no violations were found, all were checked
stop rule: stopped after finding the configured number of violated cuts
multi-cut policy: added up to the sampled violated-cut limit
initial cuts: generated for every scenario, no sampling
fractional cuts: used same separator and ratio
lifting: applied after violation identification
CVaR: recourse constraints still scenario-wise; no tail-only skipping
exactness claim: acceptance was exact, but ratio semantics were violated-cut-limit,
not sampling-first
```

Classification before Phase 6C2C:

```text
eta-asc, r=1: exact ordering only
eta-desc, r=1: unsupported for non-homogeneous weighted combinatorial mode
r<1: exact acceptance with violated-cut-limit semantics, not sampling-first
random sampling: unsupported
heuristic incomplete sampling: unsupported and not exposed
```

## Scenario Ordering

Phase 6C2C treats ordering as a complete permutation of the training scenario
set. It does not change the objective, scenario probabilities, cut formula, or
candidate acceptance rule.

`eta_s` is the current master recourse value and is already in weighted-loss
units. Ordering does not multiply it by cell weights or scenario probabilities.

Tie-breaking is deterministic by scenario ID, then by position only if IDs are
identical:

```text
eta-asc:  smaller eta_s first
eta-desc: larger eta_s first
```

Rationale only:

```text
eta-asc:  checks smaller current recourse estimates first, which may expose
          underestimated scenarios
eta-desc: checks larger current recourse estimates first
```

No ordering is claimed to be theoretically superior.

## Exact Sampling-First Algorithm

For `S` training scenarios and `r in (0,1]`:

```text
m = max(1, ceil(r * S))
```

At every integer candidate:

1. Build the complete deterministic ordered scenario list.
2. Evaluate the first `m` scenarios.
3. If the initial sample contains violated scenarios, add cuts for those
   violated sampled scenarios and reject the candidate. Remaining scenarios are
   counted as skipped after candidate rejection, not as verified.
4. If the initial sample contains no violation, evaluate every remaining
   scenario in deterministic fallback order.
5. Accept the candidate only if the fallback sweep also finds no violation.

This preserves exactness because every accepted integer candidate has verified:

```text
eta_s >= Q_s(y) - tolerance
```

for every training scenario `s`.

There is no heuristic incomplete sampling mode in Phase 6C2C. The reported
policy fields are:

```text
combinatorial_scenario_policy_exact = true
combinatorial_scenario_policy_heuristic = false
combinatorial_full_verification_before_acceptance = true
```

## CVaR and Mean-CVaR

CVaR and mean-CVaR retain all scenario recourse constraints. Apparent current
tail membership is not used to permanently skip scenarios, because an omitted
scenario may become tail-relevant after exact weighted loss evaluation.

Ordering uses weighted master `eta_s` values only. Sampling-first fallback still
verifies all scenarios before candidate acceptance.

## Initial and Fractional Cuts

Initial cuts remain optional strengthening and are generated for every scenario
from the deterministic greedy binary solution. They do not replace integer
candidate verification.

Fractional user cuts remain optional strengthening. They use globally valid
non-lifted fractional path cuts when lifting is requested, as in Phase 6C2B.
Fractional separation may use ordering and sampling, but it does not certify an
integer incumbent and never replaces full integer candidate verification.

## Lifting

For integer candidate cuts, `none`, `heuristic`, and `posterior` lifting remain
supported. Lifting is applied after a violated scenario is identified. Sampling
does not scale coefficients, alter destination weights, or change propagation.

## Cache and Hash Safeguards

No cross-candidate exact-loss cache was added. Each callback recomputes
scenario losses/cuts for the current candidate vector and the separator carries
the same compact weight vector and deterministic weight-map hash used by the
solver result and final evaluator. Homogeneous implicit and explicit unit maps
remain equivalent.

## Supported Combinations

Weighted combinatorial Branch-and-Benders supports:

```text
lift_mode = none | heuristic | posterior
scenario_order = eta-asc | eta-desc
cut_sampling_ratio in (0,1]
initial_cuts = true | false
separate_fractional = true | false
risk_measure = expected | cvar | mean-cvar
```

Still rejected:

```text
LP-dual --use-root-user-cuts with combinatorial Benders
LLBI/projected LLBI combined with combinatorial Benders
global dominance combined with combinatorial Benders
conditional zero-benefit fixing combined with combinatorial Benders
restricted-candidate combinatorial Benders
DPV and Static-DPV combinatorial variants
```

## Diagnostics

Phase 6C2C adds:

```text
combinatorial_scenario_order
combinatorial_cut_sampling_ratio
combinatorial_realized_sample_size
combinatorial_sampling_exact_fallback
combinatorial_scenario_policy_exact
combinatorial_scenario_policy_heuristic
combinatorial_full_verification_before_acceptance
combinatorial_candidate_initial_sample_scenarios_evaluated
combinatorial_candidate_fallback_scenarios_evaluated
combinatorial_candidate_full_sweeps
combinatorial_candidates_rejected_in_initial_sample
combinatorial_candidates_rejected_in_fallback
combinatorial_candidates_fully_verified
combinatorial_sampled_violations
combinatorial_fallback_violations
combinatorial_scenarios_skipped_after_candidate_rejection
combinatorial_sampling_time_sec
combinatorial_ordering_time_sec
```

These are propagated through `ModelResult`, `StandardExperimentResult`, JSON,
and CSV.

## Tests

Added:

```text
tests/test_weighted_fpp_combinatorial_scenario_ordering.cpp
tests/test_weighted_fpp_combinatorial_sampling.cpp
tests/test_weighted_fpp_combinatorial_sampling_exactness.cpp
tests/test_weighted_fpp_combinatorial_sampling_cplex.cpp
```

Updated:

```text
tests/test_weighted_fpp_combinatorial_callback.cpp
tests/test_weighted_fpp_combinatorial_cplex.cpp
tests/test_weighted_fpp_combinatorial_fractional_validity.cpp
tests/test_solution_io.cpp
```

Coverage includes eta ascending, eta descending, complete permutation checks,
scenario-ID tie-breaking, invalid ratios, sample-size rounding, violation in
the initial sample, violation only outside the sample, no-violation full
verification, a CVaR omitted-tail counterexample, deterministic randomized
exactness search, lifting combinations, initial/fractional interaction, CPLEX
objective equivalence, homogeneous regression, and CSV/JSON structural
regression.

## Validation Results

Validated on 2026-07-20:

```text
make cplex                                             passed
make test                                              passed
test_weighted_fpp_combinatorial_scenario_ordering      passed
test_weighted_fpp_combinatorial_sampling               passed
test_weighted_fpp_combinatorial_sampling_exactness     passed
test_weighted_fpp_combinatorial_sampling_cplex         passed
test_weighted_fpp_combinatorial_cplex                  passed
test_weighted_fpp_combinatorial_lifting_cplex          passed
test_weighted_fpp_saa_cplex                            passed
test_weighted_fpp_branch_benders_cplex                 passed
test_solution_io                                       passed
```

The randomized exactness search used fixed seed `62023` over tiny directed
instances, ratios in `(0,1]`, both orderings, and random eta vectors. It found
no omitted accepted violations; sampled candidates were accepted if and only if
the full-evaluation reference had no violated scenario.

## Smoke Results

Tiny OOS smoke used `Sub20`, train scenarios `1-2`, test scenarios `3-4`,
`alpha=0.01`, 30 second limit, one CPLEX thread. Each smoke matched direct
FPP-SAA, LP callback Branch-and-Benders, and full combinatorial reference with
maximum objective difference `0`.

```text
run                                                   risk       weights        lift       order     ratio sample fallback objective
smoke01_heterogeneous_expected_eta_asc_ratio1         expected   heterogeneous  none       eta-asc   1.0   2      false    40.25326732
smoke02_heterogeneous_expected_eta_asc_ratio01        expected   heterogeneous  none       eta-asc   0.1   1      true     40.25326732
smoke03_heterogeneous_expected_eta_desc_ratio01       expected   heterogeneous  none       eta-desc  0.1   1      true     40.25326732
smoke04_clustered_expected_eta_desc_ratio01           expected   clustered      none       eta-desc  0.1   1      true     55.04142464
smoke05_heterogeneous_cvar_eta_desc_ratio01           cvar       heterogeneous  none       eta-desc  0.1   1      true     47.34752030
smoke06_clustered_mean_cvar_eta_desc_ratio01          mean-cvar  clustered      none       eta-desc  0.1   1      true     69.31254101
smoke07_heterogeneous_expected_heuristic_ratio01      expected   heterogeneous  heuristic  eta-desc  0.1   1      true     40.25326732
smoke08_heterogeneous_expected_posterior_ratio01      expected   heterogeneous  posterior  eta-desc  0.1   1      true     40.25326732
smoke09_heterogeneous_expected_initial_ratio01        expected   heterogeneous  heuristic  eta-desc  0.1   1      true     40.25326732
smoke10_heterogeneous_expected_fractional_ratio01     expected   heterogeneous  heuristic  eta-desc  0.1   1      true     40.25326732
smoke11_homogeneous_expected_eta_desc_ratio01         expected   homogeneous    none       eta-desc  0.1   1      true     41.50000000
```

No performance conclusion is drawn from these smokes.

## Remaining Phase 6 Limitations

The weighted combinatorial roadmap is complete for full-candidate
Branch-and-Benders with ordering, exact sampling-first fallback, integer cuts,
lifting, initial cuts, fractional cuts, and expected/CVaR/mean-CVaR objectives.
Remaining deferred work is outside Phase 6C2C: restricted-candidate
combinatorial Benders, root-only combinatorial cuts, LLBI/dominance combinations
with combinatorial mode, and DPV/Static-DPV combinatorial variants.
