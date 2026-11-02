# Strategy-directed frontier order: proof addendum for #361

Reviewed before implementing this change by the sprint's proof reviewer.
This changes exploration order only. It does not change the positive objective,
the sink polarities, move order, exact solver, snapshot compression, verdict
rule, synthesis decline, or resource/cancellation behavior of
[the reviewed reduction](on-the-fly-parity-proof.md).

## Expanded-set invariant

Replace the expanded numeric prefix by an explicit bit per discovered DPA
state. The stable source-ID-to-graph-ID map and discovery vector never reorder.
Let E be precisely the states whose complete outgoing rows have been imported
and validated. Each chosen state must be discovered and outside E; its bit is
set only after the entire row and final source/budget check succeed. The
expanded counter is |E|. All graph edges originate in E, except transient
edges during a row import that must finish or abort before another solve.

Each snapshot copies every exact edge from E, then adds the arm's fixed
frontier self-loop to **every state outside E**, rather than to a numeric
suffix. Raw edge priorities remain append-only in graph edge-storage order,
independently of source-state expansion order, and every snapshot remaps all
of them anew. Thus the original closure proof depends only on E, never on its
being a prefix. Arbitrary frontier order is sound; guidance cannot justify a
verdict, omission of a row edge, or a change to either frontier completion.

## Which strategy exists, and which choices to follow

Spot's public `spot/twaalgos/game.hh` contract and `game.cc` implementation say
the strategy is meaningful only where the vertex owner equals its computed
winner. Entries elsewhere are unspecified; zero can itself be a valid edge
number. Continue to solve globally, as the current arm does.

After an inconclusive snapshot, its initial winner is the **opposite** of the
arm's target. In UNREAL-only mode this is the optimistic system winner;
follow its system-owned choices and retain **all environment-owned choices**.
In REAL-only mode it is the optimistic environment winner; follow its
environment-owned choices and retain **all system-owned choices**. Never
read a putative strategy for the losing target player.

Traverse the current arena from its initial vertex. At a vertex owned by the
initial winner, use the stored strategy only after checking that this vertex
has that winner and that the recorded edge occurs in its outgoing row. At a
vertex owned by the other player, traverse all its outgoing edges without
reading the strategy entry. The exact winning-region contract makes this
traversal stay in the initial winner's region. Stop each path at a frontier
DPA state and collect that state once, in deterministic traversal order.

The pinned Spot explicit and semisymbolic `split_2step` implementations retain
the original automaton states as the first unchanged state IDs; intermediates
are appended (`synthesis.cc`, original-state allocation in both splitters).
Use that mapping only for guidance. If it supplies no usable frontier, the
ordinary discovery-order fallback still progresses; the correctness argument
does not rely on this internal numbering convention.

## Small implementation and progress rule

Use the collected frontier list before the ordinary discovery-order fallback,
preferring the newest discovered eligible state first as a deterministic tie-break
and skipping states already in E. Retain only that list between solves, never an
arena, strategy or snapshot-dependent priority map. Keep the same cumulative
1, 2, 4, ... row budgets; within a batch, consume the saved list and then use
the fallback until the next budget. Do not solve extra games or infer choices
beyond the saved frontier. Every successful expansion increases |E|, so a
finite source is eventually completed in the absence of resource stops.

Expose `--parity-exploration bfs|strategy`, defaulting to `bfs`, so one binary
can compare the saved baseline order against this opt-in heuristic. Include
the choice in the worker's accounting identity. Existing arm names and
default portfolio stay unchanged.

Charge the visited/work/preference vectors to the existing import allowance;
poll budget/source status during traversal and before publishing a verdict.
An unusable strategy entry may discard guidance and use the fallback; it
must never become a partial row, opposite verdict, or reused old solution.
The first solve has no guidance and expands the initial row normally, so this
change cannot help an initial-row BDD/resource decline.

## Predeclared check and stopping rule

Extend the independent synthetic fixture with a graph whose only useful
frontier is outside the numeric prefix, an opponent branch that must not be
pruned, and an unspecified losing-owner strategy slot. Re-run the existing
frontier, late-priority, full-row, cancellation and I/O-timing tests.

Then repeat the same nine load_balancer inputs at 17 s, one solver at a time,
recording exact binary and source identities. Continue this order heuristic
only if it adds at least one sound UNREAL solve and at least one solved input
uses at most 128 expanded rows. Report whether any such solve finishes within
5 s separately. If either condition fails, stop frontier-order tuning;
investigate per-row construction cost/representation rather than adding a
custom parity algorithm. Initial-row resource failures remain a separate
bottleneck regardless of the seven inputs that can begin exploration.
