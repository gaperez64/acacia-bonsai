# Semantic actions and the M2 downset frontier

This is a replace-in-place research record, like [LTLSYNT-GAP.md](LTLSYNT-GAP.md). Each campaign
replaces its own section wholesale rather than appending, so the document never accumulates stale
runs. Rejected experiments move to [What has been tried](#what-has-been-tried) and stay there.

## Decision

**In progress.** Gate A0 passes: the equality quotient has a large, family-spanning target and
inclusion dominance has a second one on top of it. Stage A1 is implemented and exactly validated
against the baseline. The landing gates are running.

## Why this sprint exists

[LTLSYNT-GAP.md](LTLSYNT-GAP.md) closes the `ltlsynt` comparison with a 261-row residual census in
[gap-census.tsv](gap-census.tsv), split by mechanism:

| mechanism | rows | status entering this sprint |
|---|---:|---|
| M1 letter-loop | 122 | open; this sprint's Sprint A |
| M2 downset | 68 | open; this sprint's Sprints B–E |
| M3 translation-stall | 36 | closed as inherent: the counting fixed point only consumes Büchi |
| mixed | 26 | split across M1 and M2 mechanisms |
| M4 one-sided-race | 9 | closed: never met the 15-instance implementation threshold |
| resolved empty-side partition | 6 | wrapper artifact, retained as an audit trail |

Both open buckets are *materialization* problems.

**M1.** `src/ios_precomputers/mona.hh` builds one canonical BDD `R(i,o,p,q)` and then expands
every output path into a concrete transition set. `recurse_outputs` carries the defect in its own
words at `mona.hh:101`: `// TODO There may be more than one way to reach this bdd_pq; cache.` Two
output paths that reach the same residual BDD root at the state-variable frontier denote the same
endpoint relation and decode to identical transition sets, and each duplicate is applied again on
every CPre.

**M2.** The bounded-safety fixed point keeps an explicit maximal-generator antichain. On the
anchors named in [LTLSYNT-GAP.md](LTLSYNT-GAP.md)'s open leads, `ltlsynt` proves `lift_unary_enc3`
in 0.04 s, `lift5` in 0.13 s and `robot_grid2_2` in 0.63 s while Acacia decides none; with a
genuine unique bound the baseline runs 260 iterations at `k=50` and the antichain reaches 151,792
elements.

The sprint tests whether either materialization can be avoided, under the existing landing gates.
It does not replace Acacia with deterministic automata and does not delegate to `ltlsynt`.

## Frozen baseline

| item | value |
|---|---|
| Acacia | `700e65bd` (merge of PR #121), branch `codex/semantic-actions-and-m2` |
| Posets submodule | `bd71d64720c1f190cfbcc7ae5b6bb97e0155d150` |
| tlsf-tools submodule | `8f7b8e3629d053b1d911ed4f5d343b471ab81906` |
| syntcomp-benchmarks submodule | `4105caf1f1e5fd3b76657879bfce8021d130cbde` (v2026) |
| reference preset | `best_decomp_rank_bucketed_mona` |
| TLSF corpus | `benchmarking/syntcomp-corpus.py materialize`, verified against `tests/suites/benchmarks/tlsf-manifest.tsv` |

The sprint brief pinned the baseline at `d363c89d`. That commit is eight commits behind the merged
PR #121 head and predates both the preset-registry validation and the single-expansion suite
manifest, so the baseline was re-frozen at the merge commit under the brief's own rebase rule.
The submodule pins are unchanged and match the brief exactly. Baseline timings are re-measured on
this tree; archived timings from other trees are not used for comparison.

Protocol for every gate: 17-second per-instance cap, one 8 GiB zero-swap user-systemd scope,
`MemorySwapMax=0`, sequential runs with no competing CPU work. PAR-2 charges twice the cap for
every non-answer. PAR-2-only movements inside the measured same-configuration spread — 21.134 s on
SYNTCOMP25, 12.074 s on SYNTCOMP26 — are not performance evidence on their own.

## Sprint A: pre-decoding semantic action quotient

### Prior art this stage must beat

A whole-letter action quotient was measured and rejected in the PR #118 campaign; the record is in
[LTLSYNT-GAP.md](LTLSYNT-GAP.md). It canonicalized each concrete output letter by its complete
transition-relation action *after* decoding, cut actions 14.16–15.59× on the three highest-action
targets, passed G1 at 40/40 with PAR-2 101.867 → 87.880 s, and gained two SYNTCOMP25 G3 answers.
G2s then found `syntcomp24/round_robin_arbiter4.ltl` moving from 32,306,372,637 to
252,184,896,339 median cycles — a 680.604% regression with all three quotient runs reaching the
60-second cap — and the prototype was removed.

Sprint A is the same equivalence computed *before* decoding, which is cheaper, plus an explicit
account of why the previous attempt regressed. Two order channels carry that risk, and the sprint
brief names only the first:

1. **Action order inside an input.** `src/input_pickers/critical.hh:46-70` — the shipping picker,
   since the preset sets `input_picker: critical` — scans an input's actions in order, breaks at
   the first action that keeps `f` in the region, and splices that action to the front. A
   first-occurrence quotient yields a subsequence of the current DFS order, so this channel is
   safe by construction.
2. **Input order.** `src/actioners/standard.hh:52` keys `std::set<input_and_actions,
   compare_actions>` on `x.second < y.second`, a lexicographic comparison of the *entire* per-input
   action list. `input_output_fwd_actions` is therefore ordered by action-list content, and
   `critical.hh:32-34` rebuilds its scan list from that order on every call. Removing duplicate
   actions changes every comparison key and reorders the inputs. `round_robin_arbiter4` is an M2
   row with `max_f` 7,154 over 875 loops, so it picks inputs 875 times.

The second channel is the likelier explanation of the 680.604% regression, and it is the one this
sprint must control. G2s therefore runs as an early pre-gate for Sprint A, not as a final
formality.

The exact equality quotient already ships on the equivariant path:
`src/solver/equivariant_k_bounded_safety_aut.hh:44-47` documents `input_orbit::actions` as
"Deduplicated backward actions of the representative letter, over ALL output letters.
Deduplication of identical `action_vecs` is exact for the union." Sprint A is new work only for
the classic path. The `equivariant_decline` column of [gap-census.tsv](gap-census.tsv) confirms
the relevant workers are all on the classic path: `OneCounter` and `tmp_13cfc6f2` decline with
"no indexed AP families", `f-real-real` with "no indexed input AP families", and
`round_robin_arbiter4` with "0/0 star gens". The earlier regression cannot be attributed to
equivariant interaction.

### Stage A0: semantic action census

**Gate A0 passes.** The equality quotient has a large, family-spanning target, and inclusion
dominance has a second one on top of it.

Raw data: `benchmarking/semantic-action-census.tsv`, campaign directory
`_bm-logs.semantic-census-20260829/`. Cohort: every M1 and M2 row of
[gap-census.tsv](gap-census.tsv), taken from the table itself rather than a hand-kept copy --
190 distinct instances, 470 worker rows, 0 skipped. Preset
`best_decomp_rank_bucketed_mona_diag`, 25-second cap, one 8 GiB zero-swap user-systemd scope per
invocation, per-input dominance budget 1500 ms. syntcomp26 has no `.ltl` source map, so its 49
rows ran through the native TLSF frontend.

Equality: `raw_output_paths / unique_residual_roots`.

| ratio | workers |
|---|---:|
| exactly 1, the quotient is a no-op | 112 |
| at least 2 | 173, across 31 families |
| at least 8 | 37 |
| at least 100 | 18 |

Median 1.542; maximum 3,591,372.8. Gate A0 asked for ten workers from two families at ratio at
least 2, or one stall at ratio at least 8; both clauses are met many times over.

| instance | worker | raw output paths | residual roots | ratio |
|---|---|---:|---:|---:|
| `syntcomp24/amba_decomposed_lock16` | unreal-automaton | 35,913,728 | 10 | 3,591,372.8 |
| `syntcomp24/amba_decomposed_lock15` | unreal-automaton | 15,859,712 | 10 | 1,585,971.2 |
| `syntcomp24/amba_decomposed_lock16` | real | 4,194,304 | 8 | 524,288.0 |
| `syntcomp25/amba_decomposed_lock_pb_13_pe_` | unreal-automaton | 3,014,656 | 10 | 301,465.6 |
| `syntcomp24/amba_decomposed_lock12` | unreal-automaton | 1,294,336 | 10 | 129,433.6 |

Dominance: `unique_residual_roots / minimal_residual_roots`, keeping the inclusion-minimal
residual relations because more universal successors is worse for the controller.

| ratio | workers |
|---|---:|
| at least 1.5 | 80, across 22 families |
| at least 2 | 67 |
| at least 4 | 33 |
| at least 8 | 9 |

| instance | worker | residual roots | minimal | ratio |
|---|---|---:|---:|---:|
| `syntcomp24/prioritized_arbiter7` | real | 512 | 18 | 28.4 |
| `syntcomp24/prioritized_arbiter6` | real | 256 | 16 | 16.0 |
| `syntcomp24/OneCounter` | unreal-formula | 9,497 | 1,013 | 9.4 |
| `syntcomp24/workstation_resupply_3` | real | 1,748 | 187 | 9.3 |
| `syntcomp24/TwoCountersDisButAC` | unreal-automaton | 9,800 | 1,262 | 7.8 |

Split by mechanism, the equality quotient is not an M1-only phenomenon:

| cohort | workers | median ratio | ratio >= 2 | ratio >= 8 | dominance >= 1.5 |
|---|---:|---:|---:|---:|---:|
| M1 letter-loop | 304 | 1.71 | 121 | 18 | 57 |
| M2 downset | 166 | 1.40 | 52 | 19 | 23 |

Three caveats the numbers carry, stated so later campaigns read them correctly.

**The dominance figures are lower bounds.** 170 of 470 workers exhausted the 1500 ms per-input
budget and were recorded as having no reduction, which is the conservative direction. The
`dominance_declines` column marks them, so a bail-out is never mistaken for a measured absence of
dominance.

**Census-only mode measures the automaton before preprocessing.** `ACACIA_DIAG_ALPHABET_CENSUS_ONLY`
short-circuits ahead of the automaton preprocessor and the Boolean-state pass, so its numbers are
close to but not identical with what the solver's own expansion sees. On `OneCounter`'s
realizability worker the path count agrees exactly at 40,448 while the residual-root count is
7,711 here against 7,965 in a full run. Structural conclusions are unaffected; exact per-run
validation belongs to the decode mode below.

**The `census_ms` and `dominance_ms` columns were measured under light concurrent load** (a
two-job `nice -19` compile ran during part of the campaign). They are diagnostic, not gate inputs;
the structural counts are load-independent. Stage A2 re-measures them on a quiet machine before
using them to judge whether dominance pays for itself.

`census_ms` covers traversal and residual-root dedup together. They are one memoized DAG walk, and
separating them would mean walking twice and reporting something other than the implementation
being measured.

### What the duplication actually is

Writing the stage A1 test turned up the mechanism, and it is not the obvious one. Duplication does
**not** come from unused output propositions: the BDD never branches on a variable that occurs in
no guard, so a don't-care output costs no extra path. It comes from **disjunctive guards**, where
several output branches reach one endpoint relation. A first test fixture built on the don't-care
assumption quotiented nothing at all.

That also explains the shape of the distribution above. The 112 workers at exactly 1.0 are those
whose guards partition the output space; the `amba_decomposed_lock` family, where a wide
disjunction guards a handful of destinations, is the opposite extreme at six decimal digits of
ratio.

### Stage A1: equality quotient before relation decoding

Status: **implemented and validated; landing gates in progress.**

`ios_precomputers::semantic_mona` keeps the first output path that reaches each residual BDD node.
It is a template parameter on the existing descent rather than a second copy of it, so there is
one implementation of the traversal and not two that must be kept identical by hand.

The differential unit test asserts the exact claim: the quotient's action list equals the
first-occurrence dedup of the baseline's, the input cubes keep their order, the union over outputs
agrees on every rank vector in `{-1..K}^states` exhaustively, and the verdict and winning bound
match. It also asserts that the baseline really does decode duplicates on the duplicating fixture,
so the test cannot pass vacuously if the quotient stops working.

Head-to-head on the real solver, `--spot-fast off` with a single orientation selected so the two
binaries expand the same automaton:

| instance | worker | decoded, base -> quotient | unique | decode ms, base -> quotient |
|---|---|---|---:|---|
| `syntcomp24/OneCounter` | real | 40,448 -> 7,965 | 7,965 | 923 -> 193 |
| `syntcomp24/amba_decomposed_lock10` | unreal-automaton | 229,376 -> 10 | 10 | 349 -> 5 |
| `syntcomp24/round_robin_arbiter4` | real | 256 -> 256 | 256 | 12 -> 11 |

The quotient decodes exactly the unique count in every case, which is the invariant stage A1 has
to satisfy.

The third row is the one that matters for the landing argument. `round_robin_arbiter4` is the
instance whose 680.604% G2s regression rejected the previous whole-letter quotient. Its ratio is
exactly 1: the quotient removes nothing, so the two binaries build *identical* action lists there.
That closes both order channels on that instance at once -- not only the per-input action order,
but also the input order, since `actioners::standard` keys its set on the per-input action list
and that key is unchanged. G2s on this target is therefore a prediction, not a hope: it must show
no change beyond noise.

### Stage A2: inclusion-dominance pruning before decoding

Status: not started; gated on A1.

## Sprint B: static obligation-scheduling risk detector

Status: not started.

`tlsf-tools` already recognizes most of the required syntactic structure. `include/tlsf/recognize.h`
matches response `G(r -> F g)`, pure recurrence `G F x`, mutex `G(!(a && b) ...)` and
temporal-free safety invariants `G(B)`, and it already forms a multi-constraint
`arbiter_candidate` block from responses plus a grant mutex — which is the scheduling signature
this sprint hypothesizes. `include/tlsf/liveness_class.h` already reports `n_response`,
`n_recurrence`, `n_eventual` and `n_until`. Stage B0 is therefore an export of `ConstraintCover`
data with a stable schema, not a new analyzer.

## Sprint C: operation-aware M2 representation census

Status: not started.

The decisive question is not static compression. It is whether one complete input-conditioned
update `D <- D ∩ ⋃_o Pre_{i,o}(D)` stays compact and cheaper in the compressed representation.
Answering it exactly requires snapshot data Acacia does not record today:
`src/solver/antichain_snapshot.hh` dumps one antichain per loop at the loop *head*, before the
picker and before CPre, with no schema version, no selected input, no action profiles, no
orientation and no symmetry layout, and it is never called from the equivariant solver.

## Sprints D and E: adaptive probe and compressed backend

Status: not started; gated on Sprint C.

## What has been tried

Nothing has been rejected in this sprint yet. Rejections from earlier campaigns remain in
[LTLSYNT-GAP.md](LTLSYNT-GAP.md).

## Final validation

Not yet run. The sprint closes with a G0–G5 checklist in this section, each line carrying the
tool's literal verdict string.

## Next experiment boundary

Pre-committed before any measurement, so a later campaign cannot move the bar to fit its result.

- **Sprint A may land** only with G0 clean, decoded-relation count exactly equal to the census
  `unique_residual_roots`, no increase in `actions_seen`, `GATE PASS` on G1 at 40/40, `GATE PASS`
  on G2s with no per-instance regression on `round_robin_arbiter4`, and `GATE PASS` on both G3
  panels with no genuine lost answer. A favourable G1 and G3 do not override a G2s failure; that
  is exactly the combination that rejected the previous quotient.
- **Sprint B may be wired into runtime routing** only if one interpretable rule reaches precision
  at least 0.8 for large-frontier cases across at least two families, or identifies at least five
  current M2 timeouts across at least two families with at most two false-positive expensive
  routes. Family-name proxies are not structural features and do not count. Otherwise it stays
  research telemetry.
- **A compressed representation may proceed to a prototype** only if at least five hard snapshots
  from at least two families compress by at least 8×, *and* the region after the complete CPre
  stays within 4× the representative count before it. Static compression alone is not admission.
- **A symbolic backend may be admitted** only if, on at least five hard updates from two families,
  it reproduces the explicit result exactly, its median CPre time is no worse than explicit
  replay, no tested anchor shows catastrophic node growth, and peak size gives a real memory
  advantage over explicit literal mass.
- **No backend may be entered on region size alone.** A recognizer or probe must certify both
  representation compression and CPre preservation.
