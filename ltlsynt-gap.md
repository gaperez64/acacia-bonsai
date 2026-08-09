# Closing the `ltlsynt` gap

This roadmap separates the next performance workstream from the symmetry
paper. Its scope is solver internals, automaton structure, and the front end;
post-synthesis optimization is deliberately excluded.

## The shape of the problem

The original common ranking campaign used 994 `syntcomp24/0s-1s` instances
and a 17-second cap, but its harness accepted exit code 2 on unlabelled
benchmarks. K-bound give-ups, cgroup OOMs mapped to UNKNOWN, and child
exceptions were therefore reported as solved. Requiring an actual printed
verdict changes the historical table unevenly:

| Configuration | Reported solved | Verdict required | Non-answers scored OK |
|---|---:|---:|---:|
| `ltlsynt` | 913 | **896** | 17 |
| `best_decomp_mona` | 823 | **808** | 15 |
| `best_decomp_mona_any` | 825 | 809 | 16 |
| `best_decomp_sharingtrie_mona` | 825 | **793** | 32 |
| `best_decomp_mona_elevator` | 813 | **786** | 27 |
| `base_iosprecom_mona` | 810 | 782 | 28 |

Every solved count from the old `_bm-logs-*` campaigns is inflated and is not
directly comparable to results below. Old tables retained later in this file
are historical ablation evidence only; they use the former scoring rule
unless explicitly marked strict.

The corrected 994-instance data also closes the knob-recombination question.
The per-instance oracle over all 15 Acacia configurations answers 814 files,
only **six** beyond shipping's 808; greedy marginal coverage is +3, +1, +1,
+0 after the best single configuration. Of the 119 instances answered by
`ltlsynt` but not shipping Acacia, 111 are timeouts. `ltlsynt`'s median on
those 111 is 0.04 seconds and 83 finish below 0.2 seconds. The families split
37 `ltl2dba*`, 48 arbiter-shaped (`mux`, `amba_decomposed_lock`, or
`*arbiter*`), and 26 other. This is a structural stall, not a constant-factor
gap. The other eight historical losses were instant UNKNOWNs and are now
answered in essentially zero time by the shipped syntactic bypass.

## The strict 2024 reset

Benchmarking now uses Spot 2.15.1.dev built with
`SPOT_MAX_ACCSETS=64`, matching CI. At 32 acceptance sets, 22 Acacia cases and
17 `ltlsynt` cases threw, including `06`--`13` and several `ltl2dba_R*`
files. Campaign metadata records both the Spot version and limit. With the
limit raised, `ltl2dba_R14` and `R16` no longer throw; `06`--`13` also run
without acceptance-set exceptions.

The strict sequential rebaseline used an 8 GiB per-solver cgroup, no swap,
and a 17-second cap over the first stratified `syntcomp24` panel plus the
independent 94-instance `syntcomp21/crit` gate:

| Configuration | Solved | Timeout | UNKNOWN | Error | PAR-2 (s) |
|---|---:|---:|---:|---:|---:|
| `ltlsynt` | **223/274** | 51 | 0 | 0 | **1955.8** |
| `best_decomp_mona_small` | 207/274 | 67 | 0 | 0 | 2650.7 |
| `best_decomp_mona` (`Small+Any` race) | 205/274 | 68 | 1 | 0 | 2737.9 |

The strata show why one aggregate number is inadequate. Each cell is
`solved / timeout / unknown / error; PAR-2 seconds`:

| Stratum | N | `Small+Any` | `Small` | `ltlsynt` |
|---|---:|---:|---:|---:|
| independent `syntcomp21/crit` | 94 | 89/5/0/0; 268.5 | 90/4/0/0; 237.1 | 86/8/0/0; 342.0 |
| easy | 40 | 40/0/0/0; 5.2 | 40/0/0/0; 4.7 | 38/2/0/0; 75.6 |
| border | 65 | 61/4/0/0; 423.7 | 62/3/0/0; 368.4 | 41/24/0/0; 893.6 |
| gap | 60 | 15/44/1/0; 1530.5 | 15/45/0/0; 1530.5 | 58/2/0/0; 134.6 |
| open | 15 | 0/15/0/0; 510.0 | 0/15/0/0; 510.0 | 0/15/0/0; 510.0 |

The sole fast UNKNOWN is `arbiter_with_buffer7.ltl` at 16.15 seconds. It is
charged as a non-answer, exactly like a timeout, but remains visible in its
own column.

## The stratified panel

The 994-file `0s-1s` suite had 743 easy cases and only 184 files whose status
could change. It has been retired from `syntcomp24/all.list`; the manifest is
kept only for historical reproduction. A deterministic, family-balanced
generator now samples four verdict-aware strata and writes provenance beside
the list.

The initial panel, cut from the July and April historical references, covered
1011 of 1195 corpus files and excluded 184 unmeasured files. Its composition
was 40/743 easy, all 65/65 border, 60/119 gap, and 15/67 open: 180 files, 125
of which could change status. Regenerating from only that panel's 180 measured
cases then exposed a closure defect: reference coverage was intersected, so no
case outside the old panel could re-enter. The resulting 153-entry panel is
retained in git history (`ca64d35a`) as the failing intermediate state.

