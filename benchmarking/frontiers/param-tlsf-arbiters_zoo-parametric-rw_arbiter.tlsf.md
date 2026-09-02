# Frontier dossier: `rw_arbiter`

- **family key**: `param:tlsf/arbiters_zoo/parametric/rw_arbiter.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 5
- **frozen targets from this family**: `rw_arbiter_pb_4_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=1 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.04 |
| n=3 | REALIZABLE | REALIZABLE | TIMEOUT | 0.77 |
| n=4 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |

## Boundary

- largest solved: `rw_arbiter_pb_3_pe_.ltl` (0.77 s)
- first unsolved: `rw_arbiter_pb_4_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `rw_arbiter_pb_4_pe_.ltl` | 39 | 17 | 17664 | 17 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 1: `rw_arbiter_pb_3_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
