# Frontier dossier: `arbiter_with_cancel`

- **family key**: `param:tlsf/arbiters_zoo/parametric/arbiter_with_cancel.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 10
- **frozen targets from this family**: `arbiter_with_cancel_pb_6_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=1 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=3 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=4 | REALIZABLE | REALIZABLE | REALIZABLE | 0.08 |
| n=5 | REALIZABLE | REALIZABLE | TIMEOUT | 1.32 |
| n=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.09 |
| n=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| n=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.07 |

## Boundary

- largest solved: `arbiter_with_cancel_pb_5_pe_.ltl` (1.32 s)
- first unsolved: `arbiter_with_cancel_pb_6_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `arbiter_with_cancel_pb_6_pe_.ltl` | 1120 | 13 | 5952 | 20 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 1: `arbiter_with_cancel_pb_5_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
