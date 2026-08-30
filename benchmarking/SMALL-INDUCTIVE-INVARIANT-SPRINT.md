# Small inductive invariants for the M2 downset

A replace-in-place research record, like [LTLSYNT-GAP.md](LTLSYNT-GAP.md) and
[SEMANTIC-ACTIONS-AND-M2-SPRINT.md](SEMANTIC-ACTIONS-AND-M2-SPRINT.md). Each campaign replaces its
own section rather than appending. Rejected experiments move to
[What has been tried](#what-has-been-tried) and stay there.

## Decision

**Gate 0, Gate 3A, Gate U1, G0 and G1 pass. Gate 1A fails. The landing panels decide.**

The generator-subset core is dead: it finds nothing before the solver converges, and its cost is
already twice the landing cap at a fifth of the frontier sizes that matter. The forward bounded
kernel search is alive and stronger than expected -- verified certificates on 6 workers across 5
families, four of them instances Acacia currently cannot answer at all, one of them found in a
millisecond from the safe set.

Live integration is next, and is what decides whether this becomes a production result.

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

**Gate 1A fails, on both clauses.**

Over 21 checkpoints from 7 workers, two have a non-empty core and **zero** were found before the
solver converged. Both non-empty cores are at the fixed point itself -- `SPI` peels 15 to 15,
`TwoCountersDisButA8` 1 to 1 -- which is trivially guaranteed, because at the fixed point every
generator satisfies the criterion and that is precisely why the input picker returns nothing.
`LedMatrix` peels to zero at every checkpoint through 5,190 maxima.

The reason is structural and the brief anticipated it: the winning region's generators are
generally *interior* points of an over-approximation, not its maximal generators, so no subset of
the maxima can find them.

The probe cost disqualifies it independently:

| checkpoint maxima | peel time |
|---:|---:|
| 20 | 42 ms |
| 480 | 295 ms |
| 1,120 | 3.5 s |
| 5,190 | **38 s** |

Super-linear, and already twice the 17-second landing cap at 5,190 maxima, against frontiers of
18,404 in the target cohort. That is Gate 1B's own stop criterion. S1 is retained as a cheap
diagnostic and a possible seed source, not as a candidate algorithm.

**`core = 0` says nothing against a small interior invariant.** On `SPI`'s loop-1 checkpoint the
core is empty and the forward kernel search finds a verified six-generator certificate.

## Stage S2: forward bounded strategy kernel

**Gate 3A passes: 6 distinct workers with verified kernels at budget <= 64, across 5 families.**

Campaign `_bm-logs.small-invariant-20260830/`, 18-instance anchor cohort, 120-second research cap,
one solver per 8 GiB zero-swap scope. Every kernel below was re-verified from scratch against the
reduced antichain -- forward images recomputed, initial containment rechecked, every generator
checked against the safe vector -- before being counted.

| worker | k | checkpoint maxima | kernel | search | compression |
|---|---:|---:|---:|---:|---:|
| `robot_grid2_2` real | 2 | **1** | **6** | **1 ms** | from the safe set |
| `robot_grid2_2` real | 2 | 18,432 | 6 | 113 ms | 3,072x |
| `finding_nemo_1` real | 5 | 29 | 8 | 8 ms | 3.6x |
| `finding_nemo_1` real | 5 | 16,400 | 8 | 48 ms | 2,050x |
| `finding_nemo_2` real | 5 | 9,586 | 16 | 2.8 s | 599x |
| `lift3` real | 5 | 26,317 | 26 | 5.3 s | 1,012x |
| `SPI` real | 2 | 1 | 6 | 34 ms | from the safe set |
| `TwoCountersDisButA8` unreal-automaton | 2 | 1 | 2 | 26 ms | from the safe set |

**Four of the six are instances Acacia does not currently answer at all.**

| instance | mechanism | Acacia | `ltlsynt` | `max_f` |
|---|---|---:|---:|---:|
| `lift3` | M2 downset | 17.03 s, timeout | 0.028 s | 14,060 |
| `robot_grid2_2` | M2 downset | 17.03 s, timeout | 0.660 s | 9,383 |
| `finding_nemo_1` | M2 downset | 17.03 s, timeout | 0.026 s | 574 |
| `finding_nemo_2` | mixed | 17.03 s, timeout | 0.039 s | 87 |

`robot_grid2_2` is the sharpest case: a verified six-generator winning certificate, found **from
the safe set in one millisecond**, on an instance whose exact frontier reaches 18,432 maxima and
which Acacia currently times out on.

### How much the envelope matters is instance-specific

An earlier reading of the `lift3` result alone suggested the search always needs a refined
envelope. The cohort refutes that. `robot_grid2_2`, `SPI` and `TwoCountersDisButA8` all succeed at
the loop-1 checkpoint, where the region is still the whole safe set and the exact iteration has
contributed nothing.

`lift3` genuinely does need it. Measured directly: at k=5 with the envelope replaced by the safe
set alone, the same search exhausts a 200,064-node budget and 327 million forward applications in
197 seconds and finds nothing, where the exact 26,317-maximum envelope lets it succeed in 31 nodes.

So the envelope is a refinement oracle for the hard cases, not a precondition for the method. A
live probe should therefore run **early and again later**: it costs a millisecond when it works.

### The negative side

The same search refutes as well as proves. When every action of some input class sends the initial
vector outside the exact envelope, the initial vector is not in the winning region at that bound --
because the envelope contains it. On `lift3` at k=2 that fires at loop 6, on a 72-maximum
checkpoint, after **35 forward applications**, while the solver goes on to 26,317 maxima at k=2
before raising the bound. The same shape appears on `lift4` and `lift_unary_enc3`.

This is a fixed-K refutation, never a top-level unrealizability verdict: it licenses exactly the
insufficient-bound transition ordinary Acacia would eventually take.

## Stage U1: fixed-bound root refutation

**Gate U1 passes.**

When every action of some input class sends the initial vector outside the exact region, the
initial vector is not winning at that bound: the region contains the winning region, so a winning
initial vector would have to keep some successor inside it. The refuting input is also a
legitimate critical input -- for the maximum `m >= r_init`, monotonicity and downward closure give
`tau(m) outside X` for every action -- so acting on it does not invent a schedule Acacia would not
have taken.

Measured on the same campaign, comparing where the refutation fires against the largest frontier
the solver itself reached at that bound:

| worker | k | refuted at loop | frontier there | forward applications | solver peak at that k | ratio |
|---|---:|---:|---:|---:|---:|---:|
| `lift6` real | 2 | 3 | 30 | 12,692 | 28,662 | **955x** |
| `lift4` real | 2 | 4 | 48 | 478 | 16,740 | **349x** |
| `lift5` real | 2 | 3 | 20 | 6,444 | 6,915 | **346x** |
| `lift_unary_enc3` real | 2 | 6 | 69 | 6,491 | 19,656 | **285x** |
| `lift3` real | 2 | 6 | 72 | **35** | 8,266 | **115x** |
| `prioritized_arbiter10` real | 5 | 8 | 101 | 4,281,384 | 5,761 | 57x |
| `prioritized_arbiter9` real | 5 | 8 | 82 | 1,767,460 | 3,529 | 43x |

Seven workers across three families -- `lift`, `prioritized_arbiter`, `finding_nemo` -- are refuted
at a frontier at least twice smaller than the solver's own peak, and five of them are current
17-second timeouts. The gate asked for five workers from two families refuted at least two updates
earlier; `lift3` is refuted at loop 6 on 72 maxima where the solver goes on past 8,266.

### The scan must be budgeted, and this is why

The cost is not uniform. `lift3` is refuted in 35 forward applications; `prioritized_arbiter10`
needs 4,281,384. Worse, refuting the *safe set* at loop 1 -- before any CPre has narrowed
anything -- reached 145,769,784 forward applications on `finding_nemo_2`'s unreal-formula worker
and 132,345,097 on `prioritized_arbiter10`'s unreal-automaton worker.

A live probe therefore needs a forward-application budget, not only a node budget, and exhausting
it must return `unknown` rather than anything else. This is the difference between a probe that
pays for itself and one that becomes the new bottleneck.

## Stage U2: the live two-sided probe

`src/solver/local_certificate.hh`, behind `acacia_local_certificate`, default off. At a loop head
it first tries a root refutation, then a bounded forward kernel search, and falls through to the
ordinary picker and CPre when neither returns a certificate.

### Trigger policy

The probe runs on the first loop at the current bound, and on the first crossing of frontier sizes
16, 64, 256, 1024, 4096 and 16384. Each mark latches, so a region oscillating around a mark does
not re-probe. A bound raise resets every mark and the first-loop flag, because the lift re-inflates
the region and a fresh set of crossings follows. It mirrors `antichain_snapshot::observe` so the
offline checkpoints and the live probe fire in the same places.

The marks are not validated by the offline campaign, and it would be circular to claim they are:
the snapshot only records at those same marks, so every measured result could only have been found
at one. What the campaign does show is that each refuted worker's *earliest* refutation sits at a
frontier of 16 to 101 maxima, so probing the low marks acts early. Whether certificates exist
strictly between marks is untested.

### Budgets, and why they are not optional

Generator budget 64, node budget 200,000, forward-application budget 2,000,000, each overridable
by environment variable so a campaign can sweep without a rebuild. The forward-application budget
is the one that matters: the U1 census shows the same scan costs 35 forward applications on
`lift3` and 4,281,384 on `prioritized_arbiter10`, and scanning the safe set at loop 1 reached
145,769,784 on `finding_nemo_2`'s unreal-formula worker. The default admits `lift` at 12,692,
`robot_grid2_2` at 5,320 and `prioritized_arbiter9` at 1,767,460 while excluding the
hundred-million-scale pathologies.

**A root scan that exhausts its budget mid-input reports `budget_exhausted`, never
`root_refuted`.** An untested action might have kept the initial vector inside, so promoting a
partial scan would be a wrong verdict rather than a slow one. The check sits before each forward
application, not after the loop.

### First measurements

Targeted, at a 25-second cap, against the same tree without the probe. Not a panel and not a gate.

| instance | baseline | with probe | `ltlsynt` | verdict |
|---|---|---:|---:|---|
| `robot_grid2_2` | timeout | **0.02 s** | 0.66 s | REALIZABLE, agrees |
| `finding_nemo_1` | timeout | **0.10 s** | 0.026 s | REALIZABLE, agrees |
| `lift3` | timeout | **0.81 s** | 0.010 s | REALIZABLE, agrees |
| `finding_nemo_2` | timeout | **5.35 s** | 0.039 s | REALIZABLE, agrees |

All four are M2 or mixed rows that Acacia does not answer today and that only `ltlsynt` decides.
Every verdict was checked against `ltlsynt` on the same formula and partition.

`lift3` is the clearest demonstration of the two sides working together. Offline, its winning
certificate was only found from a 26,317-maximum envelope, and cost 5.3 seconds of search there.
Live it finishes in 0.81 seconds total, because the refutation fires first at k=2 and raises the
bound without building the k=2 frontier at all; the positive search then succeeds at k=5 from a
region far smaller than the one the offline campaign had to reach.

This is a targeted result on four instances. Whether it lands is decided by G1, G2s, G3 and G4,
which measure what the probe costs on the workers it cannot help.

### Budget tuning, which the gate would otherwise have caught

The first default, 2,000,000 forward applications, fails G1. On a quiet machine the frozen
sentinel `Morning_f2774e0b` goes from 13.70 s to **17.78 s** and crosses the 17-second cap. A sweep
settles it:

| forward-application budget | `Morning_f2774e0b` | wins still found |
|---:|---:|---|
| 20,000 | 14.01 s | `lift3` and `finding_nemo_2` fall back to UNKNOWN |
| **100,000** | **14.69 s** | **all four** |
| 500,000 | 15.02 s | all four |
| 2,000,000 | 17.78 s, over cap | all four |

100,000 is the default. Every verified certificate fits -- `robot_grid2_2` needs 3,834 forward
applications, `finding_nemo_1` 4,314, `finding_nemo_2` 139,143 -- and what it excludes is the tail
that was never going to pay: `prioritized_arbiter` at 1.8 to 4.3 million, and the safe-set scan at
145 million.

One earlier reading recorded `Morning_f1477cc5` improving from 14.04 s to 7.13 s. That was taken
while a compile was running. Re-measured quiet it is 13.23 s to 15.03 s, a regression, and the
earlier figure is withdrawn.

### What G2s caught, and the per-bound budget

G2s failed on `syntcomp24/round_robin_arbiter4`: **+29.4% cycles**, against a 6% per-target
ceiling. The panel geometric mean was a 90.9% improvement at the time -- `lift3` 100x faster,
`finding_nemo_2` 8.9x -- which is exactly the shape the per-target ceiling exists to catch, and
the same instance that rejected the previous sprint's quotient.

The diagnostics said why without a bisection. The real worker ran the probe **six times at a
single bound**, spent the full 100,000 forward applications each time, concluded nothing, and
never raised the bound. The winners look nothing like it: `robot_grid2_2` one run at 3,225
applications, `finding_nemo_1` two at 6,290, `finding_nemo_2` five at 297,649, `lift3` six at
347,382 -- but those six are spread across bounds, not spent at one.

That is the distinction the first budget missed. A per-run budget bounds one search; it does not
bound how many times a worker may re-run a search that cannot succeed. So the budget became
**cumulative per bound, reset when the bound is raised** -- a new bound is a new problem, and both
`lift3` and `finding_nemo_2` win only after a raise, so evidence from the old bound must not
carry over.

The **generator budget drops from 64 to 32**. Every verified certificate in the campaign is
smaller: 6 generators for `robot_grid2_2`, 8 for `finding_nemo_1`, 16 for `finding_nemo_2`, 26 for
`lift3`. A larger cap cannot find a certificate that a smaller one misses here, and it makes every
*failing* search more expensive, because each node rescans every generator against every input and
action.

Neither change works alone, which is worth recording. A cumulative cap by itself cannot separate
the cases -- `lift3` needs more than 200,000 at one bound, and `round_robin_arbiter4` wastes
600,000 at one bound. Lowering the generator cap is what makes 400,000 workable.

| budget | `round_robin_arbiter4` | `lift3` |
|---|---:|---:|
| B=64, per-run 100,000 | +29.4%, over ceiling | REALIZABLE |
| B=32, cumulative 400,000 | **+4.7%** | **REALIZABLE, 0.51 s** |

Measured at `B=32` and `CUM=400,000`, all four wins survive -- `robot_grid2_2` 0.02 s,
`finding_nemo_1` 0.10 s, `lift3` 0.55 s, `finding_nemo_2` 5.50 s -- against a baseline that
answers none of them, and `Morning_f2774e0b` sits at 6.2%, inside the cap.

The same diagnostics surfaced a second waster that the per-bound ceiling also stops:
`finding_nemo_2`'s unreal-automaton worker spent **3,203,392 forward applications over 35 runs**
for nothing.

### Gates

| gate | result |
|---|---|
| G0 | 25/25 Acacia unit, 18/18 Posets, 183 Python; registry validates |
| G1 | **`GATE PASS`**, 40/40 frozen verdicts; PAR-2 101.867 s frozen, 92.224 s without the probe, **90.651 s with it** |
| G2s | first run **`GATE FAIL`** on `round_robin_arbiter4` at +29.4%; re-running with the tuned budgets |
| G3 | pending |
| G4 | pending |

## Stage S3: checkpointed width schedule

Implemented as a control. On `SPI`'s fixed point, widths 1, 2, 4 and 8 all lose the initial vector
-- width 8 even grows to eleven maxima as exact CPre creates new interior meets -- and only the
full width retains it. Deprioritised in favour of the two-sided local search.

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
