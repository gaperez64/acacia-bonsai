# Semantic actions and the M2 downset frontier

This is a replace-in-place research record, like [LTLSYNT-GAP.md](LTLSYNT-GAP.md). Each campaign
replaces its own section wholesale rather than appending, so the document never accumulates stale
runs. Rejected experiments move to [What has been tried](#what-has-been-tried) and stay there.

## Decision

**In progress.** No stage has reached a landing verdict yet. The current stage is A0, the
semantic-action census.

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

Status: **in progress.**

The decisive ratio is `raw_output_paths / unique_residual_roots`. Acacia already measures its
aggregate form: `src/ios_precomputers/alphabet_census.hh` counts distinct BDD nodes at the
state-variable frontier (`collect_frontier` against `first_state`) and paths to that same boundary
(`count_paths`). What A0 adds is per-input granularity, the inclusion-dominance count, decoded and
action-vector counts for validation, and a phase split across traversal, dedup, dominance and
decode.

Prior structural evidence, from the archived alphabet census of 2026-08-14
(`_bm-logs.alphabet-census-20260814/descents.tsv`, a different tree — recorded here as motivation,
not as a gate result). Over 101 output-descent rows from 58 targets:

| `raw_output_paths / unique_residual_roots` | rows |
|---|---:|
| exactly 1, the quotient is a no-op | 34 |
| at least 2 | 22 |
| at least 8 | 6 |

| target | path | raw output paths | residual roots | ratio |
|---|---|---:|---:|---:|
| `amba_decomposed_lock16.ltl` | real | 4,194,304 | 8 | 524,288.0 |
| `amba_decomposed_lock15.ltl` | real | 1,966,080 | 8 | 245,760.0 |
| `FelixSpecFixed2_fa4d4ce3.ltl` | unreal-formula | 138,022 | 2 | 69,011.0 |
| `FelixSpecFixed3_b0840146.ltl` | unreal-formula | 138,022 | 2 | 69,011.0 |
| `Automata16S.ltl` | real | 491 | 43 | 11.4 |
| `workstation_resupply_4.ltl` | unreal-formula | 3,584 | 384 | 9.3 |
| `workstation_resupply_3.ltl` | unreal-formula | 1,184 | 160 | 7.4 |
| `f-real-real.ltl` | real | 1,952 | 608 | 3.2 |
| `tmp_13cfc6f2.ltl` | real | 1,594 | 558 | 2.9 |
| `abcg_arbiter3.ltl` | real | 18,432 | 18,432 | 1.0 |

Gate A0 asks for ten hard workers from at least two families at ratio at least 2, or any
action-construction stall at ratio at least 8. The archive meets both clauses. The third of rows
sitting at exactly 1.0 sets the other requirement: the quotient must cost approximately nothing
where it buys nothing.

### Stage A1: equality quotient before relation decoding

Status: not started.

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
