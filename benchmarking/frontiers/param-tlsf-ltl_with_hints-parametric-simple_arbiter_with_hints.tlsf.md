# Frontier dossier: `simple_arbiter_with_hints`

- **family key**: `param:tlsf/ltl_with_hints/parametric/simple_arbiter_with_hints.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 7
- **frozen targets from this family**: `simple_arbiter_with_hints_pb_8_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.01 |
| n=4 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=6 | REALIZABLE | REALIZABLE | TIMEOUT | 2.40 |
| n=8 | TIMEOUT | TIMEOUT | UNKNOWN | 60.09 |
| n=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |
| n=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |

## Boundary

- largest solved: `simple_arbiter_with_hints_pb_6_pe_.ltl` (2.26 s)
- first unsolved: `simple_arbiter_with_hints_pb_8_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `simple_arbiter_with_hints_pb_8_pe_.ltl` | 1607 | 9 | 22272 | 21 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 1: `simple_arbiter_with_hints_pb_6_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