The repair has two parts. Multiple references are now unioned, with the first
(newest) campaign authoritative on overlaps and its directory retained as
`source_campaign` provenance. More importantly, a fresh sequential reference
now covers all 1195 `syntcomp24` cases. The four historical timing buckets
contained only 1085 distinct files; an audited, disjoint 110-file supplement
closes that previously unnoticed corpus hole. Both tools have exactly 1195
unique rows and zero rows outside the corpus:

| Configuration | Solved | Timeout | UNKNOWN | Error | PAR-2 (s) |
|---|---:|---:|---:|---:|---:|
| `ltlsynt` | **896/1195** | 297 | 2 | 0 | **10438.2** |
| Acacia `Small` | 864/1195 | 329 | 2 | 0 | 11606.2 |

There are 830 common answers, 66 `ltlsynt`-only answers, 34 Acacia-only
answers, and 265 cases answered by neither. The 110-file supplement itself is
almost entirely open: Acacia and `ltlsynt` both answer only `mux150` and
`workstation_resupply_2`; Acacia times out on the other 108, while `ltlsynt`
times out on 107 and returns one strict UNKNOWN (`generalized_buffer5`).

The complete-reference selection is:

| Stratum | Observed pool | Selected | Real / Unreal / Unknown | Largest family |
|---|---:|---:|---:|---:|
| easy | 805 | 40 | 22 / 18 / 0 | 1 |
| border | 59 | 59 | 30 / 29 / 0 | 7 |
| gap | 66 | 60 | 50 / 10 / 0 | 4 |
| open | 265 | 15 | 0 / 0 / 15 | 1 |
| total | 1195 | **174** | | |

The full reference meets the 60-case gap quota, but it also falsifies the
expectation that 65 border cases exist: Small Acacia answers only 59 cases in
the 1--17 second interval. The generator therefore takes the complete border
pool and produces 174 entries rather than duplicating or misclassifying six
cases. Two independent regenerations are byte-identical, with
`covered=eligible=1195` and `uncovered=0`. The selection is fitted to today's
Acacia timings. Two guards limit that bias: gap/open membership is defined
using `ltlsynt`, and the independently selected `syntcomp21/crit` suite
remains in every campaign. The 2025 panel below is the cross-corpus
overfitting check.

## The 2025 cross-check

SyFCo converted 1579 of the 1586 files in `selection-ltl-2025`. The seven
skips are exactly `finding_nemo_pb_1_pe_` through `_7_pe_`, all rejected by
the `ltlxba` printer because they use strong next. Ten deterministically
sampled conversions were reproduced byte-for-byte, including their partition
files.

The strict 17-second reference ran all 1579 converted files sequentially in
8 GiB/no-swap solver scopes. The Acacia binary in this completed campaign was
the then-default `Small+Any` race; the landing result below means it is a
reference build, not the final shipping default.

| Configuration | Solved | Timeout | UNKNOWN | Error | PAR-2 (s) |
|---|---:|---:|---:|---:|---:|
| `ltlsynt` | **1257/1579** | 317 | 2 | 3 | **11410.9** |
| Acacia `Small+Any` reference | 1022/1579 | 553 | 4 | 0 | 19748.7 |

The paired result has 863 comparable both-solved files, 266 `ltlsynt`-only,
31 Acacia-only, 128 both-solved files where Acacia is more than twice as slow,
and 291 answered by neither. Of the 266 `ltlsynt`-only files, 87 are
realizable and 179 unrealizable; `ltlsynt`'s median is 0.0855 seconds and 194
finish below 0.2 seconds. The structural-stall diagnosis therefore reproduces
on a new corpus rather than being an artefact of the 2024 selection.

The generated 2025 panel covers the complete converted corpus. Its routine
quota is now aligned with 2024: 180 files, comprising 40/884 easy, 65/138
border, 60/266 gap, and 15/291 open. Its real/unreal split is 21/19 for easy,
18/47 for border, and 20/40 for gap; all 15 open entries have unknown verdict.
A second generation was byte-identical.

### Degenerate I/O alphabets

The complete reference exposes a small independent front-end gap. Sixty-one
2025 instances have no output propositions: the environment chooses the
entire word, so realizability is exactly emptiness of the negated formula's
language. `ltlsynt` answers all 61; Acacia answers 50 and times out on 11.
Fifty-nine are unrealizable and two realizable. Another one-sided instance,
`EscalatorNonReactive.ltl`, has no inputs and is already answered by both
tools. The corresponding 2024 audit has 16 empty-output and two empty-input
instances, with one overlap (17 unique); both tools answer all of them.

The decision fast path therefore handles empty outputs by translating the
negation and checking emptiness, and empty inputs by checking ordinary
language non-emptiness, after realizability simplification and before the
unreal workers swap I/O. Strategy-producing no-input requests keep the
existing lasso-to-AIG path. The CLI now distinguishes an explicitly empty
`-o ''` from an omitted `-o`, matching the existing empty-`-i` support; an
end-to-end unit test protects both cases.

The strict 17-second corpus gate completed all one-sided instances under
8 GiB/no-swap solver scopes:

| Corpus | Cases | Verdicts | Solved | Prior losses recovered | Total time |
|---|---:|---:|---:|---:|---:|
| 2025 | 62 | 3 real / 59 unreal | **62/62** | **11** | 1.0 s |
| 2024 | 17 | 3 real / 14 unreal | **17/17** | 0 | 0.2 s |

