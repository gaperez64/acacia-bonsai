# Frontier dossier: `load_balancer_unreal1`

- **family key**: `param:tlsf/load_balancer_unreal/parametric/load_balancer_unreal1.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 51
- **frozen targets from this family**: `load_balancer_unreal1_pb_5_6_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=2, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=2, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=8 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=9 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=10 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=11 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=2, u=12 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.08 |
| n=2, u=13 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.15 |
| n=2, u=14 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.35 |
| n=2, u=15 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.73 |
| n=2, u=16 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.75 |
| n=3, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=3, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=3, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=8 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=3, u=9 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.09 |
| n=3, u=10 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.16 |
| n=3, u=11 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.27 |
| n=3, u=12 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.57 |
| n=4, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=4, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=4, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=4, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.06 |
| n=4, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.17 |
| n=4, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.08 |
| n=4, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 7.42 |
| n=4, u=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=4, u=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=4, u=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=5, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.07 |
| n=5, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.10 |
| n=5, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.63 |
| n=5, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 13.49 |
| n=5, u=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=5, u=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=6, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.16 |
| n=6, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.23 |
| n=6, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.62 |
| n=6, u=4 | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 22.25 |
| n=6, u=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |

## Boundary

- largest solved: `load_balancer_unreal1_pb_6_4_pe_.ltl` (22.16 s)
- first unsolved: `load_balancer_unreal1_pb_4_8_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `load_balancer_unreal1_pb_5_6_pe_.ltl` | 210 | 17 | 50110 | 18 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 1: `load_balancer_unreal1_pb_6_4_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
