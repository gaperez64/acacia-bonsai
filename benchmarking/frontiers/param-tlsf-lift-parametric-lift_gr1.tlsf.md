# Frontier dossier: `lift_gr1`

- **family key**: `param:tlsf/lift/parametric/lift_gr1.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 6
- **frozen targets from this family**: `lift_gr1_pb_3_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.03 |
| n=3 | TIMEOUT | TIMEOUT | REALIZABLE | 60.08 |
| n=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.32 |
| n=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |
| n=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.27 |

## Boundary

- largest solved: `lift_gr1_pb_2_pe_.ltl` (0.03 s)
- first unsolved: `lift_gr1_pb_3_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `lift_gr1_pb_3_pe_.ltl` | 225 | 91 | 115278358 | 42 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 1: `lift_gr1_pb_3_pe_.ltl`
- fails where B or S succeed: 0

## Structural conjecture

**Forward buys exactly one parameter step here, and the step it buys is the whole story.**

`n=2` is solved by everything in 0.025 s. `n=3` is **forward-only** — 0.42 s, against a
backward timeout at 60 s. From `n=7` upward nothing solves it. The 2026 set contains no
`n=4,5,6`, so "one step" is one *observed* step, not necessarily one parameter.

This family carried the sprint's most instructive mistake. Its `actions_seen` of **115,278,358**
was read as an action-table size, and used to predict the forward solver could not possibly
help. The counter is cumulative work per CPre call, so a huge value means the backward fixed
point ground through an enormous number of maxima — evidence *for* trying a reachable search,
not against it. The prediction was exactly inverted, and the measurement refuted it.

The conjecture is that `lift_gr1` at `n=3` has a small controlled invariant that the backward
computation only reaches after enumerating a very large permissive region, and that the
insufficient-bound refutations at low `K` are also cheap forward — the same asymmetry the
local-certificate sprint found, where a root refutation cost 35 forward applications while the
backward frontier grew to 26,317 maxima.

## Next theorem

1. Extract the forward winning certificate at `n=3` and measure its generator count against
   the backward peak antichain. If it is small, this family has a succinct-strategy theorem.
2. Establish why `n≥7` defeats forward as well. If the reachable set is still small there, the
   obstruction is the bound schedule rather than the game.
3. Obtain the missing `n=4,5,6` from the parametric template. A one-step frontier over a series
   that skips from 3 to 7 is under-sampled, and the shape of the cliff is currently unknown.