Every verdict agrees with the full-corpus reference. The eleven gains are
exactly `gf-unreal37` through `gf-unreal47`, which previously timed out; no
previously answered instance regressed. The decision fast path therefore
clears the landing bar and ships.

## Tier 0 — translation race at the final landing gate

The translation race runs both Spot `Small` and `Any` realizability children,
alongside the two unrealizability checks. The earlier 97-file gate scored
91/97 against `Small`'s 90/97, gaining `amba_decomposed_lock13` and losing
nothing; an isolated run also gained `amba_decomposed_lock14`. That evidence
motivated making `small+any` the default and temporarily naming the Small
build as the ablation.

The strict 274-file reset did not clear the unchanged per-instance landing
bar. `Small` answered `syntcomp21/collector_v215.ltl` in 16.09 seconds and
`syntcomp24/05.ltl` in 13.85 seconds; the race reached the 17-second cap on
both and gained no case in that run. Seven isolated repetitions in alternating
configuration order resolved whether these were boundary flakes:

| Instance | `Small+Any` | `Small` |
|---|---:|---:|
| `collector_v215.ltl` | **0/7** | **7/7**, median 15.84 s |
| `05.ltl` | 7/7, median 15.36 s | 7/7, median 13.36 s |

`05.ltl` was run noise; `collector_v215.ltl` is a repeatable lost answer.
The race therefore fails the no-regression bar and is **not** the shipping
default. `acacia_translation_pref` remains `small` and
`best_decomp_mona_race` keeps `small+any` available explicitly. The redundant
Small alias has been removed; the old campaign artifacts retain their
historical filenames as measurement provenance.

Two nearby downset variants are also positive despite carrying avoidable
work. `rank_bucketed` and `filtered_vector` each add one solved instance and
save about 49 PAR-2 seconds.

In `rank_bucketed_vector_backed`, the `buckets` vector is write-only dead
state: `rebuild_buckets()` refills it on every successful insert
(`rank_bucketed_vector_backed.hh:48-58`, called at `:129`) and nothing ever
reads it — the queries go through `ranks` via `lower_rank_pos` and
`upper_rank_pos`. That is a linear pass per insert for nothing, so inside a
meet cloud it costs O(cloud x n) of pure overhead. The neighbouring
`vector_set.insert (begin + pos, ...)` is the more expensive half and needs an
actual redesign rather than a deletion.

In `filtered_vector_backed`, the threshold bitsets are built but consulted
only in `contains` (`:177-190`); `insert` (`:215`), `union_with` (`:243`,
which merely sets `filter_dirty`) and `intersect_with` (`:249`) are
byte-identical to `vector_backed`. Using the filter in a meet cloud requires
freezing it over `other`, which is `const` for the whole call, rather than
over the accumulator, whose every insert dirties it and triggers an
O(k n log n) rebuild.

Both configurations therefore win *despite* the waste, which is the argument
for cleaning them up and re-ablating — not for a blind default flip.

## Tier 1 — shipped: syntactic bypass

The largest measured structural lever is `ltlsynt`'s pre-game bypass:
`ltlsynt_no_bypass` solves 892 instances versus 913 for `ltlsynt`, a
21-instance contribution on this exact corpus.

Spot's `try_create_direct_strategy`
(`subprojects/spot/spot/twaalgos/synthesis.cc:1223`) recognizes cheap
syntactic cases before constructing a game, including `G(bool)`, the
`GF`/`FG` dual cases, and syntactic obligations. Acacia now has a
decision-only front-end path behind `acacia_enable_syntactic_bypass`. It calls
Spot's direct check after the realizability simplifier and before the
unreal-child I/O swap, then maps the formula verdict to each forked child's
role. Strategy construction is deliberately excluded. Unit coverage exercises
realizable and unrealizable `G(bool)`, the `GF`/`FG` dual patterns, and
unsupported fallback.

The companion ablation identifies what not to copy:
`ltlsynt_no_obligation` ranks first at 918 solved. Full obligation synthesis
costs `ltlsynt` five instances here. Acacia therefore implements the cheap
`G(bool)`, `GF`, and `FG` special cases and defers the obligation machinery.

### Tier 1 campaign result (2026-08-03)

The four-way campaign compared baseline, syntactic bypass, action-storage
deduplication, and both changes together. Every build passed all 11 unit
tests. Coverage was identical in every configuration: 17/24 on
`symmetry-2025` and 89/94 on `syntcomp21/crit`, with the same five timeouts on
the latter. At a 17-second PAR-2 cap:

| Configuration | `symmetry-2025` T_OK / PAR-2 | `syntcomp21/crit` T_OK / PAR-2 |
|---|---:|---:|
| baseline | 16.5 / 254.5 | 102.3 / 272.3 |
| syntactic bypass | 16.7 / 254.7 | 88.0 / 258.0 |
| action deduplication | 17.0 / 255.0 | 101.3 / 271.3 |
| both | 16.8 / 254.8 | 82.8 / 252.8 |

