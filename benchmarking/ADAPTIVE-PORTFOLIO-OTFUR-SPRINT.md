# Adaptive arm portfolios and OTFUR work elimination

Living record. What the sparse guarded solver stopped doing, what it now
measures about itself, and what the handoff that proposed this sprint got wrong
about the tree it was written against.

**Status: the code work described here is landed and unit-gated. The corpus
campaign is not run.** Every number below comes from the fixed-seed unit sweeps,
on games of at most three atomic propositions at K <= 3. They establish
mechanism and equivalence, not corpus value. Do not quote them as coverage or
PAR-2 evidence, and do not change `docker_default` on them.

## 1. Frozen environment

| item | value |
|---|---|
| Acacia | `4f51b9b6`, branch `sprint/otfur-work-elimination`, based on `8678d53f` |
| Posets | `139e143` |
| Spot | 2.15.1.dev, `/usr/local/lib/pkgconfig/libspot.pc` |
| compiler | GCC 16.2.1 20260819 (Red Hat 16.2.1-2) |
| host | 11th Gen Core i7-11850H, 16 threads, 15 GB, zram swap active |
| verification build | `-Dacacia_spot_guarded_backend=true -Dacacia_spot_guarded_inequality_covering=true -Dacacia_forward_safety_solver=true -Dacacia_spot_lazy_provider=true` |
| that binary | SHA-256 `0a531373cf88db9f87b146e442ed3956e9261af355dbdd0be6619a3ba3e9ddce` |

The binary hash is recorded for reproduction only. It is not campaign
provenance: no campaign was run with it.

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
default cap of 80% of the machine. Any isolated-versus-race number measured
under that arrangement is suspect in exactly the direction the handoff reads it,
and its "concurrency can hurt individual arms" evidence — 46 isolated against 44
raced, on a 50-instance attribution cohort — is two instances wide to begin with.

**The existing arm census contains no guarded arm.** This is the finding that
decides whether P0 is worth running at all. `OTF-AND-SPOT.md` reports twelve
arms in isolation over the 180-instance panel, and concludes that four arms
reach the twelve-arm oracle ceiling at 137/180 with only two answers unique to
any single arm. Read alone, that kills the portfolio hypothesis. But its
`B`/`S`/`F` are *backward*, *backward-with-local-certificates* and *forward*
(legend, `OTF-AND-SPOT.md:1036-1038`) — not the sparse guarded backend. The
guarded unreal arm, which is where the whole +66 of the last sprint came from,
was never in that census. Its redundancy conclusion therefore does not transfer,
and P0 has real information to produce — as an extension of that census, not a
restart.

**P4 was already attempted twice.** O3 batched invalidation was reverted after
measuring maximum batch sizes of 2, 5, 2, 5 and 1. O5 indexed the antichain's
generator list, measured no change, and was discarded — the list peaks at 177
entries. Neither should be re-run. The structure that would address the
22,261-to-1 scan ratio O3 measured is an index over the *visited node* set, and
the repo had already identified that and deferred it.

## 3. An arm census was being read at the wrong cap

`select-portfolio-arms.py` ranked subsets by "the union of decisive answers at
cap 17s", its own words, while never reading `smallest_cap_solved`.
`run-syntcomp26-coverage.py` fills `decisive_result` from the *earliest* staged
cap that decided an instance, so a campaign run with `--caps 1,5,17,60` files its
60-second answers in the same column as its 17-second ones.

The files that trip it are in the tree: `_coverage26/B-runs-summary.tsv` and
`S-runs-summary.tsv` hold 37 and 35 instances first decided at 60 seconds.

Selecting over `_coverage26` at `--cap 17` reproduces the coverage PR #125
published for those same three configurations. Without the filter it does not:

| configuration | published (17 s) | `--cap 17` | unfiltered |
|---|---:|---:|---:|
| B | 1056 | **1056** | 1093 |
| S | 1065 | **1065** | 1100 |
| F | 1053 | **1053** | 1053 |
| B ∪ S | 1065 | **1065** | 1100 |
| B ∪ S ∪ F | 1090 | **1090** | 1113 |

It also reordered the ranking, so this was not only an inflated total: unfiltered,
`B+F26` ties for first at 1113; at 17 seconds `F26+S` wins 1090 to 1089.

