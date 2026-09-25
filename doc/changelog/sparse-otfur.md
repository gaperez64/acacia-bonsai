# A sparse losing upset in v2.4.2

The `otf_sparse_formula` unrealizability arm first shipped in v2.4.2
(2026-09-09; `v2.4.2:config/acacia-presets.json:358-362,486-492`).
The v2.4.1 tag lacks the sparse rank/search files; v2.4.2 contains both.
Its frozen-graph `spot-guarded-sparse` backend runs `spot_lazy_game::Search`
(`v2.4.2:src/solver/solve_game_impl.hh:190-200,214-225`), whose losing region
is a `LossSet` of sparse rank generators
(`v2.4.2:src/solver/spot_lazy_game.hh:548-566`).

## What one generator stores

`Rank` is `SparseForwardRank<int32_t>`; each `Entry` is a
`std::pair<StateId, Value>`. The first field is an automaton state ID
(a rank coordinate), and the second is that state's bounded rank value;
it is not an edge or an input/output letter
(`v2.4.2:src/solver/spot_lazy_game.hh:19`,
`v2.4.2:src/solver/sparse_forward_rank.hh:14-19`,
`v2.4.2:src/solver/spot_state_ids.hh:14`).
The constructor sorts pairs, merges duplicate state IDs by maximum value,
and drops entries valued `-1`. Every missing coordinate therefore reads as
`-1`, the least rank value
(`v2.4.2:src/solver/sparse_forward_rank.hh:19-37`).

For example, with four numeric states `q0` through `q3` and bound `K = 3`,
`{(0, 1), (2, 0)}` represents `[1, -1, 0, -1]` in state-ID order.
This is the pair sequence stored by `Rank {{{0, 1}, {2, 0}}, 3}`.
For a frozen graph with Boolean threshold 4, these four coordinates have
safe cap `K - 1 = 2`; the saturated value 3 is still a representable but
unsafe value (`v2.4.2:src/solver/spot_lazy_game.hh:142-148,310-316`;
`v2.4.2:src/solver/sparse_forward_rank.hh:19-23`).

## Inserting five losing ranks

Suppose the solver proves these ranks losing in the order shown. The sparse
column lists the stored `(StateId, Value)` pairs; the dense column expands
all four coordinates. All five ranks are safe in this example.

| Order | Rank | Sparse pairs | Equivalent dense vector |
|---:|:---:|:---|:---|
| 1 | B | `{(0, 2), (2, 0)}` | `[2, -1, 0, -1]` |
| 2 | C | `{(1, 0), (3, 1)}` | `[-1, 0, -1, 1]` |
| 3 | D | `{(0, 1), (2, 1)}` | `[1, -1, 1, -1]` |
| 4 | A | `{(0, 1), (2, 0)}` | `[1, -1, 0, -1]` |
| 5 | E | `{(0, 0), (1, 0)}` | `[0, 0, -1, -1]` |

The upset is `U = {r in {-1, 0, 1, 2, 3}^4 : A <= r or C <= r or E <= r}`,
where `<=` is componentwise rank order. Inserting A removes B and D because
`A <= B` and `A <= D`; C and E are incomparable with A and each other.
The stored `LossSet` vector follows `[B] -> [B,C] -> [B,C,D] -> [C,A] -> [C,A,E]`.
The set of minimal generators is `{A, C, E}`, while the vector preserves survivor
order and appends the new rank. Its final order is `[C, A, E]`: sparse
`{{(1, 0), (3, 1)}, {(0, 1), (2, 0)}, {(0, 0), (1, 0)}}`, dense
`{[-1, 0, -1, 1], [1, -1, 0, -1], [0, 0, -1, -1]}`.
`LossSet::insert` rejects a rank already above a generator, removes old generators
above a new one, then appends it (`v2.4.2:src/solver/spot_lazy_game.hh:550-565`).

## Why this works

For `g <= r`, only `g`'s stored pairs need checking: an absent coordinate
in `g` is `-1` and cannot violate the comparison; a pair in `g` missing
from `r` fails it. Thus an absent `-1` in a generator imposes no lower
bound on that coordinate of its upward closure. `leq` implements the merge
(`v2.4.2:src/solver/sparse_forward_rank.hh:33-46`). In v2.4.2,
`enqueue_loss` inserts a generator when it enlarges `LossSet` and queues the
loss. After each pending loss, `propagate_losses` scans already interned,
non-losing nodes against the whole antichain, even if that loss added no generator.
Future nodes are checked for subsumption during expansion; the search does not
eagerly mark every mathematical rank above a generator
(`v2.4.2:src/solver/spot_lazy_game.hh:909-929,944-969,976-983`).
The one-scan-on-growth change came later in `aab54de2` (2026-09-10).
Sparse storage avoids allocating an entry for every inactive state.
Its benefit shrinks when most coordinates
are active: pairs, sorting and merge comparisons then add work relative to a
dense vector (`v2.4.2:src/solver/sparse_forward_rank.hh:25-31,40-46`; inference).

**VERDICT:** In v2.4.2, the sparse losing upset retains minimal generators in
survivor order `[C, A, E]` and scans the whole antichain after each pending loss.
