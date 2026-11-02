# One-sided lazy DPA games: soundness obligation (#361)

Source: https://github.com/gaperez64/acacia-bonsai/issues/361 (read 2026-11-02).
Reviewed before solver implementation by the sprint's proof reviewer. This
argument assumes the tlsf-tools DPA recognizes the supplied objective and
Spot's split/solver contracts hold. The existing `allow_unproved` fallback's
sample-based translation validation remains an explicit trust assumption;
this reduction does not prove that construction anew.
The implementation is opt-in; neither arm changes the default portfolio or
creates a helper worker. Start with `unreal:parity:oxidd-otf`, then expose its
dual `real:parity:oxidd-otf` using the same implementation.

## Objective and move order

Use the original **positive** TLSF objective after the existing frontend's
effective-Mealy lowering, with the complete declared input/output partition.
Do not negate, swap players/APs, simplify the objective, or add a timing shift.
The tlsf-tools API supplies a deterministic complete max-odd DPA: the system
wins exactly when the largest infinitely recurring transition priority is odd.
Spot's existing `split_2step` supplies an environment input move followed by a
system output move. Its exact `solve_parity_game` then decides the system's
winning region, as in the existing eager arm. The split must retain all possible
input/output choices, including unused declarations.

## Published prefix and frontier completion

Maintain a fixed initial state, stable source IDs, exact imported guards and
raw priorities, and a set E of expanded DPA states. For each state in E, every
outgoing edge is present. A state enters E **only after** `tlsf_dpa_successors`
returns a whole nonempty row and every label, target and priority is imported
and validated: guards are nonempty, disjoint, and their union is true. All
destinations are interned before publication. No partially imported row is
ever solved. A failed API call or validation aborts this attempt as UNKNOWN;
the API explicitly forbids continuing a declined context.

Each snapshot copies the exact rows of E. Each discovered state outside E
instead has one true-labelled self-loop:

* REAL-only: priority 0, so entering the frontier loses for the system.
* UNREAL-only: priority 1, so entering the frontier loses for the environment.

Thus every snapshot is a complete deterministic positive max-odd automaton,
which can use the existing split and exact solver without a new game algorithm.
Expanded states retain their original rows, even if they have frontier targets.

## One-sided conclusion and strategy closure

For REAL-only, return REAL only if the system wins the initial state in the
frontier-losing snapshot. Its positional winning strategy cannot reach any
frontier state against any environment choices: a reachable frontier permits
only the losing absorbing loop. All resulting plays therefore stay in E,
where rows and priorities agree with the full game, and satisfy the original
objective. The strategy extends arbitrarily outside its reachable region.
Consequently the full game is REAL.

For UNREAL-only, return UNREAL only if the environment wins the initial state
in the snapshot whose frontier loses for the environment. By the same closure
argument its strategy avoids the frontier against **all** system responses.
Its plays remain in E and violate the positive objective in the full game.
Consequently the full game is UNREAL. In either arm, the opposite snapshot
answer is inconclusive while frontier states remain, and never published as a
verdict. Once all reachable rows are expanded, the snapshot is the full game;
the opposite answer still causes a one-sided decline rather than crossing the
arm's advertised polarity.

## Stable priorities and repeated solving

Store each source transition's raw priority permanently and check it against
the creation-time API bound. Never infer the parity convention from the
currently observed maximum. Sink priorities are always 0 or 1 as above.
If Spot's acceptance width requires compression, build a fresh snapshot map
over its used raw priorities including the sink, in increasing order: preserve
parity and merge only adjacent priorities of the same parity. For any infinite
play, the maximum recurring mapped priority is the mapped maximum recurring
raw priority and has the same parity. Stored priorities do not change as the
frontier grows; maps and game solver results are snapshot-local, with no
incremental solver state carried between maps. The same fixed game semantics
therefore applies across expansions even if compact acceptance numbers differ.
Decline UNKNOWN if the current snapshot still exceeds Spot's width.

Expand complete rows and re-solve after cumulative exploration budgets
1, 2, 4, 8, ...; also solve a newly completed game. Begin with deterministic
breadth-first discovery. Strategy-directed frontier selection is optional
later and cannot affect soundness, provided every published row remains whole.
Use the existing exact Spot solver each time; do not implement incremental
Zielonka or add hidden workers. Check the shared budget before and after each
non-cooperatively cancellable Spot solve. A deadline/cancellation during that
solve suppresses its result. Parent process-group supervision remains the
hard cancellation boundary.

## Synthesis and resource outcomes

Initially both arms are decision-only and explicitly decline `-s` before DPA
construction or any verdict/controller rendezvous. A partial-game controller
could be sound only after preserving its closed winning strategy cone and
passing the usual target-frame conversion/export. That work is deliberately
deferred; the existing eager arm continues to support synthesis.

Creation/expansion/import failures, unsupported syntax, capacity/priority
limits, allocation failures, deadline and cancellation are UNKNOWN, never
opposite verdicts or evidence that a row was fully explored. Retain the
existing worker AS/RSS limits and one shared construction/exploration deadline.
The exploration budget controls when to solve; it is not a reason to publish
a result. A source failure after earlier snapshots does not revive a cached
answer. All conclusive results must pass a final budget/status check.

## Required checks and first measurement

Test both polarities against the existing eager/Spot routes on small positive
objectives, input/output-dependent and constant formulas, and TLSF timing
variants. Exercise losing/winning frontier completions before full expansion,
raw priority gaps/compression, full-row failures, deadlines/cancellation,
explicit synthesis decline, and parser opt-in/default preservation. Check that
neither arm ever emits its opposite verdict. Root first measures UNREAL-only
on the nine load_balancer inputs, then owns the matched 17/60/120 s campaigns.
