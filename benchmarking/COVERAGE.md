# Coverage frontier

Where the solver stops on the official SYNTCOMP 2026 set, which families it
stops on, and what it was doing when it stopped -- plus the forward safety-game
solver that answers a different 25 of them.

## What is in here

- [SYNTCOMP26-COVERAGE-FRONTIER.md](#syntcomp26-coverage-frontier) — where Acacia stops on the full 2026 set, by family and by failure kind.
- [FORWARD-COVERAGE-SPRINT.md](#forward-coverage-sprint) — the forward solver answers 25 instances backward cannot, and loses 37 it can — opt-in, never the default.


---

## SYNTCOMP 2026 coverage frontier

<sub>Was `benchmarking/SYNTCOMP26-COVERAGE-FRONTIER.md`. Snapshot pinned to `2178290b`. The frontier has moved since; the family analysis and the failure-kind census have not been superseded.</sub>

Where Acacia-Bonsai stops on the full official 2026 LTL-realizability set, which families it
stops on, and what the solver was doing when it stopped.

### 1. Frozen revisions and binaries

| item | value |
|---|---|
| Acacia | `2178290b` (master; PRs #122 and #123 merged) |
| corpus | `syntcomp-benchmarks` `4105caf1`, materialized and **verified: 1,586 TLSF files** |
| official set | 1,524 instances, `tests/suites/benchmarks/syntcomp26/all.list` |
| B — backward baseline | `best_decomp_rank_bucketed_semantic_mona`, binary `fbe85394…` |
| S — backward + local certificates | `best_decomp_rank_bucketed_semantic_mona_local`, binary `05c81e9b…` |
| diagnostics build (A6) | `..._semantic_mona_diag`, binary `aef21524…`, probe **off** |
| protocol | one transient user-systemd scope per invocation, `MemoryMax=8G`, `MemorySwapMax=0`, sequential, staged caps 1/5/17/60 s |

B and S differ **only** in `acacia_local_certificate`. The arms were verified at the binary
level with `nm` rather than from `meson configure`, because turning the probe on by default
makes `meson configure` report `true` for build directories whose binaries contain no probe.

### 2. Coverage over all 1,524 instances

Cumulative instances decided, by cap:

| arm | 1 s | 5 s | 17 s | 60 s | unsolved |
|---|---:|---:|---:|---:|---:|
| B backward | 936 | 1010 | 1056 | **1093** | 431 |
| S + local certificates | 940 | 1017 | 1065 | **1100** | 424 |
| P survey union | 942 | 1018 | 1065 | **1100** | 424 |

`P` is a **survey union**, not a runtime portfolio: no executable computes it.

**The local-certificate probe is worth 7 instances on the full set** — the first measurement
of its value beyond the two 180-instance panels it was tuned against. All seven are
realizable and all are decided far inside their cap:

| instance | cap | time |
|---|---|---:|
| `box-real` | 1 s | 0.051 s |
| `robot_grid_pb_5_1_pe_` | 1 s | 0.066 s |
| `finding_nemo_pb_1_pe_` | 1 s | 0.134 s |
| `lift_pb_3_pe_` | 1 s | 0.153 s |
| `g-real-real` | 5 s | 2.782 s |
| `finding_nemo_pb_2_pe_` | 5 s | 3.789 s |
| `lift_unary_enc_pb_3_pe_` | 17 s | 4.530 s |

B's 431 unsolved are 412 timeouts, **17 memory exhaustions at 8 GiB**, one OOM kill, and one
UNKNOWN.

### 3. Three wrong corpus annotations

The campaign checks every decisive verdict against the `//STATUS` annotation and stops on a
conflict. Three fired, and **all three were the annotation, not the solver** — each confirmed
by `ltlsynt` 2.15.1.dev independently:

| instance | annotated | Acacia | `ltlsynt` | evidence |
|---|---|---|---|---|
| `lilydemo04_modified` | realizable | UNREALIZABLE | UNREALIZABLE | the file's own comment says the modification exists "to make the spec unrealizable" |
| `lilydemo15` | unrealizable | REALIZABLE | REALIZABLE | two independent solvers agree |
| `lilydemo16` | unrealizable | REALIZABLE | REALIZABLE | two independent solvers agree |

A scan of all 1,586 files found no further case. Fifteen `chomp` instances look like the same
pattern and are not: their comment says the spec is unrealizable only for `N=M=1`, and every
`chomp` instance in the set has `N,M ≥ 2`.

Conflicts are collected rather than fatal, and the runner exits **3** so a conflicted campaign
cannot be mistaken for a clean one. The check is deferred, never waived: a conflict adjudicated
*against* Acacia would be a correctness failure and would stop the sprint.

### 4. Family frontiers

Parameters come from the TLSF release manifest's `param:<template>:<name>=<value>` origins, not
from filenames. Of 1,524 instances, **734 have exact parameter origins** (454 one-parameter,
280 two-parameter) and 790 are direct files with no parameters at all — marked
`parameter_confidence=none`, since only their *family grouping* is a guess.

| classification | families |
|---|---:|
| all solved | 215 |
| **clean cutoff** | **34** |
| singleton hard | 50 |
| direct cluster | 12 |
| mixed direct | 10 |
| all unsolved | 4 |
| **total** | **325** |

57 families are formally orderable (identical parameter names in identical order, all numeric).
Multi-parameter families use the **componentwise** order, never a lexicographic total order, so
a "cutoff" is claimed only where one exists. **77 boundary pairs**: 73 with a genuine covering
solved neighbour, 4 with none.

Representative cutoffs: `amba_case_study` n=2→3, `abcg_arbiter` n=3→4, `arbiter` n=5→6,
`amba_decomposed_arbiter` n=6→7.

### 5. The frozen top-20 targets

Selected from 40 preselected candidates using the committed weight tables, then ranked with
mechanism data from the diagnostics campaign. **Target selection did not consult `ltlsynt` at
any point**; the set is frozen before any external annotation.

19 families, 2 memory-quota rows, none missing diagnostics, scores 19–49.

| # | score | instance | aut states | rank coords | actions |
|---:|---:|---|---:|---:|---:|
| 1 | 49 | `arbiter_on_inpchange_pb_5_pe_` | 53 | 16 | 736 |
| 2 | 46 | `chomp_pb_4_2_pe_` | 75 | 22 | 30,070 |
| 3 | 46 | `full_arbiter_unreal1_pb_3_8_pe_` | 40 | 7 | 14,120 |
| 4 | 46 | `full_arbiter_unreal1_pb_4_6_pe_` | 52 | 9 | 25,968 |
| 5 | 46 | `full_arbiter_unreal2_pb_5_pe_` | 43 | 21 | 249,408 |
| 6 | 46 | `prioritized_arbiter_pb_7_pe_` | 47 | 43 | **30,100,344** |
| 7 | 46 | `rw_arbiter_pb_4_pe_` | 39 | 17 | 17,664 |
| 8 | 44 | `amba_case_study_pb_3_pe_` | 104 | 20 | 253,248 |
| 9 | 44 | `load_balancer_unreal1_pb_5_6_pe_` | 210 | 17 | 50,110 |
| 10 | 42 | `lift_gr1+_pb_3_pe_` | 270 | 136 | **87,281,993** |
| 11 | 42 | `lift_gr1_pb_3_pe_` | 225 | 91 | **115,278,358** |
| 12 | 41 | `robot_grid_pb_3_3_pe_` | **10,514** | 82 | 721 |
| 13 | 41 | `round_robin_arbiter_unreal1_pb_3_9_pe_` | 755 | 99 | 1,288 |
| 14 | 40 | `arbiter_with_buffer_pb_6_pe_` | 1,377 | 19 | 188,416 |
| 15 | 40 | `arbiter_with_cancel_pb_6_pe_` | 1,120 | 13 | 5,952 |
| 16 | 40 | `collector_v1_pb_11_pe_` | 15,372 | **15,371** | 90 |
| 17 | 40 | `collector_v3_pb_9_pe_` | 2,835 | 10 | 0 |
| 18 | 40 | `simple_arbiter_with_hints_pb_8_pe_` | 1,607 | 9 | 22,272 |
| 19 | 20 | `robot-to-target-charging3` (memory) | 99 | 20 | 1,558,304 |
| 20 | 19 | `Morning2_06e9cad4` (memory) | 198 | 183 | 0 |

Every row carries its solved boundary neighbour in
`benchmarking/syntcomp26-frontier-targets.tsv`.

### 6. What the mechanisms say

**Correction.** An earlier version of this section read `actions_seen` as the size of an
instance's semantic action table and concluded that a forward reachable solver was an
implausible answer for the "action explosion" families. That was wrong twice over.

`observe_action()` is called inside `cpre_inplace`'s loop over the action list, once per
action **per CPre call**, so `actions_seen` is cumulative backward work across every
fixed-point iteration — not a table size. A table of 115 million entries would be gigabytes;
what the number actually says is that the backward fixed point ground through an enormous
amount of work. That is a *symptom* of the backward search being large, which is evidence a
reachable forward search might help, not evidence against it.

The measurement settled it: `lift_gr1_pb_3_pe_` (115,278,358 `actions_seen`) and
`lift_gr1+_pb_3_pe_` (87,281,993) are both solved by the forward backend in 0.42 s and
2.42 s, having been unsolved by B and S at 60 s.

What the diagnostics do separate reliably:

- **Automaton size.** `robot_grid_pb_3_3` reaches 10,514 states with 721 actions per pass.
- **Rank dimension.** `collector_v1_pb_11` carries 15,371 numeric rank coordinates against 90
  actions per pass — the opposite shape.
- **Backward work.** `prioritized_arbiter_pb_7`, `lift_gr1_pb_3` and `lift_gr1+_pb_3` spend
  30.1M, 115.3M and 87.3M cumulative action applications. This measures how far the backward
  fixed point got, not how big the instance is.
- **Measured on the wrong workers.** `arbiter_on_inpchange_pb_5` appears small by every
  measure — 53 automaton states, 16 rank coordinates, 736 actions — and unsolved at 60 s. A
  later trace shows why that reading is wrong: those columns describe classic workers that
  finish in 47 ms reporting `unknown`, while the instance actually runs on the **equivariant
  solver**, reaching `equivariant-after-closure` with 5 clients, 10 blocks and 6 orbits. The
  equivariant fixed point emits no snapshots, so its cost is uninstrumented. Nothing here was
  measured about the computation that actually runs. See
  `frontiers/param-tlsf-arbiters_zoo-parametric-arbiter_on_inpchange.tlsf.md`.

The honest summary is that the diagnostics distinguish *shapes* of backward difficulty well,
and that predicting from them which shapes a forward solver can handle was premature. The
frozen set contains all of them, which is what allowed the prediction to be tested and
falsified rather than merely asserted.

### 7. The memory cohort, and why it needed a quota

All 22 memory-bounded instances are direct TLSF files with **no parameters**, so they cannot
earn the weights that dominate the ranking — minimal point of an exact parametric family, an
ordered solved neighbour, a family with ≥3 observed points — and cap near 14 against 40. They
were not penalised; the score could not see them. Left alone, the target set would have
contained none of the one failure mode a forward reachable solver is most likely to move: the
backward antichain exhausting 8 GiB rather than 60 s.

Quota slots are therefore reserved and filled round-robin across **distinct** families. Filling
by score alone put all four in `Morning`, which would have characterised one benchmark
generator rather than the failure mode.

### 8. Open questions per family

- Why does `arbiter_on_inpchange` stop at n=5 when every size measure stays small?
- Is `collector_v1`'s 15,371-coordinate rank vector genuinely necessary, or an artefact of the
  encoding?
- For the action-explosion families, is the semantic quotient already tight, or do 30–115
  million actions collapse further under a state-dependent minimisation (Stage F3)?
- Do the memory-bounded families have small reachable strategies despite an unrepresentable
  permissive region?

### 9. Reproduction

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out tlsf-corpus
python3 benchmarking/family_metadata.py
python3 benchmarking/run-syntcomp26-coverage.py --bin <B> --solver-label B \
    --list tests/suites/benchmarks/syntcomp26/all.list \
    --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
    --tlsf-corpus tlsf-corpus --caps 1,5,17,60 --conflict-policy collect \
    --output benchmarking/_coverage26/B-runs.tsv
python3 benchmarking/adjudicate-status-conflicts.py --conflicts … --tlsf-corpus tlsf-corpus
python3 benchmarking/build-coverage-frontier.py --runs B=… --runs S=…
python3 benchmarking/select-frontier-targets.py
python3 benchmarking/run_diag_targets.py --build <diag> --tlsf-map … --tlsf-corpus … --csv …
python3 benchmarking/freeze-frontier-targets.py
```

Every table above is regenerable by these committed scripts.

---

## Forward safety-game solver: coverage sprint

<sub>Was `benchmarking/FORWARD-COVERAGE-SPRINT.md`. Closed: PORTFOLIO. `acacia_forward_safety_solver` stays `false`.</sub>

### Decision

**PORTFOLIO.** The forward solver ships opt-in and must not become the default.

It answers **25 official 2026 instances that neither backward configuration can**, and fails
37 that they answer. Every gate that compares it against the backward solver fails on lost
answers — and **none fails on a wrong answer**. `acacia_forward_safety_solver` stays `false`.

### Frozen baseline

| item | value |
|---|---|
| Acacia | `2178290b` (master), branch `codex/syntcomp26-coverage-forward` |
| corpus | `syntcomp-benchmarks` `4105caf1`, 1,586 TLSF verified, 1,524 official instances |
| B | `best_decomp_rank_bucketed_semantic_mona`, `fbe85394…` |
| S | `..._semantic_mona_local`, `05c81e9b…` |
| F | `..._semantic_mona_forward` (local certificate and equivariant solver **off**) |
| protocol | one systemd scope per run, `MemoryMax=8G`, `MemorySwapMax=0`, sequential under a lock |

### Full 2026 coverage (G26-full, 17 s, 1,524 instances)

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

### Stages

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

### Gates

| gate | result | nature of failure |
|---|---|---|
| G0 unit/random | **PASS** — 28/0 unit, 221 Python, 5,000-game agreement | — |
| G1 frozen 40 | **FAIL** — 40 → 37 | 3 lost answers, **0 wrong** |
| G3 syntcomp25 panel | **FAIL** — 113 → 105 | 12 lost (11 TIMEOUT, 1 UNKNOWN), **0 wrong** |
| G4 correctness corpus | **FAIL** — 551 Ok, 5 Fail | 5 `UNKNOWN`, **0 wrong** |
| G2s per-target cycles | **PASS** — geomean 10.9x, no target regresses | see caveat below |
| G26-full | **+25 unique**, union 1,065 → 1,090 | **0 disagreements in 1,524** |

#### G2s passes, and should not be read as a win

Every one of the ten targets improves, `lift_pb_3_pe_` by 2152x and `lift4` by 137x, for a
geometric mean of 10.9x and `decision=adoption-candidate`. That is not evidence the forward
solver is ten times faster.

G2s measures **cycles on instances the candidate answers**, and this candidate's
characteristic behaviour is to give up quickly with `UNKNOWN` rather than exhaust the cap.
A cheap non-answer scores as a cheap run. The gates that count *answers* say the opposite:
G1 loses 3 of 40, G3 loses 12 of 180, G4 returns 5 `UNKNOWN`. A solver that instantly
returned `UNKNOWN` on everything would post a spectacular G2s and be worthless.

The two gates measure different things, and only one of them is about whether the solver is
useful.

G4's five failures deserve a note. The forward solver returns `UNKNOWN` on hitting its resource
cap, as designed — `RESOURCE_LIMIT` is never a verdict. The harness scores an honest "cannot
decide" as a failure while tolerating a solver that simply runs out the clock; the backward
build times out on the same instances and is scored as a timeout. The difference is reporting
discipline, not correctness.

### What has been tried and rejected

- **Making forward the default.** Rejected: it loses answers on G1, G3 and G4.
- **F3 Pareto minimisation as a coverage lever.** No measurable effect on the frontier targets.
- **Predicting from `actions_seen` which families forward could handle.** Wrong: that counter is
  cumulative backward work per CPre call, not action-table size, so a large value indicates the
  backward search was enormous — a reason to expect forward to help, not to doubt it. The two
  `lift_gr1` instances predicted to fail are among the forward-unique wins.

### Next theorem boundary

- `arbiter_on_inpchange_pb_5` runs on the **equivariant** solver, whose fixed point emits no
  snapshots; the small mechanism numbers recorded for it come from classic workers that finish
  in 47 ms without deciding. The forward preset disables the equivariant solver, so B and F
  have never been compared like-for-like on it. Instrumenting the equivariant fixed point is
  the prerequisite for any claim about this family.
- The losing antichain is a flat vector with linear `subsumes`. On tiny games it already showed
  5,540 invalidation scans for 1,122 invalidated nodes. Indexing it — or contributing a genuine
  upward-closed structure to posets — is the obvious next step now that F2 is justified.
- Why is half the forward-unique set unrealizable? If fixed-K refutation is systematically
  cheaper forward, a refutation-only forward mode may be worth more than the full solver.