The bypass collapses seven intended cheap cases from 18.61 seconds in total
to 0.20 seconds: `mux16`, `detector14`, `ltl2dba_C214`, `ltl2dba_U18`,
`ltl2dba_Q5`, `ltl2dba_beta4`, and `ltl2dba_U17`. These are exactly
`G(bool)` and `GF`/`FG` dual shapes. Spot's separate MTBDD obligation
synthesis is not invoked by this path. The net suite gain is smaller because
of single-run noise on unrelated cases (most visibly `prioritized_arbiter6`),
but the combined build does not reproduce that slowdown. With no lost
instance and a structural 18.4-second saving on recognized cases, Tier 1
clears the adoption bar.

The forced-rebuild follow-up on 2026-08-04 confirmed the result. Again, every
configuration solved 17/24 focused cases and 89/94 crit cases with identical
timeout sets. Baseline versus bypass was 16.9 versus 16.5 T_OK seconds on the
focused panel and 103.4 versus 83.3 on `syntcomp21/crit`. Eight direct cases
fell from 19.14 seconds to 0.24 seconds in total: the seven listed above plus
`ltl2dba_E6`. The bypass is now enabled by default; the campaign-only presets
were removed.

A component-local follow-up tested the remaining difference from `ltlsynt`,
which retries the same direct check after formula decomposition. It added no
coverage: both variants solved 17/24 focused cases and 89/94 crit cases with
identical timeout sets. Focused timing was exactly tied at 16.9 T_OK seconds;
crit timing was 89.2 seconds for whole-formula-only and 85.4 for the component
variant. A structural scan established that component checks actually fired
only on six already-fast files (`Gamelogic`, two `KitchenTimer` sizes,
`SensorInit`, `test2_f2774e0b`, and focused `KitchenTimerV7`). Their paired
changes were all between -0.008 and +0.007 seconds, so the 3.8-second aggregate
difference came from unrelated cases and is run noise. No timeout contained a
usable component hit. The temporary option and hook were removed.

The larger 2025 follow-up reached the same conclusion. An exact structural
scan covered all 1,586 TLSF files in `selection-ltl-2025`; only 16 contained a
decomposed component on which the extra check could change execution (seven
additional files could not be translated by SyFCo's `ltlxba` printer because
they use strong next). A paired 17-second, 8 GiB campaign therefore ran both
release binaries on all 16 relevant candidates, alternating run order. Both
variants solved 15/16 with identical verdicts and both timed out on
`LedMatrix.tlsf`; there was no coverage gain or regression. This rules out the
component-local extension for the current benchmark distribution, so only the
whole-formula Tier 1 bypass remains.

Acacia does have one bypass. `spot_nba_fastpath` routes a deterministic
universal co-Büchi automaton through Spot's `split_2step` and game solver.
Diagnostics report `fast_class = gfg-disabled` on essentially every hard
instance, so this path rarely covers the loss set.

## Tier 2 — translator hygiene complete; knob matrix deferred

The duplicated Spot option construction in the CLI and Python paths now goes
through `src/solver/translator_options.hh`. Validation runs immediately after
constructing each `spot::translator`, when Spot has consumed the options, so
an unused or misspelled name throws visibly instead of silently benchmarking
a no-op. A unit test covers both the configured option set and a deliberately
misspelled `simlu` option.

The separate `simul`, `ba-simul`, `det-simul`, and translator-level matrix was
not added. It would introduce several low/medium/high-style configuration
surfaces without a measured reason to expect a clear gain. Reopen it only if
translation diagnostics identify a concrete dominant instance set; `07.ltl`
remains the motivating example, with 6700 ms of translation and a
13,609-state, 155,315-edge automaton.

## Tier 3 — shallow letter-loop attempts closed

The hard path is often action enumeration rather than antichain storage:

- `amba_decomposed_lock14` spends 10,406 ms solving an automaton with only
  five states, eight edges, four loops, and `max_f = 3`. The cost is I/O
  letter enumeration.

- `mux16` takes the deterministic fast path, yet `fast_solve_ms = 7308`;
  Spot's own `split_2step` blows up on the letter alphabet.
  `spot::partitioned_relabel_here` exists in the vendored synthesis
  implementation (`subprojects/spot/spot/twaalgos/synthesis.cc:1946-2019`)
  and is not used by Acacia's path.

- `src/actioners/standard.hh:20-36` stores actions through nested vectors,
  lists, and pairs, then traverses them once per output letter per CPre step.
  Its `TODO` at lines 134-136 explicitly notes that two representations of
  the same transition information are retained.

The campaign tested the two shallow interventions available without changing
the underlying game construction: public partitioned relabelling and a more
contiguous action-storage path. The findings below close both variants.

### Tier 3 findings (2026-08-03/04)

The first action-storage slice removed the full copy returned by
`actioner.actions()`. It preserved every verdict and timeout, but by itself
saved only 0.9 seconds over the 89 solved `syntcomp21/crit` cases and lost 0.5
seconds on the focused panel: useful evidence for memory ownership, not a
standalone speed claim.

The forced-rebuild follow-up added contiguous input/output-action sequences:

| Configuration | `symmetry-2025` T_OK / PAR-2 | `syntcomp21/crit` T_OK / PAR-2 |
|---|---:|---:|
| baseline | 16.9 / 254.9 | 103.4 / 273.4 |
| syntactic bypass | 16.5 / 254.5 | 83.3 / 253.3 |
| action storage | 16.3 / 254.3 | 102.8 / 272.8 |
| both | 16.6 / 254.6 | 83.1 / 253.1 |

