# S1: existential output covering, measured

Sprint package S1. The question is whether covering an input region with an
existentially quantified output, rather than one fixed total output valuation,
changes what the solver answers.

Answer: **it does not change coverage on this cohort, and it is not free of
effect either.** The mechanism fires where the structure matches and is inert
where it does not. Recommendation: **KEEP DEFAULT-OFF**.

## Run identity

| | |
|---|---|
| Cohort | `benchmarking/coverage-frontier-20260912/diagnostic-cohort.list`, 46 instances, frozen before any candidate was built |
| Arms | E-C `otf_sparse_formula` (control) and E-X `otf_sparse_existential`, both exact successors |
| Binaries | E-C `59967bab9d2fa07c…`, E-X `a2e716b5cb8a14ef…` |
| Build | release, `-O3`, LTO, `b_ndebug`, `acacia_compiler_profile=release`; both arms built from the same revision, differing only in `acacia_spot_guarded_existential_outputs` |
| Regime | 17 s cap, `MemoryMax=8G`, `MemorySwapMax=0`, one invocation at a time, nothing else running |
| Raw data | `_bm-logs.20260912-s1-modes/` (gitignored): per-run TSVs, summaries, and per-worker records under `wrec-s1-ec/`, `wrec-s1-ex/` |

Both arms run the preset's four-arm portfolio, of which one worker is the sparse
guarded engine. The end-to-end numbers are therefore diluted: another arm can
answer first and hide any change in the sparse worker. The per-worker records
are what isolate the mechanism, and both readings are reported below.

## End-to-end coverage: no change

| arm | solved / 46 | REAL | UNREAL | timeouts | unknown |
|---|---:|---:|---:|---:|---:|
| E-C | 11 | 3 | 8 | 34 | 1 |
| E-X | 11 | 3 | 8 | 34 | 1 |

The solved **sets** are identical, not merely equal in size, and no instance
received a different verdict. Per-instance times differ by at most 1.07 s, on
instances taking 11 s or more; every fast solve moves by under 0.1 s. Nothing
here is outside run-to-run variation.

The baseline is consistent with the historical evidence. The ten instances the
2026-09-10 race recorded as answered by the shipped four-arm configuration but
not by the two-arm one are exactly the ten fast solves here, on a binary built
two days later from a different revision. The eleventh, `workstation_resupply_pb_3_pe_`
at 16.02 s against a 17 s cap, was a two-arm-only instance historically; a
near-cap instance moving between configurations is the contention effect the
failure-phase analysis already identified, not a change attributable to S1.

## Mechanism: fires on 1 of 13, inert on 11, unchanged on the busiest node

Thirteen instances reached the search in both arms with counters present.
Eleven are identical: same abstract nodes, same choices to within four, same K,
times within noise. Two moved.

| instance | choices/node | choices | search ms | note |
|---|---|---:|---:|---|
| `taxi-service-real` | 1.752 → 1.378 | 184 → 102 | 27.2 → 14.8 | mechanism fires: 45% fewer choices, 46% less search |
| `robot-to-target-charging10` | 0.999 → 0.999 | 678 → 1389 | 318 → 776 | K reached 14 → 17; both time out, so the larger counters are a snapshot at a later K, i.e. more progress in the same budget |

The eleven inert instances explain themselves. Four sit at exactly 1.000 choices
per node — `g-unreal-113`, `g-unreal-116`, `thermostat-F-real`,
`ordered-visits-choice-real` — where there is nothing to collapse. More
informative is `workstation_resupply_pb_3_pe_`, which carries the highest
choice density in the cohort at 4.769 choices per node and is **completely
unchanged**, 13115 choices in both arms.

That is the boundary of the mechanism, and it is now measured rather than
assumed. Existential covering merges regions that reach the **same** abstract
target under different outputs. Where a node's many choices lead to many
distinct targets, quantifying the output merges nothing. A high choice count is
therefore not a predictor of benefit; sharing a target is.

## Verification cost: no penalty

The failure-phase analysis found verification running at 19.8% to 46.1% of
search plus verification, median 42.1%, so a change that adds verifier
obligations could plausibly pay for its search savings twice over. It does not.
`verification_ms` tracks `search_ms` proportionally on every one of the thirteen
instances, in both directions. Replacing a stored constant output with an
existential obligation costs the verifier nothing measurable.

## Reading this result

The kernel tests establish that the mechanism is real: a copy relation in which
only a matching output reaches a safe self-loop needs 2, 4 and 16 constant
regions for one, two and four paired variables, and a single existential region
in each case. The cohort establishes that the structure the kernel exhibits is
rare here. Both statements are true, and neither implies the other.

This is a cohort of 46 instances chosen for difficulty, of which only 15 reach
the game at all. One instance improving by 45% is a mechanism demonstration, not
a coverage result, and it is reported as such.

## Decision

**KEEP DEFAULT-OFF.** The implementation is correct, independently verified,
free of measurable cost when it does not fire, and occasionally a substantial
saving when it does. It does not move coverage on this cohort and there is no
evidence it would move coverage on the full corpus. It stays available as
`otf_sparse_existential` for configurations whose instances share targets across
outputs, and as a control for later work.
