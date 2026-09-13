# S2: symbolic losing-input search, measured

Sprint package S2. The question is whether asking directly for an input that is
losing whatever the controller does, instead of sampling an uncovered input and
discovering afterwards that it was losing, changes what the solver answers.

Answer: **one instance is repeatably about 8% faster, and that is the whole of
it.** The coverage difference a single campaign appeared to show does not
survive repetition in the form it first took. Recommendation:
**KEEP DEFAULT-OFF**, with the speedup recorded.

## Run identity

| | |
|---|---|
| Cohort | the 46 instances frozen in `diagnostic-cohort.list` before any candidate existed |
| Arms | `s2-off` `otf_sparse_formula` and `s2-on` `otf_sparse_losing_input`, both built from this revision, differing only in `acacia_spot_guarded_losing_input_search` |
| Binaries | off `4e6bbee3e9ef2165…`, on `4269c01b51222aee…` |
| Build | release, `-O3`, LTO, `b_ndebug`, `acacia_compiler_profile=release` |
| Regime | 17 s cap, `MemoryMax=8G`, `MemorySwapMax=0`, one invocation at a time |
| Raw data | `_bm-logs.20260913-s2-ablation/`, gitignored; summaries and the repetition log are committed beside this file |

## What the single campaign showed, and why it was not the answer

| run | solved / 46 |
|---|---:|
| `s2-off` | 10 |
| `s2-on` | 11 |

Read naively that is "the losing-input search gains an instance". It is not.
Ten instances are solved by every arm measured in this sprint. The entire spread
across four campaigns sits on two instances, and both of them solve within one
second of the 17 s cap:

- `GF-G-contradiction7`, solved only by `s2-on`, at 16.33 s;
- `workstation_resupply_pb_3_pe_`, solved by both S1 arms at 16.02 s and 16.61 s
  and by neither S2 arm, although the S2 control is behaviourally identical to
  the S1 control.

A control that differs from another control is a measurement resolution problem,
not a result. Both instances were therefore rerun in five alternating paired
rounds on an otherwise idle machine.

## `GF-G-contradiction7`: a real, one-directional speedup

| round | 1 | 2 | 3 | 4 | 5 | solved |
|---|---|---|---|---|---|---|
| off | 15.81 | 16.48 | 16.98 | timeout | timeout | 3/5 |
| on | 14.44 | 15.17 | 16.05 | 15.23 | 14.76 | 5/5 |

`on` is faster in every round. Where both arms answer the paired difference is
0.93, 1.31 and 1.37 s; in the two rounds where `off` exceeds the cap the
advantage is at least 1.8 s. Mean of the answering runs is 15.13 s against
16.42 s, about 8%.

That is the mechanism working as designed: the losing input is found by one
universal quantification rather than after repeated sampling and re-expansion of
the node. The coverage consequence is downstream of the speedup and depends on
the cap. At a 20 s deadline both arms answer; at 14 s neither does. Reporting
this as "solves one more instance" would attribute to coverage what belongs to
an 8% saving that happens to straddle a deadline.

## `workstation_resupply_pb_3_pe_`: not evidence

| round | 1 | 2 | 3 | 4 | 5 | solved |
|---|---|---|---|---|---|---|
| off | 16.54 | timeout | timeout | timeout | 16.02 | 2/5 |
| on | 16.82 | timeout | timeout | 16.07 | 16.32 | 3/5 |

The sign changes between rounds: `off` is faster in rounds 1 and 5, `on` in
round 4, and both miss in rounds 2 and 3. This instance straddles the cap under
every configuration tested, including two that are behaviourally identical. It
supports no claim in either direction and is excluded from the comparison.

## Measurement notes worth keeping

The absolute times of a near-cap instance vary by about a second between runs of
the same binary, which is enough to change its verdict at a 17 s cap. CPU
frequency and package temperature were checked after the study and showed no
throttling, so this is ordinary run-to-run variance rather than thermal decay.

Two consequences. First, a one-instance difference on this cohort is not
evidence; only a repeated, one-directional paired difference is. Second, the
alternating paired design is doing real work here: a design that measured five
runs of one arm and then five of the other would have attributed the within-
session spread to the arm measured second.

## Decision

**KEEP DEFAULT-OFF.** The search is correct, independently reviewed, costs one
extra universal quantification per expansion because `Bad` is shared with the
path that already needed it, and produces a repeatable 8% saving on one of the
fifteen cohort instances that reach the game. That is worth keeping available as
`otf_sparse_losing_input` and worth measuring on a wider set before it is
considered for a default, but it is not a coverage result.