Every configuration again solved 17/24 and 89/94 with the same seven and five
timeouts respectively. Action storage saved only 0.6 seconds standalone on
each suite and 0.2 seconds when combined with Tier 1. Paired deltas were
mixed: the largest crit improvement was 0.79 seconds on `collector_v39`,
while the largest regression was 0.30 seconds on `amba_case_study2`. This is
noise-scale evidence, not a letter-loop improvement, so the option, presets,
diagnostics, and representation changes were removed.

The public partitioned-relabel helper is not viable at the hook suggested
above. `partitioned_game_relabel_here()` accepts an arena only *after*
`split_2step`, while `mux16` pays the alphabet explosion inside that split.
Applying it afterwards cannot reduce the measured bottleneck. A unit prototype
also showed that undoing the relabel after game solving leaves a fresh BDD
variable registered. The variant was removed before benchmarking. Any future
attempt must change or replace the partition construction inside
`split_2step`; simply wrapping its output is both too late and unsafe.

## Phase partition of the current 2024 gap

The regenerated 2024 panel contains 44 gap targets. Each was run for 120
seconds through the diagnostics build under the same 8 GiB/no-swap policy.
The refined pass adds a checkpoint immediately after action and input-picker
construction: a child stopped at `before-solve` is action-construction-bound,
while one that reaches `after-action-construction` has entered the fixed
point. Targets are classified by the modal phase across the four race
children, breaking equal splits toward the deeper phase.

| Dominant phase | Targets | Characteristic families |
|---|---:|---|
| translation | 7 | `Automata16S/32S`, `LedMatrix`, four large unreal arbiter encodings |
| action construction | 4 | two `FelixSpecFixed`, `amba_decomposed_lock15/16` |
| fixed point | **33** | lift, finding-nemo, theta, and most arbiter families |

The original expectation that Tier 4 would fall below translation and letter
construction is falsified: 33/44 targets reach action construction and then
stall in the fixed-point phase, often in their first loop. Only four are
predominantly stopped before the first loop. The +6 option-oracle ceiling
still limits the likely coverage gain, but the phase evidence makes
fixed-point work the first experiment rather than a guessed constant-factor
detour.

The timer split reran those 33 candidates from a clean diagnostics build at
120 seconds per target. The finer checkpoints reclassified three as action
construction and one as translation; the remaining 29 fixed-point targets
split as follows:

| Fixed-point sub-bucket | Targets | Aggregate CPre / apply / downset |
|---|---:|---:|
| downset-bound | **16** | 975.5 / 172.3 / **801.6 s** |
| letter-loop-bound | 10 | 285.0 / **187.6** / 96.0 s |
| mixed (within 20%) | 3 | 30.7 / 15.5 / 14.7 s |

Across the table, `apply_ms + downset_ms` is 1287.7 seconds against 1291.2
seconds of measured CPre time (0.3% residual); for every target with at least
10 ms of CPre, the largest per-target residual is 5.1%. The split is therefore
accounting for the intended loop rather than classifying timer gaps. Downset
work is both the plurality and the dominant aggregate cost, selecting the
Tier 4 meet-cloud prune below. The clean campaign was bounded by a 2 GiB
controller cgroup and separate 8 GiB/no-swap solver cgroups; it completed all
33 targets without an OOM.

## The 2025 gap phase cross-check

The regenerated 2025 panel contributes 60 pre-fast-path gap targets. The same
diagnostics build ran each for 120 seconds, sequentially, with a 2 GiB
controller and a separate 8 GiB/no-swap solver scope. All 60 produced a phase
summary: 42 reached the cap and 18 returned before it, with no OOM or missing
row. Applying the refined checkpoints from the outset gives this
cross-corpus comparison (the 2024 row includes the four reclassifications from
its timer pass):

| Corpus | Targets | Translation | Action construction | Fixed point | Fixed point: letter / downset / mixed |
|---|---:|---:|---:|---:|---:|
| 2024 gap | 44 | 8 | 7 | **29** | 10 / **16** / 3 |
| 2025 gap | 60 | **19** | 9 | **32** | **17** / 11 / 4 |

The 2025 fixed-point accounting is again tight:

| Fixed-point sub-bucket | Targets | Aggregate CPre / apply / downset |
|---|---:|---:|
| letter-loop-bound | **17** | 1038.9 / **796.0** / 241.7 s |
| downset-bound | 11 | 1057.2 / 345.8 / **709.2 s** |
| mixed (within 20%) | 4 | 314.8 / 148.3 / 166.5 s |

In aggregate, apply plus downset accounts for 2407.4 of 2410.9 measured CPre
seconds, a 0.14% residual. For every target with at least 10 ms of CPre, the
largest per-target residual is 4.2%. (`picker_ms` is a nested view of work in
the input-picker call and is not additive with apply and downset.) The timer
partition is therefore sound on this corpus too.

The family shape differs materially from 2024. Translation contains the
large simple-arbiter unreal sizes, robot-cat/repair/running, `follow`, and
`chomp`; action construction contains robot-to-target charging, prioritized
and hinted arbiters, the decomposed lock, and infinite race. Within the fixed
point, contradiction/theta, patrolling, robot-resource, thermostat, and
workstation cases are letter-loop-heavy, while `LedMatrix`, lift, heim,
prioritized-arbiter, and robot-to-target cases are downset-heavy. Two of the
19 translation rows, `gf-unreal37` and `gf-unreal46`, are no longer losses:
the degenerate-I/O path answers them in 12 and 14 ms, and they appear in that
bucket only because translation is the fast path's substantive operation.

