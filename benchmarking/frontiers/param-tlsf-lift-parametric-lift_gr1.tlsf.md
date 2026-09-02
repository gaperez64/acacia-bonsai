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

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
