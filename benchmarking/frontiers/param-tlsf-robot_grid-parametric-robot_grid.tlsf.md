# Frontier dossier: `robot_grid`

- **family key**: `param:tlsf/robot_grid/parametric/robot_grid.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 9
- **frozen targets from this family**: `robot_grid_pb_3_3_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| xN=2, yN=2 | REALIZABLE | REALIZABLE | REALIZABLE | 0.26 |
| xN=3, yN=3 | TIMEOUT | TIMEOUT | REALIZABLE | 60.08 |
| xN=4, yN=4 | TIMEOUT | TIMEOUT | REALIZABLE | 60.05 |
| xN=5, yN=1 | TIMEOUT | REALIZABLE | REALIZABLE | 60.09 |
| xN=5, yN=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| xN=6, yN=1 | TIMEOUT | TIMEOUT | REALIZABLE | 60.11 |
| xN=6, yN=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.33 |
| xN=7, yN=7 | MEMOUT | MEMOUT | MEMOUT | 10.36 |
| xN=8, yN=8 | MEMOUT | MEMOUT | MEMOUT | 13.01 |

## Boundary

- largest solved: `robot_grid_pb_5_1_pe_.ltl` (0.07 s)
- first unsolved: `robot_grid_pb_3_3_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `robot_grid_pb_3_3_pe_.ltl` | 10514 | 82 | 721 | 19 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 3: `robot_grid_pb_3_3_pe_.ltl`, `robot_grid_pb_4_4_pe_.ltl`, `robot_grid_pb_6_1_pe_.ltl`
- fails where B or S succeed: 0

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