This cross-check does **not** reproduce 2024's downset dominance. Fixed-point
work remains the largest single phase, but translation plus action
construction accounts for 28/60 targets, and letter application is the
largest fixed-point sub-bucket. The 2024 evidence was sufficient to select
the Tier 4 experiment, but it is not a corpus-independent reason to keep
prioritizing downsets after that experiment failed. Translation diagnosis and
the deep letter-loop path therefore move ahead of further downset work in the
re-ranked menu below.

## Tier 4 — solver internals, with a coverage ceiling

The corrected per-instance oracle over all 15 Acacia configurations rescues
only six cases beyond shipping on the historical 994-file corpus. This tier
is therefore primarily PAR-2 and constant-factor work, not a route to broad
coverage. That remains worthwhile on the arbiter family, where a constant
factor can buy several client sizes, but the expected payoff must be stated
honestly and checked against the phase partition before implementation.

### Prune meet-cloud generation in `bboxtree_backed`

Every top-four backend shares one shape in `intersect_with`: for each
surviving `x`, compute `x.meet(y)` for **every** `y in other`, then reduce the
resulting cloud. They differ only in how the cloud is filtered *after*
generation. Nothing prunes generation. This is the only design on the table
that does, and it never landed — the posets submodule took the kd-tree
correctness fixes and nothing else.

The rule. `bboxtree` nodes carry an upper corner `join_c` per subtree
(`utils/bboxtree.hh:15-35`). For any subtree `S` and any `y in S`,

    x /\ y  <=  x /\ join_c(S),

because `min(x_i, y_i) <= min(x_i, max_{z in S} z_i)` coordinatewise. So if
`x /\ join_c(S)` is already dominated (non-strictly) by an element known to be
in the cloud, then every `x /\ y` for `y in S` is dominated by that same
element, none can be maximal, and **the whole subtree is skippable without
computing a single meet**. Cost per visited node is one meet plus one
dominance query, against `|S|` meets and `|S|` insert attempts — no more
expensive than a single `insert_maximum`, and amortised against subtree size.
Non-strict domination is sound: if `x /\ join_c(S)` equals a known cloud
element, the meets it covers are duplicates of something already present.

Soundness. Maintain a bounded prune set `P` with the invariant **`P` is a
subset of the emitted intersection** — every element admitted to `P` was also
pushed into `intersection`. Pruning then can never empty the result, because
whatever did the pruning is itself in the result, which keeps `reset_tree`'s
`assert (not antichain.empty ())` safe. Cap `|P|` (64 is a starting guess, a
tuning knob and not a correctness parameter), keep it sorted by descending
`sum_key` so a query can stop once keys fall below the candidate's, and prefer
high-sum elements when full since they prune the most.

What is wrong today. `bboxtree_backed::intersect_with` (`:126-152`) already
does the right thing for `x` itself — line 133 calls
`other.tree.dominates (x, false, tree.get_keys ()[i])` with a precomputed key
and skips the inner loop when `x` survives. But when `x` does *not* survive,
line 141 falls back to a flat `for (const auto& y : other)` over the backing
vector, bypassing the tree, the bounding boxes and the key ordering entirely.

Two implementation constraints, both load-bearing:

- **Thread precomputed keys everywhere.** Never call the keyless `dominates`
  overload. `sum_key` (`utils/reduce.hh:18-29`) is concept-constrained on the
  element type providing `cached_sum()`, and the shipping vector type does not
  have it (`generic.hh:312` requires `HasSum`, while
  `simd_vector_backed = generic<..., false, true>`), so every keyless call
  degrades to a scalar `operator[]` loop over the dimension. A key-ordered
  traversal built on recomputed keys would pay that per visited node.
- **Report two numbers, not one.** Measure under both
  `best_decomp_bboxtree_mona` and `best_decomp_bboxtree_mona_cached_sum` (the
  latter already exists and sets `vector_impl: simd_vector_backed_sum`). If
  pruning only wins with the cached sum, that is a real result about the preset
  group. Do not silently flip the default `acacia_vector_impl` — that changes
  all four top configurations at once and destroys the comparability of the
  existing ranking.

Instrumentation. `bboxtree` already has a `stats` struct with `node_visits`,
`corner_prunes`, `accept_alls`, `cutoff_prunes` and `leaf_comparisons`
(`utils/bboxtree.hh:51-55`). Add `meet_subtree_prunes`, `meets_computed` and
`meets_skipped`, and report the prune rate — without them a null result is
uninterpretable, because you cannot tell whether the rule failed or simply
never fired. If `meet_subtree_prunes` is ~0 everywhere the change is dead
weight and should not land even if it is performance-neutral.

Acceptance gate, stricter than the protocol at the end of this document. A
**differential check**: build a variant with pruning compiled out and assert
that pruned and unpruned `intersect_with` produce the same antichain, element
for element, on every instance in `ab/syntcomp21/crit`. Verdict agreement is
necessary but nowhere near sufficient — a pruning bug that drops a genuinely
maximal element will still return the right verdict on most instances while
silently corrupting others.

