# Small inductive invariants for the M2 downset

A replace-in-place research record, like [LTLSYNT-GAP.md](LTLSYNT-GAP.md) and
[SEMANTIC-ACTIONS-AND-M2-SPRINT.md](SEMANTIC-ACTIONS-AND-M2-SPRINT.md). Each campaign replaces its
own section rather than appending. Rejected experiments move to
[What has been tried](#what-has-been-tried) and stay there.

## Decision

**In progress.** Gate 0 passes; the S1 census is running.

## Why this sprint exists

[SEMANTIC-ACTIONS-AND-M2-SPRINT.md](SEMANTIC-ACTIONS-AND-M2-SPRINT.md) closed the compressed-region
branch on evidence: the hard M2 automata have no verified permutation group even under exhaustive
all-pairs detection, and the unary threshold-BDD encoding of a single rank vector over hundreds of
coordinates is already larger than the vector, its CPre exploding past 120 M nodes to compute a
region the explicit antichain holds in 240 vectors.

Every one of those results is about representing Acacia's **complete permissive winning region**
compactly. None of them says anything about whether a much smaller *controlled inductive
subregion* exists inside it.

A set of generators `G` is a winning certificate when the initial rank vector lies in `↓G` and,
for every `g ∈ G` and every input class `i`, some action `a` and some `h ∈ G` satisfy
`τ_{i,a}(g) ≤ h`. Any such post-fixed point sits inside the greatest winning region, so a verified
certificate proves the worker winning without that region ever being built.

Two facts make this worth measuring rather than assuming.

**Acacia's own termination test is the criterion.** `src/input_pickers/critical.hh` scans, for each
maximum and each input class, the actions for one whose forward image stays in the region, and
reports the input critical only when none does. "No critical input" is exactly the condition
above. The verifier already exists in the hot path; what is new is asking it of a *subset*.

**The action tables are smallest exactly where the frontiers are biggest.** From
[semantic-action-census.tsv](semantic-action-census.tsv), `lift3` carries 14,060 maxima and 120
actions; `arbiter_with_buffer6` carries 1,984 maxima and 262,144 actions. A search that is
polynomial in the action table and only bounded in the generator count is therefore cheap
precisely where the explicit frontier is expensive.

## Frozen baseline

| item | value |
|---|---|
| Acacia | `codex/small-inductive-invariants`, stacked on PR #122 head `1d932514` |
| Posets submodule | `139e14336b7a1f0bc064022e587ea4e1b9a81427` (merge of PR #33) |
| tlsf-tools submodule | `b42d5ef4a680252e04820ac7f073f5d786a43f7c` (merge of PR #27) |
| reference precomputer | `ios_precomputers::semantic_mona`, landed in PR #122 |
| research preset | `best_decomp_rank_bucketed_mona_diag` with `-Dbuild_research_tools=true` |

If PR #122 merges or rebases before this work lands, the baseline is re-frozen from the merged
tree and both sides re-measured; no comparison against timings taken from the pre-merge head.

Research campaigns run at a **120-second** cap, one solver per 8 GiB zero-swap user-systemd scope.
Every landing gate keeps the frozen **17-second** protocol. The two never mix.

## Stage S0: complete action table and exact replay

**Gate 0 passes.**

Snapshot schema 3 adds what an offline search needs and the CPre event did not carry:

- `meta.tsv` gains `init_state`, `worker` and `instance`. The initial rank vector is `-1`
  everywhere and `0` at the initial state, so the state number is all it takes; the worker
  orientation is needed because the dump directory is keyed by pid, which separates workers but
  does not say which is which.
- Checkpoints trigger on the **first crossing** of frontier sizes 16, 64, 256, 1024, 4096 and
  16384, plus the first loop, plus the region the solver finished with. Sampling by loop number
  says when a dump happened; sampling by frontier size says what the region looked like.
- A checkpoint records whether the bound was raised on the way in. A raise re-inflates every
  maximum, so the region is deliberately an over-approximation at that moment and is not a
  candidate for "is this already inductive".
- `all-input-actions.tsv` records every input class with its ordered actions, once per automaton,
  behind its own opt-in and caps. The CPre event carries the one input the picker selected; a
  search for an inductive subregion asks of every candidate whether *every* input has a supporting
  action.

The rank semantics live in one place, `src/research/rank_action_replay.hh`, shared with the CPre
replay. `tests/small_invariant_test.cc` compares `apply_forward` and `apply_backward` against
`actioners::standard::apply` itself on 400 random vectors and actions — not against a hand-written
expectation, because a disagreement between the offline tools and the solver is the one thing that
would make every later measurement ambiguous.

The offline continuation mirrors the solve loop — find a critical input, apply that one update,
repeat — and uses the solver's own downset. A first attempt did neither, rolling a private
`O(n²)` antichain reduction and sweeping every input class, and did not finish on a 6,343-maximum
region in 110 s. Applying CPre for a non-critical input is correct but recomputes an intersection
that cannot remove anything.

Gate 0 result, on `syntcomp24/SPI` (12 states, 10 input classes, 24,883 actions):

| checkpoint | maxima | iterations | final maxima | contains init | matches the solver's final region |
|---|---:|---:|---:|---|---|
| loop 1 | 1 | 4 | 15 | yes | **yes** |
| loop 5 (final) | 15 | 0 | 15 | yes | **yes** |

Agreement is checked by mutual containment of generators, not by size. The second row is the
sharper one: from the region the solver stopped at, the continuation immediately reports no
critical input, so the offline criticality test agrees with the solver's own claim of a fixed
point.

## Stage S1: generator-subset core

Status: **census running.**

Peeling is decremental: a witness per `(generator, input)`, lazy reverse dependencies, and an
active-subset dominator lookup that mirrors the rank-sum prefilter
`rank_bucketed_vector_backed::contains` uses — starting the scan at the first generator whose
coordinate sum is at least the query's, because `v ≤ w` implies `rank(v) ≤ rank(w)`.

The shape of the answer is constrained in advance. At the solver's fixed point every generator
already satisfies the criterion — that is why the picker returns nothing — so peeling can remove
nothing there. Both behaviours are visible on `SPI`: the loop-5 fixed point peels 15 to 15 and
verifies, while the loop-1 safe set peels 1 to 0. **The whole question is whether a support-closed
subset containing the initial vector appears at an intermediate checkpoint, before the exact
iteration converges, and how much earlier.**

### Gate 1A, pre-committed

Continue to live integration only if, before the baseline fixed point, at least 5 expensive
winning workers from at least 2 families produce a core containing the initial vector, and the
median `core_maxima / checkpoint_maxima` is at most 0.5. A timeout solved by a core counts more
strongly than the ratio.

### Gate 1B, pre-committed

Land a live probe only if it solves at least 2 current 17-second M2 timeouts from at least 2
families, or gives a robust panel improvement with no losses; with probe overhead on unsuccessful
workers inside the landing threshold, and G0, G1, G2s, G3 and G4 all passing.

## Stages S2 to S4

Not started. S2 is the forward bounded strategy kernel, S3 the checkpointed width schedule as an
exact control, S4 fixed-policy meet templates.

## Known coverage gap

The equivariant solver makes no snapshot calls, so the seven M2 workers with a verified symmetry
group — `arbiter7`, `arbiter8`, `arbiter_with_cancel6`, `load_balancer6`, `full_arbiter_enc8` and
their parameterized twins — produce no data for this sprint. This was found when
`full_arbiter_enc8` dumped nothing at all. It does not block S1, because every frontier-heavy
anchor is on the classic path, but it does mean the arbiter block is invisible here.

## What has been tried

Nothing has been rejected yet.

## Final validation

Not yet run. The sprint closes with a G0–G5 checklist, each line carrying the tool's literal
verdict string.

## Next experiment boundary

Pre-committed, so a later campaign cannot move the bar to fit its result.

- **A certificate is only a certificate after independent re-verification**: every forward image
  recomputed against the reduced antichain, `r_init ∈ ↓G` rechecked, and every generator checked
  against the safe vector. A heuristic search result is never trusted on its own.
- **A failed search proves nothing.** It must leave the downset, the action order, the input
  order, `k` and the exact diagnostics untouched, and return control to ordinary Acacia.
- **Omitted maxima are never added back to a contracted branch.** States removed during a
  contracting iteration do not regenerate; any widening restarts from the checkpoint.
- **Synthesis is a separate decision, not a flag.** `post_real` rebuilds its actions with
  `ios_precomputers::delegate` and `actioners::no_ios_precomputation`, which partitions the inputs
  differently from the solver. A certificate verified against the solver's semantic input classes
  is not automatically valid there, and the only guard is an `assert`. Until that re-verification
  exists, no small invariant may be handed to synthesis.
