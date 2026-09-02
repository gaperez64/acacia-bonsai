# Frontier dossier: `chomp`

- **family key**: `param:tlsf/chomp_game/parametric/chomp.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 15
- **frozen targets from this family**: `chomp_pb_4_2_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| N=2, M=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.08 |
| N=2, M=3 | REALIZABLE | REALIZABLE | REALIZABLE | 3.26 |
| N=2, M=4 | TIMEOUT | TIMEOUT | TIMEOUT | 60.33 |
| N=2, M=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| N=2, M=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.07 |
| N=3, M=2 | REALIZABLE | REALIZABLE | REALIZABLE | 3.21 |
| N=3, M=3 | TIMEOUT | TIMEOUT | TIMEOUT | 60.06 |
| N=3, M=4 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| N=3, M=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| N=3, M=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| N=4, M=2 | TIMEOUT | TIMEOUT | TIMEOUT | 60.33 |
| N=4, M=3 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| N=4, M=4 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| N=4, M=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| N=4, M=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |

## Boundary

- largest solved: `chomp_pb_3_2_pe_.ltl` (3.20 s)
- first unsolved: `chomp_pb_2_4_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `chomp_pb_4_2_pe_.ltl` | 75 | 22 | 30070 | 21 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 0

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