Note the `incremental_limit = 400000` split at `:127-128`: above ~400k
candidates there is no accumulating maxima set at all, everything is
`push_back`ed for one batch reduction. `P` works in that mode too, and that is
where the largest win should be, because pruning avoids *materialising*
millions of doomed meets. Expect a memory reduction as well as a time one.
Leave the split itself alone until the prune-rate numbers are in.

### Tier 4 campaign result (2026-08-06/07): rejected

The experiment implemented the bounded 64-element prune antichain, threaded
precomputed keys through the subtree traversal, and retained deterministic
left-to-right emission order. A compile-time differential mode compared the
pruned and old flat intersections element for element. On all 94
`syntcomp21/crit` instances it produced 90 definitive verdicts and four clean
timeouts with no differential abort. Two losing race siblings hit the
pre-existing `utils::push_aps` recursion crash before reaching downset code;
their other children answered, so these are not antichain mismatches.

The fully optimized 17-second performance gate used the final 174-entry 2024
panel plus the independent 94-entry crit set:

| Configuration | 2024 solved / PAR-2 | crit solved / PAR-2 | Combined solved | Combined PAR-2 |
|---|---:|---:|---:|---:|
| shipping `vector_backed` | 98/174 / 2891.5 s | 89/94 / 261.4 s | 187/268 | 3152.9 s |
| pruned `bboxtree` | 97/174 / 2923.5 s | 90/94 / 228.9 s | 187/268 | 3152.4 s |
| pruned `bboxtree` + cached sum | 98/174 / 2881.6 s | 90/94 / 232.4 s | **188/268** | **3114.0 s** |

There were no UNKNOWN or error rows, but the best aggregate hides forbidden
paired regressions. Cached-sum gained `collector_v215` in both corpora and
`simple_arbiter_with_hints6`, while losing `amba_decomposed_arbiter6` and
`ltl2dpa19`. A three-repetition alternating gate on the five critical files
made the trade exact: shipping solved the same three files in every repetition
and cached-sum solved the other two in every repetition. In particular,
shipping needed about two seconds for each of the three cached-sum timeouts.

The counters also show that the rule is too weak for this distribution. The
skipped-meet rates on those five files were 7.05%, 0.41%, 0%, 0.32%, and 0%
respectively. The highest observed rate was on
`amba_decomposed_arbiter6`—precisely the case that regressed from a two-second
shipping solve to timeout. The small aggregate PAR-2 improvement cannot
override repeatable losses of already-answered instances. Tier 4 therefore
fails the landing bar; the implementation, options, diagnostics counters, and
posets changes were removed, and only these null-result measurements remain.

### Smaller items in the same tier

- `CPRE_AVOID_UNIONS` defaults to zero. Mode 1 performs one batch reduction
  over `|actions| × |f|` but remains off and unbenchmarked; mode 2 is still a
  compile-time “Not implemented” error at
  `src/solver/k_bounded_safety_aut.hh:208-209`.

- `sum_key` falls back to scalar indexing on the shipping vector type, which
  has no cached sum (see the constraint above).

- The three posets correctness findings — the promote-to-kd-tree path made
  unreachable by a `dim()` that returned the element count, a vacuous
  size-ratio guard from an `n = this->size ()` typo, and `log2(0)` undefined
  behaviour in `relabel_tree` on an empty tree — are **fixed and merged**
  (`ae0b4b7`; now `vector_dim()` at `vector_or_kdtree_backed.hh:41`, correct
  min/max at `:90-94`, `tree_capacity(...)` at `kdtree.hh:212`). Recorded so
  they are not re-opened from stale local notes. One consequence is still
  open: `KD_THRESH` was tuned while the promotion path was dead code, so the
  constant is unvalidated now that the path is live. The kd-tree backends
  remain outside the top four regardless.

- The backward `actioner.apply` has no early exit, while the forward path
  stops at its extreme value (`src/actioners/standard.hh:100-118`).

- `bool_threshold` and `bitset_threshold` are external globals consulted by
  the vector implementation, imposing repeated global loads on the bitset
  path.

## Tier 5 — automaton structure is mostly a closed lead

The SCC lever was tried and lost. `elevator`, the only
`spot::scc_info` consumer in `src/`, classifies SCCs and collapses safe or
losing traps. It ranks 16th of 20 at 813 solved and PAR-2 6584.6: ten
instances worse than shipping. It is disabled for a measured reason.

Backward Boolean-state saturation is also closed. The comment in
`src/boolean_states/forward_saturation.hh:8-20` records that the forward
computation is already exact for its definition and was checked against an
SCC-based implementation on arbiter sizes 3 through 6.

Determinism is exploited by `spot_nba_fastpath`, but the path rarely fires on
the hard set; SCC collapsing was measured and rejected. Per-SCC K bounds and
an SCC-topological decomposition of the fixed point remain genuinely
unexplored, but the negative `elevator` result ranks them below the choices in
the next menu.

## Next menu

The low-risk front-end work is now exhausted: keep `Small`, ship the syntactic
bypass, retain strict translator-option validation, and do not add the
translator-level matrix. The shallow Tier 3 storage and public
partitioned-relabel variants are also closed. The bounded Tier 4 meet-cloud
prune is closed as well: exact differential checking passed, but the prune
rate was low and its cached-sum variant repeatably lost three shipping
answers.

The remaining choices, in recommended order, are:

