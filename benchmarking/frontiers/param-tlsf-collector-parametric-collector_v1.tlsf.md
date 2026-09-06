# Frontier dossier: `collector_v1`

- **family key**: `param:tlsf/collector/parametric/collector_v1.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 9
- **frozen targets from this family**: `collector_v1_pb_11_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=3 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=5 | REALIZABLE | REALIZABLE | REALIZABLE | 0.04 |
| n=7 | REALIZABLE | REALIZABLE | TIMEOUT | 0.09 |
| n=9 | REALIZABLE | REALIZABLE | TIMEOUT | 2.74 |
| n=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.09 |
| n=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.04 |
| n=13 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=14 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |

## Boundary

- largest solved: `collector_v1_pb_9_pe_.ltl` (2.71 s)
- first unsolved: `collector_v1_pb_11_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `collector_v1_pb_11_pe_.ltl` | 15372 | 15371 | 90 | 23 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 2: `collector_v1_pb_7_pe_.ltl`, `collector_v1_pb_9_pe_.ltl`

## Structural conjecture

**This is the family where the forward solver is strictly worse, and its shape says why.**

Backward solves `n=3,5,7,9` (the last in 2.71 s) and stops at `n=11`. Forward solves only
`n=3,5` and **times out on `n=7` and `n=9`, which backward answers in 0.09 s and 2.71 s**.

The distinguishing measurement: at `n=11` the worker reports **15,372 automaton states and
15,371 numeric rank coordinates** — the rank dimension is the automaton size minus one, so
essentially every automaton state is a counting coordinate and the Boolean tail is empty.

The forward solver interns environment nodes on the **complete coordinate vector**. With a rank
dimension of that size, two distinct reachable configurations almost never share an exact
vector, so interning collapses nothing and the reachable set grows with the exploration rather
than being folded by it. The backward downset, by contrast, stores an antichain of maxima and
shares structure across the whole region — exactly the representation that pays when the
dimension is large and the values are small.

The conjecture is therefore that **rank dimension, not automaton size or action count, predicts
where forward loses**: the forward representation has no sharing, so it is beaten wherever the
downset's sharing is doing the work.

## Next theorem

1. Test the prediction on the other frozen targets: rank dimension should correlate with
   forward failure better than automaton states or `actions_seen` do. `robot_grid` (rank 82,
   forward wins) and this family (rank 15,371, forward loses) are the two extremes and agree
   with it.
2. If the prediction holds it gives a **routing rule** — send high-rank-dimension instances to
   the backward solver — which is worth more than either solver alone and is the concrete form
   the portfolio conclusion should take.
3. Ask whether the 15,371-coordinate encoding is necessary, or an artefact of how the collector
   specification is translated. A smaller encoding would change the family's difficulty for
   both backends.