`--cap` now defaults to 17, a decisive row with no `smallest_cap_solved` is
fatal rather than silently counted, and the saved table carries `cap_s`.
`compare-backend-race.py` already filtered this way; the selector was the
outlier. The summary's `still_unsolved_at_60` and `failure_kind_at_60` were
computed at `max(--caps)` and misnamed for every campaign not ending at 60; they
are now `*_at_max_cap` beside a `max_cap_s` that says which cap that was.

## 4. Losing-region work, measured

All figures from the `spot-provider-replay` sweep: 8 formulas x every
input/output partition x K in {1,2,3}, 72 games, each cross-checked against the
explicit forward game.

| revision | loss events | insertions | broad scans | node checks | subsumption queries | prefilter skips |
|---|---:|---:|---:|---:|---:|---:|
| before | 136 | 117 | 136 | 1,059 | 1,471 | — |
| single-pass subsumer | 136 | 117 | 136 | 1,059 | 1,471 | — |
| growth-gated scan | 136 | 117 | **117** | **757** | **975** | — |
| mass prefilter | 136 | 117 | 117 | 757 | 975 | **187** |

19 of 136 loss events add no generator. Their rank already lies inside the
upward-closed losing region, so the pass they used to trigger searched a region
the earlier passes had already searched. Gating the scan on the insert that
decides whether the region grew removes exactly those, and testing only the new
generator rather than all of them cuts node checks 28.5% and subsumption queries
33.7%.

The search itself is unchanged, which is the point: expansions (529), choices
(329), nodes (229), invalidations (19) and removals (43) are identical across
all four revisions.

`reopened_sources` is the one counter that moves without the search moving —
127 to 99 — and it needs reading rather than comparing. It counts reopen
*attempts*, and an attempt on an already-queued node does nothing. Marking a
cascade at enqueue time means 28 of those attempts now find the node already
losing. `reopen_enqueues` was added to make the pair legible and is 55 either
way, which is why the expansion count did not move.

The mass filter is exact, so it may not move anything but its own counter, and
does not. Its lemma — `l <= r` forces both a no-larger support and a no-larger
shifted mass `M(r) = sum over supp(r) of (r(q)+1)`, and equal mass under `<=`
leaves only equality — is checked on all 4,096 pairs of the exhaustive order
test. `M` is a sum over stored entries alone, which is what makes it work where
the dense coordinate sum could not: no dependence on how much of the automaton
has been discovered.

## 5. Guard covering, measured

`acacia_spot_guarded_inequality_covering`, default **off**.

| mode | expansions | choices | nodes | node checks | reopen enqueues | region inputs verified |
|---|---:|---:|---:|---:|---:|---:|
| exact (default) | 529 | 329 | 229 | 757 | 55 | 438 |
| `<=` covering | **525** | **325** | **227** | **751** | 59 | 438 |

The direction is the one the mechanism predicts — fewer choices, fewer nodes —
and `reopen_enqueues` moves the other way, which is the expected cost: a wider
region hands back more input space when its target is deactivated. The margins
are four nodes on three-AP games. They say the mechanism works, and nothing
about whether it pays.

Two things are worth recording about how this was checked, because the first
version of the check was wrong.

The `<= target` predicate was **already in the file**: `Oracle::preimage`'s
downward kind, which the inductive invariant check already aggregates. The
handoff's §3.4 proposes deriving a new BDD construction for it. That would have
been redundant, and its own instruction to re-derive from the current rank-update
code is what caught it.

The differential test first checked only *active* choices, and passed in both
modes — discriminating nothing. The two modes diverge in choices that are created
and later deactivated. Checking every claimed region, active or not, against
`rows::evaluate_sparse` — an arithmetic oracle the solver does not use — makes
the test real: forcing the exact-covering assertion on under `<=` covering now
fails, which is the evidence that the flag does something semantic rather than
only reordering work.

The verifier gets the matching swap and rebuilds the predicate from its own
fresh `Reader` and `Oracle`, so a choice claiming too much input space still
fails there. Gating the verifier too is what makes "flag off changes nothing"
checkable; it is confirmed byte-identical on every counter above.

`propagate_losses` stays conservative and untouched. Under `<=` a concrete
successor strictly below a losing target need not be losing, so deactivating the
choice and reopening the source, rather than propagating the target's loss
backwards, stops being merely correct and becomes load-bearing.

## 6. Correctness

- `meson test --suite unit`: **42 tests, Fail: 0**, both flag states built from
  the same sources in every build.
