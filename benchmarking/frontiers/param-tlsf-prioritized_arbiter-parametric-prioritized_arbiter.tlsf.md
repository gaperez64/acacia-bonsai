# Frontier dossier: `prioritized_arbiter`

- **family key**: `param:tlsf/prioritized_arbiter/parametric/prioritized_arbiter.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 11
- **frozen targets from this family**: `prioritized_arbiter_pb_7_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=1 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.03 |
| n=3 | REALIZABLE | REALIZABLE | REALIZABLE | 0.04 |
| n=4 | REALIZABLE | REALIZABLE | REALIZABLE | 0.05 |
| n=5 | REALIZABLE | REALIZABLE | REALIZABLE | 0.04 |
| n=6 | REALIZABLE | REALIZABLE | TIMEOUT | 0.41 |
| n=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.07 |
| n=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.01 |
| n=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.05 |
| n=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.07 |
| n=12 | TIMEOUT | TIMEOUT | UNKNOWN | 60.08 |

## Boundary

- largest solved: `prioritized_arbiter_pb_6_pe_.ltl` (0.41 s)
- first unsolved: `prioritized_arbiter_pb_7_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `prioritized_arbiter_pb_7_pe_.ltl` | 47 | 43 | 30100344 | 42 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 1: `prioritized_arbiter_pb_6_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
