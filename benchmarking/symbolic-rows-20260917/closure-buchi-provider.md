# Closure/cursor transition Büchi provider

`acacia::closure_buchi::Provider` is a standalone infinite-word LTL translator.
`create(formula, dict, options)` returns a provider or a typed `Failure`.
It syntactically lowers Boolean abbreviations, F/G, W/M and negation into NNF
over literals, constants, And/Or, X, U and R. In particular,
`a W b = b R (a | b)` and `a M b = b U (a & b)`; negation uses duality.
PSL/SERE, strong next and all other operators are declined. Inputs already
lowered from finite-trace semantics cannot be distinguished from ordinary LTL;
the caller must supply the worker's infinite-word formula.

Normalization memoizes input DAG nodes and polarities and retains Spot's
hash-consed formula nodes. It does not call an automaton translator or a
semantic simplifier. Node, depth and estimated storage limits also apply to
normalization. Factory return fixes the normalized closure, every until ID and
all AP registrations, and discovers exactly `({formula}, 0)`, with no rows.

## Local expansion and fairness

An underlying state is a sorted, duplicate-free set S of closure IDs. For one
step a branch holds current work, current-step `done`, a guard g, next
obligations T and postponed untils P. Literals restrict g; false kills a branch;
And adds every operand and Or chooses an operand. X adds only to T.
For `u = a U b`, choose b now, or a now with u in T and P. For `r = a R b`,
require b now and choose a now or r in T. The `done` set prevents duplicate
current expansion; it never removes obligations from T or markers from P.
Exhausted branches yield `(g,T,P)`. Only equal `(T,P)` may merge their guards.

The language argument is:

- Each raw branch denotes a conjunction of its current guard and next-step
  obligations, with a marker precisely for each chosen until postponement.
- An infinite run satisfying all fairness obligations cannot justify an until
  forever solely by postponing it. Unfolding the local obligations therefore
  gives a word satisfying the initial formula. Release uses its greatest
  fixpoint, so infinite release continuation itself needs no extra fairness.
- For a satisfying word, choose Boolean witnesses and discharge each active
  until when its witness is reached. Shared occurrences may use the same
  witness. This produces a branch sequence satisfying all fairness obligations.
- A complete round-robin cursor wraps infinitely often exactly when every
  fixed fairness obligation occurs infinitely often: each wrap witnesses all
  obligations in order, and infinitely recurring obligations cannot leave the
  cursor stuck forever at any index.

Here fairness obligation u occurs on an edge iff u is absent from P, including
when u is inactive. This is why an unchosen disjunct creates no outstanding
eventuality. For m untils, `(S,j)` advances by **at most one** when `U[j]` is
absent from P, and accepts exactly on a satisfied `j == m-1`. With m=0 every
existing edge accepts. The representation uses one ordinary transition Büchi
set regardless of the number of untils; P is a vector of closure IDs, not a
Spot acceptance bitset. Empty S has a true continuation with this cursor rule.
Contradictory S has no edges and is never totalized.

Thus the Büchi language equals the worker formula's language. **Language
equivalence does not imply equal minimum successful K** in bounded games.

## Identity, ownership and publication

Ordered maps intern S using exact vector comparison, avoiding hash-based
quotienting altogether. A second exact map interns `(S,j)`. Both arenas append
without renumbering. Equal S with different j denotes different states.
Spot states hash and compare their context identity plus local ID; clones keep
the context alive. States from separate provider instances never compare equal.
The context owns dictionary registrations even when a Spot state outlives the
adapter. Guards and all published spans remain valid while the context lives.

`request_row(StateId)` returns `variant<span<const Edge>, Failure>`, where an
edge has `destination`, `condition` (a BDD), and `accepting`. Underlying rows
are shared across cursors. A request stages the full raw row and cursor row,
merging cursor guards only for equal destination **and** accepting flag. Both
new caches publish together through nonthrowing moves after the final check.
Discovery interns destinations but does not request their rows. On failure,
already discovered IDs may remain; neither a partial row nor a newly staged
raw row is cached. Previously complete rows remain immutable. Failed requests
can be retried after a per-provider test hook is removed.

Explicit branch/step, guard-operation/live-BDD, state, row/edge and estimated
memory limits, cancellation and injected faults return typed failures. C++
allocation and unexpected exceptions are caught as well. Every failure means
UNKNOWN to a solver. Because `twa::succ_iter` cannot return a typed result, the
adapter throws `AdapterFailure` before exposing an iterator. It must be used
inside a checked consumer boundary, such as `SpotRows::row`. The existing
`BuddyErrors` boundary is reused: an unrecoverable native BuDDy error exits 2
instead of returning a potentially poisoned guard; the worker parent must map
that exit or a signal to UNKNOWN. Instances share BuDDy and are single-threaded.

Counters record cumulative attempted generation work, including failed work;
complete-row/edge counts describe only published data. Times are integer
nanoseconds. Factory time includes normalization, which is also recorded
separately. Raw and cursor generation times are disjoint. First-row time runs
from factory entry through first publication, including caller idle time.
Retained bytes estimate provider containers/formula nodes, not allocator
overhead or shared BDD nodes. It is a resource guard, not a measured RSS value.

## Later RowStore integration

Keep one provider alive across K attempts and use the existing worker BDD
dictionary. APs are registered before any row and guards use exactly those
dictionary variables; do not recreate them in another dictionary. Adapt the
row span to `CompleteRankRow`, or use `GenericTransitionBuchi {provider}` with
`SpotRows`, which already accepts the adapter. In the latter case SpotRows IDs
are its own arena: map through `state_id(canonical_state)` or structural state
identity, never assume its enumeration order equals the provider's. The
provider's `materialize()` does preserve provider IDs.

Initialize rank `{initial_state(): 0}`. Use `accepting`/mark 0 as the transition
increment, all numeric coordinates, saturation at K and missing coordinates
-1. Do not apply accepting-initial-state logic, Boolean-state preprocessing,
SCC preprocessing, or another acceptance cursor. In particular, avoid
`LazyBuchiView` wrapping, its `detail::CursorState` cast in RowStore telemetry,
TAA-specific counters and its underlying-provider ownership assumptions.
Use this provider's counters and structural accessors instead. Keep the fresh
certificate reconstruction and all K-dependent search state in the later task.

`materialize()` is an explicit exploration policy over `request_row`; it uses
the same translation and caches as lazy use and returns no partial graph on
failure. Unit tests evaluate original formulas independently on exhaustive
finite lassos, compare both row materialization and the twa adapter with Spot's
translator using `are_equivalent`, inject every fault position, and check
locality. The eventuality conjunction stress test deliberately demonstrates
exponential first-row growth; this component makes no performance admission
claim. Finite-K games, certificate integration and worker/replay wiring remain
the subsequent integration task.
