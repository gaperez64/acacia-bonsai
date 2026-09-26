# A sparse losing upset in v2.4.2

A rank stores sorted `(StateId, Value)` pairs. Value `-1` is the implicit bottom (“not reached”) and is never stored; duplicate states merge by maximum value. The example uses states `q0` through `q11` and bound `K = 3`. Each dense vector has 12 coordinates in state-ID order.

`x.leq(y)` iff every stored pair `(q, v)` of `x` has a stored pair `(q, w)` in `y` with the same state `q` and `v <= w` — pairs of `y` on other states are unconstrained, because `x`'s missing coordinate is `-1`.

| Insert | Sparse rank | Dense equivalent |
|:---:|:---|:---|
| B | `{(0, 2), (7, 0)}` | `[2, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1]` |
| C | `{(4, 1)}` | `[-1, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1]` |
| D | `{(0, 1), (7, 1)}` | `[1, -1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1]` |
| A | `{(0, 1)}` | `[1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]` |
| E | `{(4, 0), (9, 1)}` | `[-1, -1, -1, -1, 0, -1, -1, -1, -1, 1, -1, -1]` |

`A <= B`: `(0, 1) <= (0, 2)`, while B's `(7, 0)` is unconstrained by A. B and D are incomparable: B's `(0, 2) > (0, 1)`, while D's `(7, 1) > (7, 0)`. C and E are incomparable: C's `(4, 1) > (4, 0)`, while E's `(9, 1)` has no match in C.

`LossSet` rejects a new rank already above a stored rank. Otherwise it removes stored ranks above the new one, preserves survivor order, and appends the new rank. Insert the five losing ranks in table order:

1. **B:** remove none; the set is empty. Stored order: `[B]`.
2. **C:** remove none; C's `(4, 1)` is absent from B, and B's `(0, 2)` is absent from C. Stored order: `[B, C]`.
3. **D:** remove none; B and D cross at `(0, 2) > (0, 1)` and `(7, 1) > (7, 0)`; C's `(4, 1)` is absent from D and D's `(0, 1)` is absent from C. Stored order: `[B, C, D]`.
4. **A:** remove B and D: A's `(0, 1)` is at most B's `(0, 2)` and D's `(0, 1)`; C's `(4, 1)` is absent from A and A's `(0, 1)` is absent from C. Stored order: `[C, A]`.
5. **E:** remove none; E's `(9, 1)` is absent from C, C's `(4, 1) > (4, 0)`, and E's `(4, 0)` and A's `(0, 1)` are absent from each other. Stored order: `[C, A, E]`.

The final antichain is the set `{A, C, E}`. Its stored vector order is `[C, A, E]`: sparse `[{(4, 1)}, {(0, 1)}, {(4, 0), (9, 1)}]`; dense `[[-1, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1], [1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1], [-1, -1, -1, -1, 0, -1, -1, -1, -1, 1, -1, -1]]`.

## Why this works

Comparing singleton A with B visits A's one stored pair and looks up state 0 in B. A's 11 unreached states need no stored pairs or separate comparisons. Removing B and D keeps the same upset because every rank above either one is also above A. Sparse storage is not smaller when most states are reached.

**Code:** `v2.4.2:src/solver/sparse_forward_rank.hh:18-46`; `v2.4.2:src/solver/spot_lazy_game.hh:550-565,909-928`.
