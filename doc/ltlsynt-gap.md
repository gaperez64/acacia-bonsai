# Closing the `ltlsynt` gap

This roadmap separates the next performance workstream from the symmetry
paper. Its scope is solver internals, automaton structure, and the front end;
post-synthesis optimization is deliberately excluded.

## The shape of the problem

The common ranking campaign contains 994 `syntcomp24/0s-1s` instances, uses a
17-second cap, and compares 20 configurations:

| Rank | Configuration | Solved | PAR-2 |
|---:|---|---:|---:|
| 1 | `ltlsynt_no_obligation` | 918 | 2882.1 |
| 2 | `ltlsynt` | 913 | 3056.3 |
| 4 | `ltlsynt_no_bypass` | 892 | 3769.4 |
| 6 | `best_decomp_mona_any` | 825 | 6154.8 |
| 7 | `best_decomp_rank_bucketed_mona` | 824 | 6198.7 |
| 8 | `best_decomp_filtered_vector_mona` | 824 | 6198.8 |
| 9 | `best_decomp_mona_equivariant` | 823 | 6234.7 |
| 10 | **`best_decomp_mona` (shipping)** | **823** | **6247.5** |
| 16 | `best_decomp_mona_elevator` | 813 | 6584.6 |
| 20 | `best_decomp_sharingtree_mona` | 772 | 7928.2 |

Two facts frame the roadmap. The shipping configuration trails `ltlsynt` by
90 solved instances, while the entire spread across all 15 Acacia
configurations is 53. Recombining existing knobs cannot close the gap,
although those knobs are not all set to their own measured optimum.

The loss set is also not primarily the arbiter family. Of 114
`ltlsynt`-only instances, 38 are from the `ltl2dba_*` and `ltl2dpa_*`
families: pure LTL with neither client symmetry nor block structure.
Arbiter-specific work, including symmetry reduction, addresses a minority of
the loss set.

## Tier 0 — use the best configuration already available

`ACACIA_TRANSLATION_PREF` still defaults to `Small`
(`src/solver/create_automaton.hh:10-14`, `meson.options:36-39`). The source
comment calls `Any` an ablation because it can change conclusiveness, but
conclusiveness is precisely what the ranking measures:
`best_decomp_mona_any` is two solved instances and 92.7 PAR-2 seconds ahead
of shipping. This exceeds the coverage gain of the symmetry work and needs
only a default change once its correctness gate passes.

The gate is verdict correctness, not speed. Run the established 902 labelled
LTL instances in `tests/ltl/{realizable,unrealizable}` and require every
definitive verdict to agree with the directory ground truth. The current
corpus may contain additional labelled fixtures; include them as a
superset. Only then should `acacia_translation_pref` default to `any`.

Two nearby downset variants are also positive despite carrying avoidable
work. `rank_bucketed` and `filtered_vector` each add one solved instance and
save about 49 PAR-2 seconds. The former rebuilds write-only `buckets` in
linear time on every insert, while the latter consults its filters only in
`contains` (`sum.md`, Section 6). These results justify cleanup and a fresh
ablation, but not a blind default flip.

Correction to the retired diagnosis: it said that `Small` to `Any` had
already become the default. It has not.

## Tier 1 — add a syntactic bypass

The largest measured structural lever is `ltlsynt`'s pre-game bypass:
`ltlsynt_no_bypass` solves 892 instances versus 913 for `ltlsynt`, a
21-instance contribution on this exact corpus.

Spot's `try_create_direct_strategy`
(`subprojects/spot/spot/twaalgos/synthesis.cc:1223`) recognizes cheap
syntactic cases before constructing a game, including `G(bool)`, the
`GF`/`FG` dual cases, and syntactic obligations. Acacia has no comparable
front-end path: its `src/` tree contains no use of
`is_syntactic_safety`, `is_syntactic_obligation`,
`is_syntactic_persistence`, or `is_syntactic_recurrence`.

The companion ablation identifies what not to copy:
`ltlsynt_no_obligation` ranks first at 918 solved. Full obligation synthesis
costs `ltlsynt` five instances here. Implement and independently test the
cheap `G(bool)`, `GF`, and `FG` special cases; defer the obligation machinery.

Acacia does have one bypass. `spot_nba_fastpath` routes a deterministic
universal co-Büchi automaton through Spot's `split_2step` and game solver.
Diagnostics report `fast_class = gfg-disabled` on essentially every hard
instance, so this path rarely covers the loss set.

## Tier 2 — expose translation controls that have never been ablated

`src/solver/solver_invoker.cc:283-285` hardcodes `simul`, `ba-simul`, and
`det-simul` to zero. They have no Meson options, presets, or configuration
registry entries and therefore have never appeared in a controlled ablation.
`ltlsynt` leaves all three enabled. Acacia also never calls
`spot::translator::set_level`.

Expose the three simulation switches and the translator level independently,
then measure verdicts, translation time, automaton size, and solve time.
This targets a measured cost: on `07.ltl`, translation consumed 6700 ms of a
16294 ms run and produced a 13,609-state, 155,315-edge automaton; the
`standard` preprocessor then left its state count unchanged.

## Tier 3 — attack the letter loop

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

Prototype partitioned relabelling first, with diagnostics for alphabet
partition size and action count. In parallel, flatten the actioner's hot
representation and benchmark the letter loop independently of downset
operations.

## Tier 4 — solver internals, with a coverage ceiling

A per-instance oracle over every downset backend rescues only five of 192
losses. This tier is therefore PAR-2 and constant-factor work, not a route
to broad coverage. That remains worthwhile on the arbiter family, where a
constant factor is worth roughly three client sizes, but the expected payoff
must be stated honestly.

- The bboxtree meet-cloud generation pruning designed in `sum.md`, Section
  4, never landed. The posets update contains the kd-tree correctness fixes,
  but this design is the only one that avoids generating dominated meet
  points rather than filtering them afterward. `intersect_with` is the last
  operation in classic `cpre_inplace`.

- `CPRE_AVOID_UNIONS` defaults to zero. Mode 1 performs one batch reduction
  over `|actions| × |f|` but remains off and unbenchmarked; mode 2 is still a
  compile-time “Not implemented” error at
  `src/solver/k_bounded_safety_aut.hh:208-209`.

- `sum_key` falls back to scalar indexing on the shipping vector type, which
  has no cached sum.

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
unexplored, but the negative `elevator` result ranks them below Tiers 0
through 3.

## Recommended order

1. Verdict-gate `translation_pref=any`, then change the default if it agrees
   with all labelled ground truth.
2. Expose and ablate `simul`, `ba-simul`, `det-simul`, and translator level.
3. Build the cheap syntactic bypasses. This is the only candidate sized
   against the 90-instance coverage gap.
4. Profile and redesign action enumeration around the
   `amba_decomposed_lock14` and `mux16` cases.

Defer broad downset work (measured ceiling about five rescued instances) and
SCC decomposition (`elevator` is already negative) until the higher tiers
have been measured.

## Measurement protocol

- Keep the common 994-instance, 17-second ranking panel for configuration
  comparisons and report solved count together with PAR-2.
- Run correctness gates against labelled directory ground truth, not merely
  against another solver's result.
- Record translation, preprocessing, action construction, and fixed-point
  time separately so improvements cannot move cost between stages unnoticed.
- Run each solver in its own process group or systemd scope and kill the
  entire group on timeout; orphaned real/unreal workers otherwise contaminate
  later timings.
- Treat boundary-only solved-count changes as unstable unless they repeat
  under reversed configuration order.