1. **Targeted translation work.** Diagnose the 17 still-stalling 2025
   translation cases (19 less the two degenerate-I/O wins) together with the
   eight refined 2024 cases. Separate decomposition and the realizability
   simplifier from Spot automaton construction before opening a translator
   option matrix; the cross-corpus target set is now large enough to support
   that work.
2. **A deep letter-loop redesign.** Replace or change partition construction
   *inside* Spot's `split_2step`, or remove the actioner's duplicate transition
   representation. Letter application dominates 17/32 fixed-point cases in
   2025 and 10/29 in 2024. Both shallow leads failed, so this remains
   higher-effort than translation diagnosis, but it is now the leading solver
   loop target across corpora.
3. **Action-construction diagnosis.** Isolate the nine 2025 and seven refined
   2024 cases that stop after translation but before the fixed point. The
   contiguous-storage experiment already ruled out the shallow ownership
   change; measure construction of the standard actioner and input picker
   separately before proposing another representation.
4. **The smaller downset batch experiment.** Ablate `CPRE_AVOID_UNIONS=1` on
   the focused panels and use `syntcomp21/crit` as its regression gate. It is
   cheaper than another tree design, but the rejected prune and the 11/32
   2025 downset share put its expected cross-corpus ceiling below the first
   three choices.

Broad option recombination still has a measured coverage ceiling of six
rescued instances, and SCC decomposition remains below these choices because
`elevator` is already negative.

## Hot-loop and native-front-end campaign (2026-08-09)

### Measurement foundation

The new campaign starts from the shipping `best_decomp_mona` preset and uses
three explicit gates before any optimization is allowed to land.  G0 is the
14-test Acacia unit suite plus the 14-test upstream Posets suite.  G1 is a
frozen 40-instance verdict gate: 25 `syntcomp24` instances from 23 families
and 15 `syntcomp25` instances from 14 families.  Each corpus contributes five
10--16 second sentinels; the other rows are sub-two-second verdict checks.
The first freeze deliberately rejected `collector_v215` and both copies of
`05.ltl` after repeat runs crossed the 17-second boundary, replacing them
with cases from the same immutable reference campaigns.  The resulting gate
verified all 40 expected verdicts sequentially.

G2 is a pinned upstream Posets microbenchmark built as C++23 with
`-march=native -Ofast -DNDEBUG`.  It records cycles, instructions, cache and
LLC misses, and branch misses for build, query, transfer, intersection,
union, a CPre-shaped apply/union/intersection phase, and the SIMD reduction
path.  Both dimension 10 and dimension 128 are covered; the latter includes
a 32-coordinate Boolean tail.  A candidate slice must improve its target by
at least 5% and may not regress any non-target phase by more than 5%.

All builds and experiments in this campaign run sequentially in systemd
cgroups with `MemoryMax=8G` and `MemorySwapMax=0`.  The first release link
peaked at 6.32 GiB inside that boundary.  The measurement extension is
upstream in Posets commit `530d06a`; Acacia pins that exact revision.

## Final landing verification

The final shipping and diagnostics builds were rebuilt sequentially in
8 GiB/no-swap scopes. Both passed all 14 unit tests, including the direct
degenerate-I/O and end-to-end empty-CLI-partition checks. The four focused
benchmark-tool pytest modules passed 16/16 under a 2 GiB/no-swap scope.

The labelled correctness gate ran all 624 realizable and unrealizable tests
sequentially under an 8 GiB/no-swap scope. It produced 566 correct answers,
58 timeouts, and `Fail: 0`; the log contains no false-positive or
false-negative marker. Timeouts are expected performance non-answers and are
not accepted as verdicts. Listing `ab/syntcomp24/panel` enumerates exactly the
174 entries in the regenerated panel.

Finally, the rebuilt shipping default reran the two translation-race landing
cases seven times at the 17-second cap, alternating instance order. Both
answered in all seven repetitions: `collector_v215.ltl` had a 16.293-second
median (16.105--16.484 range), and `05.ltl` had a 13.911-second median
(13.404--14.749 range). This confirms that the final Small default retains
the two answers whose loss disqualified the race.

## Measurement protocol

- Use the generated `syntcomp24/panel` plus `syntcomp21/crit` for routine
  comparisons, and the generated `syntcomp25/panel` as the overfitting check.
  Keep `syntcomp24/0s-1s` only for reproducing historical work.
- A solved row requires both Meson's `OK` result and a standalone printed
  `REALIZABLE` or `UNREALIZABLE` verdict. Report solved, timeout, UNKNOWN,
  error, and PAR-2 separately; all non-answers pay twice the cap.
- Run correctness gates against labelled directory ground truth, not merely
  against another solver's result.
- Record translation, preprocessing, action construction, and fixed-point
  time separately so improvements cannot move cost between stages unnoticed.
- Run sequentially at a stated 17-second cap. Every solver invocation gets
  its own systemd scope with `MemoryMax=8G` and `MemorySwapMax=0`; kill the
  entire scope on timeout so forked real/unreal workers cannot contaminate
  later timings.
- Record the Git revision, Spot version, and `SPOT_MAX_ACCSETS` in campaign
  metadata. Do not compare a 32-acceptance-set run with the 64-set baseline.
- Do not adopt a default that regresses an already-observed solved file merely
  because its net solved count is positive; do not repeat a campaign unless a
  future decision genuinely depends on another sample.
