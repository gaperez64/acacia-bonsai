# Adaptive arm portfolios and OTFUR work elimination

Living record. What the sparse guarded solver stopped doing, what the solver's
arms can and cannot reach, and what the handoff that proposed this sprint got
wrong about the tree it was written against.

**Status: the code is on master and unit-gated. Three campaigns have run** — a
22-arm isolated census on the 180-instance panel, 10 arms on the 353 instances
the shipping configuration cannot solve, and a fixed-budget race curve (§9).
**G1, G3 and G4 have since run and all pass (§10); G5 is skipped by decision.** Nothing here is a full-corpus coverage claim,
and `docker_default` is unchanged.

Raw evidence: [portfolio-evidence-20260910/](portfolio-evidence-20260910/),
summary TSVs only, as [config-selection-20260906/](config-selection-20260906/)
does; full binary and harness hashes are in its `PROVENANCE`.

## 1. Frozen environment

| item | value |
|---|---|
| Acacia, campaigns | `b6da274f` (this sprint's patch stack on `3fb9f113`) |
| Acacia, pre-patch baseline | `3fb9f113`, kept built in `../acacia-prepatch` for G3 |
| campaign binary | `otf_sparse_formula`, release profile, SHA-256 `5451c91e2a133ac5…` |
| baseline binary | `otf_sparse_formula` at `3fb9f113`, SHA-256 `2d5707332ec4ff2e…` |
| harness | a pinned copy of `run-syntcomp26-coverage.py`, SHA-256 `94ae42592693d980…` |
| Posets | `139e143` |
| Spot | 2.15.1.dev, `/usr/local/lib/pkgconfig/libspot.pc` |
| compiler | GCC 16.2.1 20260819 (Red Hat 16.2.1-2) |
| host | 11th Gen Core i7-11850H, 8 cores / 16 threads, 15 GB, zram swap ~7 GB in use |
| protocol | 17 s cap (the panel census staged 1/5/17, read at ≤ 17; the rest uniform), 8 GiB `MemoryMax`, zero swap, one invocation at a time |

The harness was run from a pinned copy so that a branch switch in the work tree
could not change the instrument mid-campaign; it then needs `--status-exceptions`
and `--acacia-sha` passed explicitly, because both default to paths relative to
the script's own location (see #159).

Memory-limit outcomes deserve less trust than timeouts here: with zram near full
there is little headroom above the 8 GiB cap. Only the TAA provider arms (§6)
produced MEMOUTs in any number.

The two gate binaries named below were rebuilt after the gates had run (§11), so
their recorded hashes no longer identify artifacts on disk. The campaign binary
`5451c91e2a133ac5…` is unaffected: it was frozen read-only before that happened
and still matches.

## 2. What the handoff assumed, and what is actually true

The sprint specification was written against `master` at `3fe3598a`. Four of its
statements do not survive contact with the tree.

**Its reading list is one commit stale.** `bc812182` consolidated eight closed
sprint records into three. `SPOT-STREAMING-OTFUR-SPRINT.md`,
`SPOT-OTF-API-AUDIT.md` and `POST-PR125-COVERAGE-SPRINT.md` are sections of
[OTF-AND-SPOT.md](OTF-AND-SPOT.md); `SYNTCOMP26-COVERAGE-FRONTIER.md` is in
[COVERAGE.md](COVERAGE.md); `FORWARD-DATA-STRUCTURES-REVIEW.md` is in
[DATA-STRUCTURES.md](DATA-STRUCTURES.md).

**Its frozen baseline predates the memory fix.** PR #147 landed after
`3fe3598a`. CI had been running four solvers per runner while handing each the
default cap of 80% of the machine, and the handoff's "concurrency can hurt
individual arms" evidence — 46 isolated against 44 raced, on a 50-instance
attribution cohort — is two instances wide to begin with.

**The existing arm census contained no guarded arm.** `OTF-AND-SPOT.md` reports
twelve arms over the panel and concludes that four reach the twelve-arm ceiling
at 137/180. Its `B`/`S`/`F` are *backward*, *backward-with-local-certificates*
and *forward* (legend, `OTF-AND-SPOT.md:1036-1038`), not the sparse guarded
backend that produced the last sprint's +66. That is why §6 was worth running.

**P4 was already attempted twice.** O3 batched invalidation was reverted and O5
indexed a generator list that peaks at 177 entries. Neither should be re-run.

## 3. An arm census was being read at the wrong cap

`select-portfolio-arms.py` ranked subsets by "the union of decisive answers at
cap 17s" while never reading `smallest_cap_solved`, and
`run-syntcomp26-coverage.py` fills `decisive_result` from the *earliest* staged
cap. `_coverage26/B-runs-summary.tsv` and `S-runs-summary.tsv` hold 37 and 35
instances first decided at 60 seconds. Filtering at `--cap 17` reproduces PR
#125's published table; not filtering does not, and reorders the ranking:

| configuration | published (17 s) | `--cap 17` | unfiltered |
|---|---:|---:|---:|
| B | 1056 | **1056** | 1093 |
| S | 1065 | **1065** | 1100 |
| F | 1053 | **1053** | 1053 |
| B ∪ S ∪ F | 1090 | **1090** | 1113 |

## 4. Losing-region work, measured

From the `spot-provider-replay` sweep: 8 formulas × every input/output partition
× K in {1,2,3}, 72 games, each cross-checked against the explicit forward game.

| revision | loss events | insertions | broad scans | node checks | subsumption queries | prefilter skips |
|---|---:|---:|---:|---:|---:|---:|
| before | 136 | 117 | 136 | 1,059 | 1,471 | — |
| single-pass subsumer | 136 | 117 | 136 | 1,059 | 1,471 | — |
| growth-gated scan | 136 | 117 | **117** | **757** | **975** | — |
| mass prefilter | 136 | 117 | 117 | 757 | 975 | **187** |

19 of 136 loss events add no generator, so the pass they triggered searched a
region already searched. Gating the scan on the insert that decides whether the
region grew removes exactly those; testing only the new generator cuts node
checks 28.5% and subsumption queries 33.7%. The search is unchanged — expansions
529, choices 329, nodes 229 in every revision. A probe measured the equivalence
argument directly: maximum `enqueue_loss` depth 2, zero nested calls that scan.

`reopened_sources` moves 127 → 99 without the search moving. It counts attempts,
and `reopen_enqueues`, added to make that legible, is 55 either way.

The mass filter's lemma — `l <= r` forces a no-larger support and a no-larger
shifted mass `M(r) = Σ_{supp(r)} (r(q)+1)`, and equal mass under `<=` leaves only
equality — is checked on all 4,096 pairs of the exhaustive order test.

## 5. Guard covering, measured

`acacia_spot_guarded_inequality_covering`, default **off**.

| mode | expansions | choices | nodes | node checks | reopen enqueues |
|---|---:|---:|---:|---:|---:|
| exact (default) | 529 | 329 | 229 | 757 | 55 |
| `<=` covering | **525** | **325** | **227** | **751** | 59 |

The `<= target` predicate was already in the file as `Oracle::preimage`'s
downward kind; the handoff's proposed new BDD construction would have been
redundant. The differential test first checked only active choices and passed in
both modes; checking every claimed region against `rows::evaluate_sparse` makes
it discriminate, and forcing the exact assertion under `<=` covering now fails.
Margins are four nodes on 3-AP games: the mechanism works, and whether it pays
is untested.

## 6. The arm census: roughly five solvers wearing twenty-two names

22 arms, each run alone over the panel; zero verdict conflicts. Every arm is
polarity-pure.

| arm | solved | PAR-2 |
|---|---:|---:|
| `unreal:formula:spot-guarded-sparse` | **85** | **3302.8** |
| `unreal:formula:forward` / `:spot-guarded` | 84 | 3333.8 / 3319.8 |
| `unreal:automaton:` forward / sparse / dense | 83 | 3341.7 / 3329.6 / 3328.6 |
| `unreal:formula:backward` | 83 | 3388.7 |
| `unreal:automaton:backward` | 81 | 3454.3 |
| `real:*` frozen-graph arms (8 of them) | 47–48 | 4522.6–4570.4 |
| `unreal:formula:spot-guarded:` lazy / eager | 36 | 4912.0 / 4908.7 |
| `real:*:spot-guarded:` lazy / eager (4) | 28 | ~5204 |

| k | best fixed subset, isolated union |
|---:|---:|
| 1 | 85 |
| 2 | 133 |
| 3 | 140 |
| 4 | 142 |
| **5** | **143** |
| all 22 | **143** |

**The shipping preset is an optimal fixed four.** The isolated union of
`otf_sparse_formula`'s four arms is 142, equal to the best four-arm subset, and
the 22-arm oracle beats it by one instance, `heim-double-x-real.ltl`, reached
only by the guarded *real* arms. **No arm has a unique answer.**

What the 22 collapse to, measured rather than assumed:

| collapse | evidence |
|---|---|
| `any` ≡ `small` | identical answer sets on all five backends tested (28–48) |
| dense guarded ⊆ sparse guarded | identical on real and automaton-unreal, 84 ⊂ 85 on formula |
| TAA lazy ≡ TAA eager | identical on both polarities; on real, identical MEMOUTs too |
| TAA ⊂ frozen graph | strict subset on both polarities (36 ⊂ 84, 28 ⊂ 47) |
| guarded formula ⊇ forward formula | 84 ⊂ 85 on the panel, and it holds on the hard set |

The TAA result is the handoff's R2 question answered by its own matched control
(§R2.4): laziness changes memory pressure — 19 against 44 MEMOUTs on the
formula-unreal arm — and not one verdict. The loss is in the TAA construction.

The table's `k`-subsets name `real:any:*` arms where `real:small:*` would do
equally; that is alphabetical tie-breaking over provably identical arms.

## 7. What the shipped configuration cannot solve

The 353 instances `otf_sparse_formula` leaves undecided at 17 s in the closing
threeway campaign (1,171 of 1,524), each run alone under 10 arms chosen from §6's
collapses — nine distinct behaviours plus a dense-guarded control.

| arm | unlocked | in the shipped preset |
|---|---:|---|
| `real:small:spot-guarded-sparse` | **9** | no |
| `unreal:formula:spot-guarded-sparse` | 8 | yes |
| `unreal:automaton:spot-guarded-sparse` | 7 | no |
| `unreal:automaton:spot-guarded` (control) | 5 | no |
| `real:small:backward` / `forward` | 4 / 4 | yes |
| `unreal:automaton:backward` | 2 | no |
| `unreal:automaton:forward` | 1 | yes |
| `unreal:formula:backward` / `forward` | 1 / 1 | no |

**20 of 353 are decided by some arm, and 14 of those by an arm the shipped
preset already contains, simply running alone.** Those are contention relief,
not capability: the same arm, the same cap, one scope instead of four.

**Five are capability**, all from one arm, `real:small:spot-guarded-sparse`, and
mostly under a second: `SPIPureNext` (0.42 s), `heim-double-x-real`,
`ordered-visits-choice-real`, `thermostat-F-real`, `robot_grid_pb_5_5_pe_`. The
sixth instance beyond the shipped arms, `g-unreal-116`, lands at 17.00 s and
16.93 s under two different arms: boundary noise.

The control: panel-identical dense and sparse automaton-unreal arms separate on
the hard set, 5 ⊂ 7, in the direction that vindicates dropping dense.

### SPIPureNext

The repository recorded this instance as one "no Acacia version is known to
answer" (`f271f396`). The guarded real arm answers REALIZABLE in 0.42 s. The TLSF
carries no `//STATUS`, so the verdict needed an independent source:

| solver (SYNTCOMP 2026 `tlsfReal`) | outcome |
|---|---|
| Acacia-Bonsai, all four 2026 submissions | out, ~40 GB, ~1,750 s |
| ltlsynt acd / ds / lar | out |
| **SemML** | **REALIZABLE, 9.9 s** |
| **Strix** | **REALIZABLE, 1.7 s** |

Both are recorded `NEW-REALIZABLE` in the competition's model-checking results.
Locally, six ltlsynt strategies fail: four exhaust 8 GiB in about 78 s, two time
out. Acacia 1.x's earlier REALIZABLE does not count as corroboration, being the
crash-misreport kind documented in `LTLSYNT-GAP.md`. Source: Zenodo
`10.5281/zenodo.21451603`, `SyntComp26_prelim.zip`, SHA-256 `d64b3648…` — not the
`8a703832…` release the corpus is pinned to; the record has been updated since,
and it was used only to read verdicts for this instance.

## 8. The regression the patch stack removes

`OTF-AND-SPOT.md` records `infinite-race-u10.ltl` as `otf_sparse_formula`'s one
reproducible 17 s loss, introduced when the guarded formula arm replaced the
forward one. Alone, the guarded arm is the faster of the two (8.86 s against
9.30 s): the loss was a race effect, not a weaker arm. Paired A/B on the shipped
race, same machine and session, alternating order:

| | 17 s cap, 5 rounds | 60 s uncensored |
|---|---|---:|
| pre `3fb9f113` | TIMEOUT, 16.22, TIMEOUT, 16.62, 16.54 — **3/5** | 18.10 s |
| post `b6da274f` | 10.65, 10.56, 10.18, 11.37, 11.63 — **5/5** | 10.89 s |

The pre-patch uncensored time matches the recorded 17.95 s. The effect is the
whole stack's, most plausibly #154/#155 acting on the guarded formula arm;
inequality covering is off in this preset. One instance, not a corpus claim.

## 9. The race curve (Regime F)

Each configuration is one race invocation per instance under the same budget:
`CPUQuota=400%` (four CPUs of time), one 8 GiB scope, zero swap, 17 s. Instance
set: the panel plus the hard set, 494 after an overlap of 39.

`AllowedCPUs` cannot be used here: the user manager is not delegated the cpuset
controller, so systemd accepts the property and drops it. `CPUQuota` is
enforced — eight busy loops under a 400% quota used 12.3 CPU-seconds in 3 s with
30 throttle events, against 24 unthrottled.

| config | arms | panel / 180 | its isolated union | lost to racing | hard / 353 | total / 494 |
|---|---:|---:|---:|---:|---:|---:|
| `race4-shipped` | 4 | 140 | 142 | 2 | 4 | 144 |
| `race5-plus-guarded-real` | 5 | 141 | 143 | 2 | 6 | **146** |
| `race2-best` | 2 | 131 | 133 | 2 | **9** | 140 |

Racing costs the same 2 instances whatever the worker count, so the differences
below are about what each configuration can reach, not about racing overhead.

**The fifth arm pays.** Adding `real:small:spot-guarded-sparse` nets +2: it gains
all five of §7's capability instances and loses three hard-set instances to the
contention it adds (`GF-G-contradiction6`, `infinite-race-u11`,
`infinite-race-unequal-25`). That is a preset candidate, not a preset change: it
is one run, the three losses need repetitions, and the admission rules ask for a
full-corpus campaign first.

**Two arms is worst overall and best on the tail** — 140 total, but 9 hard
instances against 4 and 6. Fewer workers means more budget each, which buys hard
instances and costs easy ones.

**Choosing the configuration per instance has real headroom.** The best fixed
configuration reaches 146; an oracle over these three reaches **155**, and the
shipped four contributes nothing unique, so `race5` and `race2` alone realise the
whole 155. That headroom is worker count, not arm identity — the opposite of what
P5 proposed, and the one portfolio hypothesis this sprint has not closed. Two
cautions: this instance set is 71% hard instances where the corpus is 23%, so the
relative gain here overstates the corpus gain; and a real selector has to predict
the choice per instance and pay for the prediction.

## 10. Correctness

- `meson test --suite unit`: **42 tests, Fail: 0**, both flag states built from
  the same sources in every build; the 5,000-game harness agrees four ways under
  exact and `<=` covering.
- Zero verdict conflicts across 22 census arms, 10 hard-set arms, and every race
  so far.
- Config frontends agree on all 44 shared scalar options. The registry has
  **four** frontends, not the three CLAUDE.md names: the `build_conf.set` line in
  `meson.build` is the fourth, and `tests/check-config-frontends.py` compares
  only two.
### Gates

Candidate: master's `otf_sparse_formula`, built fresh, SHA-256 `ba3159daef74bd1e…`.
Baseline: the same preset at `3fb9f113`, SHA-256 `2d5707332ec4ff2e…`.

| gate | result |
|---|---|
| G1, frozen sentinels | **PASS** — 40 rows verified, 40/40 solved, 0 verdict changes, 0 coverage losses |
| G3, landing bar | **PASS** — syntcomp25 124 → 125, syntcomp26 140 → 140, 0 verdict changes, 0 coverage losses |
| G4, candidate | **PASS** — Ok 575, Fail 0, Timeout 49, no false-positive/negative marker |
| G4, the #156 preset | **PASS** — identical: same counts, and the same 49 instances time out |
| G5 | skipped by decision, below |

**G3 is the gate that speaks to coverage, and it says the patches cost nothing
and gain nothing measurable on the panels.** Its one extra answer,
`workstation_resupply_pb_3_pe_`, is a cap-boundary flip: TIMEOUT at 17.053 s
against REALIZABLE at 16.673 s. PAR-2 moves −14.9 s on syntcomp25 and −1.8 s on
syntcomp26, both inside the 21.1 s / 12.1 s noise floors. The patches' one
user-visible win is §8's, on an instance these panels do not contain.

G1's own PAR-2 line reads 101.867 s against 39.747 s. That is **not** a measured
speedup: the gate builds its baseline from the frozen `baseline_seconds` in
`regress-expected.tsv` rather than from the supplied baseline binary, so the two
sides were never measured under the same conditions. G3 is the measured
comparison.

G4 being identical on both builds is itself a result, because the option really
is compiled in — the generated `acacia_build_config.hh` reads 1 in the preset
build and 0 in the candidate. So inequality covering changes internals without
changing a single corpus verdict. `SPIPureNext.ltl` is among the 49 timeouts, via
`ab/large1`, for the memory reason `tests/meson.build` documents; CI does not run
the large suites.

The G1 **control** — the same gate with the pre-patch build in the candidate slot
— is **invalid and deliberately not repeated**. Its build directory records the
main work tree as its source directory, residue from a mistake in §11, so the
gate compared a testlog from one tree against expectations from another: 25
missing rows and 25 unexpected inputs, with no verdict changes and no coverage
losses. Its purpose was to show that G1's failures were environmental, and the
candidate passing outright makes that moot.
- **G5 is skipped by decision.** It checks that the TLSF frontend gives the same
  verdicts natively and through SyFCo, and nothing in this sprint touches the
  frontend. The preset that triggered it is sweep-role and off by default, and
  running it would need a full SyFCo reconversion — the pairs on disk cover 1,517
  of the 1,579 it expects — plus about 3,000 solver runs.

## 11. Corrections to the record

- **SPIPureNext is answerable.** `f271f396` calls it "an instance no Acacia
  version is known to answer". `real:small:spot-guarded-sparse` answers it, and
  Strix and SemML confirm the verdict. The synthesis-suite exclusion that commit
  made is still right: synthesis forces the backward backend, which does
  `bad_alloc`.
- **`infinite-race-u10` was not a capability regression** (§8).
- **#157's pinning verification was wrong.** It set `AllowedCPUs=0-1` and
  `CPUQuota=150%` together and read `nproc` = 2 as proof of the pin; recent
  coreutils `nproc` honours the CPU quota. `897f9cb7` now refuses `allowed_cpus`
  where cpuset is not delegated. This record's own previous §8 recommended
  `AllowedCPUs`; only `CPUQuota` works on a stock user manager.
- **`00a066ae` carried an unannounced file**, the first draft of
  `arm-census-report.py`, swept in by `git add -A`. That draft scores PAR-2 over
  answered instances only. `9db9902b` fixes it; it reaches master with #161.
- **Three G1 alarms were mine, not the solver's.** The first run reported 15
  failures, all `missing syntcomp25/<x>.ltl`: `tests/meson.build` skips
  TLSF-backed entries when `acacia_tlsf_corpus_dir` is empty, and it was empty in
  both builds. Exporting `ACACIA_TLSF_CORPUS` does not help — the gate scripts
  read it at run time, meson never does. The second attempt set the option with
  `meson setup --reconfigure -D… BUILDDIR` from the wrong directory, so meson took
  the cwd as the source tree, paired a master build directory with a branch that
  lacks `acacia_spot_guarded_inequality_covering`, and aborted; a later ninja
  regeneration then restored the empty option. `meson configure`, which takes only
  a build directory, is the command that cannot get this wrong. Both mistakes hid
  behind a third: `systemd-run --scope` does not propagate the wrapped command's
  exit status, so the driver logged `exit=0` over a meson error. Verify by
  outcome — here, the registered sentinel count — never by a wrapper's exit code.
- **A G4 prediction was wrong in form.** I expected `SPIPureNext` to *fail* G4 via
  `ab/large1`; it reaches the 30 s test timeout first, so it counts as a timeout,
  which the gate allows.
- **Two recorded binary hashes no longer identify artifacts on disk.** The G1 fix
  ran `meson compile` on both gate build directories, rebuilding them at 03:03
  and 03:04 — after the gates had run. The candidate `ba3159daef74bd1e…` is gone
  along with its worktree; the baseline is now `d9db7dab83e1d8ab…`, rebuilt from
  the same revision `3fb9f113` at the same preset and still kept in
  `../acacia-prepatch` for G3. Neither is a byte-identical replacement, because
  the release profile is not reproducible: `-Ofast -march=native`, and the version
  is embedded from git. The gate *results* stand — they were measured against the
  binaries whose hashes are recorded — but those two hashes are no longer
  verifiable.

## 12. Decisions

| hypothesis | decision | evidence |
|---|---|---|
| Census unions were read at a single cap | **LAND DEFAULT** | reproduces PR #125's table; unfiltered does not |
| Rank hashing is recomputed per lookup | **LAND DEFAULT** | 1.7–2.6× on interner lookups; +8 bytes per rank |
| Loss events rescan a region that did not grow | **LAND DEFAULT** | scans 136→117, node checks −28.5%, search identical |
| The witness needs a second scan to recover | **LAND DEFAULT** | pure refactor, every counter identical |
| Sparse ranks admit an arena-independent prefilter | **LAND DEFAULT** | exact; 187 comparisons skipped; +8 bytes per rank |
| `Post <= target` covering reduces search | **LAND DEFAULT-OFF ARM** | semantically distinct; four nodes on 3-AP games |
| The patch stack changes a shipped result | **LAND DEFAULT** | removes `infinite-race-u10`'s regression, 3/5 → 5/5 (§8) |
| Individual arms hide a better virtual solver | **STOP — NO MEASURED BENEFIT** on the panel | shipped four = best four; oracle +1; no unique arm |
| …on instances the shipped config cannot solve | **LAND DEFAULT-OFF ARM**, candidate | the 5-arm race nets +2 over the shipped four (§9); needs repetitions and a full-corpus campaign before any preset change |
| Learned per-instance arm selection (P5) | **STOP — NO MEASURED BENEFIT** | nothing uniquely reachable to select between |
| Learned per-instance *worker-count* selection | **OPEN — measured headroom** | oracle over the race configurations reaches 155 against 146 for the best fixed one (§9) |
| Dominance index / SIMD (P4) | **STOP — NO MEASURED BENEFIT** | two prior negatives; arms differ in cost, not reach |
| TAA lazy provider (R2) | **STOP — NO MEASURED BENEFIT** | lazy ≡ eager in verdicts; both ⊂ frozen graph |
| `--allowed-cpus` without cpuset | **LAND DEFAULT** (refusal) | systemd drops it silently; `CPUQuota` is enforced |

## 13. What to do next, in order

1. **Repeat `race5`'s three losses and five gains**, then run it over the full
   1,524 before proposing a preset. It nets +2 on this set (§9).
2. **Scope a worker-count selector.** The oracle over race configurations is 155
   against 146 fixed (§9). Unlike P5 this has measured headroom, and it needs a
   cheap per-instance predictor and an honest accounting of its own cost.
3. **A 60 s diagnostic re-run of the 20 hard-set unlocks**, to separate capability
   from cap-boundary; it does not rewrite the 17 s results.
4. If `race5` pays: a sweep preset adding `real:small:spot-guarded-sparse`, then
   the full-corpus gates before any `docker_default` change.
5. *Optional:* bisect the stacked revisions for which patch fixed §8.
6. Harness: a caught `std::bad_alloc` is recorded as ERROR with an empty
   `resource_reason`, not MEMOUT; `self-benchmark.sh` does not set
   `PKG_CONFIG_PATH` for a `/usr/local` Spot; `--conflict-policy stop` aborts on
   status-annotation mismatches as readily as on arm disagreements; and
   `regression-gate.sh` reports a build's unregistered sentinels as regression
   failures, which reads as a solver fault rather than a configuration one.

## 14. Not done, and why

- **P5 learned selector, as proposed.** The census shows nothing to select
  between on arm identity (§6), and the one genuine hard-set gain is a single
  arm that a fixed preset captures. The narrower question — how many workers per
  instance — is no longer a hypothesis: §9 measures 9 instances of headroom for
  it. Scoping that is item 2 of §13.
- **P4 index and SIMD.** Two prior negatives, and #154 removed the scan it
  targeted.
- **R1 interaction census.** Not started.
- **R2 FM-explorer probe.** Not built; the TAA provider's measured result (§6)
  weakens the case. `ltl_to_tgba_fm_otf` exists in the linked Spot if revisited.
- **The dense guarded twin**, `spot_guarded_forward_safety.hh`, shares every
  defect fixed here and is deliberately untouched: its users are `sweep`-role
  presets that failed admission.
