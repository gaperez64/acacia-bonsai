# Forward safety-game solver: coverage sprint

## Decision

**PORTFOLIO.** The forward solver ships opt-in and must not become the default.

It answers **25 official 2026 instances that neither backward configuration can**, and fails
37 that they answer. Every gate that compares it against the backward solver fails on lost
answers — and **none fails on a wrong answer**. `acacia_forward_safety_solver` stays `false`.

## Frozen baseline

| item | value |
|---|---|
| Acacia | `2178290b` (master), branch `codex/syntcomp26-coverage-forward` |
| corpus | `syntcomp-benchmarks` `4105caf1`, 1,586 TLSF verified, 1,524 official instances |
| B | `best_decomp_rank_bucketed_semantic_mona`, `fbe85394…` |
| S | `..._semantic_mona_local`, `05c81e9b…` |
| F | `..._semantic_mona_forward` (local certificate and equivariant solver **off**) |
| protocol | one systemd scope per run, `MemoryMax=8G`, `MemorySwapMax=0`, sequential under a lock |

## Full 2026 coverage (G26-full, 17 s, 1,524 instances)

| arm | solved |
|---|---:|
| B | 1,056 |
| S | 1,065 |
| F | 1,053 |
| union B∪S | 1,065 |
| **union B∪S∪F** | **1,090** |

**+25 instances**, 13 UNREALIZABLE and 12 REALIZABLE, median 3.03 s. Families: `heim-double-x`
(5), `AllLights` (5), `robot_grid` (3), `infinite-race-u` (2), and singletons including
`elevator-paper-real` (0.07 s) and `generalized_buffer_pb_5` (2.24 s).

F loses 37 instances B or S solve. **Zero verdict disagreements across all 1,524.**

That the unique wins are half unrealizable is the mechanistically interesting part: forward
losing-propagation reaches a fixed-K refutation without ever constructing the winning region.

## Stages

**F0 — explicit oracle.** Builds the whole reachable AND/OR game and solves it retrogradely.
Agrees with an exhaustive backward fixed point on 11 hand-written games and 5,000 fixed-seed
random games; 1,421 certificates independently verified.

**F1 — lazy solver.** Expands only what one strategy or losing proof needs. Agrees with F0 on
all 5,000 games. Uses posets vectors and `partial_order().leq()`; environment nodes are
interned on the complete coordinate vector, never a coordinate sum.

**F2 — minimal losing antichain. Justified by measurement.** The losing set is upward closed;
posets ships sixteen downward-closed implementations and no upward-closed dual, so this is
hand-rolled by necessity — a flat vector with a coordinate-sum prefilter.

On tiny games its counters said nothing (peak antichain 5). On the frozen targets:

| arm | solved | `lift_gr1_pb_3` |
|---|---:|---:|
| antichain **on** | **3/20** | 0.47 s |
| antichain off | 2/20 | 1.04 s |

Turning it off loses `lift_gr1+_pb_3_pe_` outright and doubles the time on `lift_gr1_pb_3`.

**F3 — state-dependent minimal successors. Not justified.** For fixed `r` and input `i`, if
`τ_{i,a₁}(r) ≤ τ_{i,a₂}(r)` then `a₂` is unnecessary. On random games this removes a further
28.4% of successors beyond exact dedup (28,979 raw → 24,400 distinct → 17,465 minimal).

On the frozen targets it buys nothing:

| threshold | solved |
|---|---:|
| 0 (dedup only) | 3/20 |
| 64 (shipped) | 3/20 |
| ∞ (always minimise) | 3/20 |

Same instances, same times. It is not harmful — the cost is not measurable either — so it stays
behind its threshold and is recorded as unjustified rather than removed; instances with larger
per-node action lists may still benefit. **Caveat: only 3 of 20 targets are solved at all, so
this sweep rests on a thin base.**

**F5 — K schedule.** Mirrors the backward bound schedule; nothing is carried across bounds
because the rank transformer changes with K. A WIN is returned only after
`certificate_verifier` re-verifies it with an **unbounded** budget; on failure the backend
degrades to backward rather than returning an unsound answer. Synthesis always routes to the
backward solver, since `post_real` rebuilds actions with a different input partition.

## Gates

| gate | result | nature of failure |
|---|---|---|
| G0 unit/random | **PASS** — 28/0 unit, 221 Python, 5,000-game agreement | — |
| G1 frozen 40 | **FAIL** — 40 → 37 | 3 lost answers, **0 wrong** |
| G3 syntcomp25 panel | **FAIL** — 113 → 105 | 12 lost (11 TIMEOUT, 1 UNKNOWN), **0 wrong** |
| G4 correctness corpus | **FAIL** — 551 Ok, 5 Fail | 5 `UNKNOWN`, **0 wrong** |
| G26-full | **+25 unique**, union 1,065 → 1,090 | **0 disagreements in 1,524** |

G4's five failures deserve a note. The forward solver returns `UNKNOWN` on hitting its resource
cap, as designed — `RESOURCE_LIMIT` is never a verdict. The harness scores an honest "cannot
decide" as a failure while tolerating a solver that simply runs out the clock; the backward
build times out on the same instances and is scored as a timeout. The difference is reporting
discipline, not correctness.

## What has been tried and rejected

- **Making forward the default.** Rejected: it loses answers on G1, G3 and G4.
- **F3 Pareto minimisation as a coverage lever.** No measurable effect on the frontier targets.
- **Predicting from `actions_seen` which families forward could handle.** Wrong: that counter is
  cumulative backward work per CPre call, not action-table size, so a large value indicates the
  backward search was enormous — a reason to expect forward to help, not to doubt it. The two
  `lift_gr1` instances predicted to fail are among the forward-unique wins.

## Next theorem boundary

- `arbiter_on_inpchange_pb_5` is unsolved at 60 s with 53 automaton states, 16 rank
  coordinates and 736 actions per pass, and forward does not solve it either. Small by every
  measure collected; the mechanism is unexplained.
- The losing antichain is a flat vector with linear `subsumes`. On tiny games it already showed
  5,540 invalidation scans for 1,122 invalidated nodes. Indexing it — or contributing a genuine
  upward-closed structure to posets — is the obvious next step now that F2 is justified.
- Why is half the forward-unique set unrealizable? If fixed-K refutation is systematically
  cheaper forward, a refutation-only forward mode may be worth more than the full solver.