- The 5,000-game fixed-seed harness agrees four ways — sparse `Search`, the
  dense guarded solver, the explicit forward game, and the backward antichain —
  under exact and `<=` covering alike.
- Config frontends agree on all 44 shared scalar options. Note the registry has
  **four** frontends, not the three CLAUDE.md names: `config/acacia-options.json`,
  `meson.options`, the `#ifndef` block in `src/config/acacia_build_config.hh.in`,
  and the `build_conf.set` line in `meson.build`. Omitting the last leaves
  `@ACACIA_…@` unsubstituted. `tests/check-config-frontends.py` compares only the
  first two; `tests/pytest/test_acacia_config.py` pins the `.hh.in` block, and it
  runs in `python-test.yml` rather than `--suite unit`.
- Six unrealizable formulas decided identically by the `<=`-covering guarded arm
  and the independent backward solver.
- G1/G3 are **not run**. They need paired binaries and corpus time.

## 7. Decisions

| hypothesis | decision | evidence |
|---|---|---|
| Census unions were read at a single cap | **LAND DEFAULT** | filtered selection reproduces PR #125's published table; unfiltered does not, and reorders the ranking |
| Rank hashing is recomputed per lookup | **LAND DEFAULT** | 1.7x-2.6x on interner lookups by support size; +8 bytes per rank |
| Loss events rescan a region that did not grow | **LAND DEFAULT** | 19 of 136 events redundant; scans 136 to 117, node checks -28.5%, search identical |
| The witness needs a second scan to recover | **LAND DEFAULT** | pure refactor, all six counters identical across revisions |
| Sparse ranks admit an arena-independent prefilter | **LAND DEFAULT-OFF risk** | exact, 187 comparisons skipped, +8 bytes per rank; the memory trade is a corpus question |
| `Post <= target` covering reduces search | **LAND DEFAULT-OFF ARM** | mechanism confirmed and semantically distinct; margins are four nodes on 3-AP games |
| Individual arms hide a better virtual solver | **OPEN** | the only census that exists has no guarded arm in it |
| Learned arm selection | **STOP — GATED** | not started; requires the census above to show headroom first |
| Dominance index / SIMD | **STOP — NO MEASURED BENEFIT** | O3 reverted, O5 discarded; the real target is the visited-node set, and the scan may no longer be hot |

## 8. What to do next, in order

1. **The census.** 16 runtime arms (4 polarity/transform x 4 backends) in one
   binary — `game_backend` is a runtime enum now, so the prior census's "cannot
   be built today" limitation is gone. Panel first
   (`syntcomp26/panel.list`, 180 instances): the full set is ~5 h per arm, and
   the panel is the precedent and its stated reason. Two parser constraints:
   automaton-unreal requires `frozen-graph`, and a non-frozen provider requires
   backend `spot-guarded` exactly, not `spot-guarded-sparse`.
2. **G1 and G3 for the landed patches**, `--baseline-bin` being the same
   configuration built from the previous revision, per patch. Read PAR-2 against
   the 21.1 s / 12.1 s noise floors.
3. **The `<=` covering A/B**, `otf_sparse_formula_inequality_covering` against
   flag-off `otf_sparse_formula` at the same revision.
4. **Race regime work is blocked.** `benchlib.run_systemd_scope` sets only
   `MemoryMax` and `MemorySwapMax`. There is no CPU quota or cpuset, so the
   handoff's fixed-budget regime is not measurable today, and a 1-vs-8-worker
   comparison without it is a scaled-resource experiment wearing the wrong label.
   Add `AllowedCPUs`/`CPUQuota` passthrough before running any race comparison.

## 9. Not done, and why

- **P5 learned selector.** Gated on §8.1. Building it before the census would be
  building it against a redundancy result measured without the arms in question.
- **P4 index and SIMD.** Two prior negative results, and P1 may have removed the
  hot path it targets. Revisit only if a profile still shows one.
- **R1 composition, R2 lazy translation.** No prototype. `ltl_to_tgba_fm_otf`
  does exist in the linked Spot (`/usr/local/include/spot/twaalgos/ltl2tgba_fm.hh:164`),
  so R2's probe is feasible; its acceptance-inventory obligation is a design
  question to write down before any code.
- **The dense guarded twin.** `spot_guarded_forward_safety.hh` is a near
  line-for-line clone and shares every defect fixed here. It is deliberately
  untouched: its only users are `sweep`-role presets that failed admission, and
  changing it would perturb frozen measurements for no shipped arm.
