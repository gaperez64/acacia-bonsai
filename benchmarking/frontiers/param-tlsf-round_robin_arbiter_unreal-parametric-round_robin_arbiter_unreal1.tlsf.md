# Frontier dossier: `round_robin_arbiter_unreal1`

- **family key**: `param:tlsf/round_robin_arbiter_unreal/parametric/round_robin_arbiter_unreal1.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 50
- **frozen targets from this family**: `round_robin_arbiter_unreal1_pb_3_9_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=8 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=2, u=9 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=10 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=2, u=11 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.08 |
| n=2, u=12 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.13 |
| n=2, u=13 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.25 |
| n=2, u=14 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.54 |
| n=2, u=15 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.32 |
| n=2, u=16 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 2.64 |
| n=3, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=3, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=3, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.17 |
| n=3, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.03 |
| n=3, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 7.05 |
| n=3, u=8 | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 57.62 |
| n=3, u=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=3, u=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=3, u=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=13 | TIMEOUT | TIMEOUT | TIMEOUT | 60.16 |
| n=3, u=14 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=16 | TIMEOUT | TIMEOUT | TIMEOUT | 60.16 |
| n=3, u=20 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=22 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=50 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=4, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.09 |
| n=4, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.16 |
| n=4, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.75 |
| n=4, u=5 | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 35.75 |
| n=4, u=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| n=4, u=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=4, u=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=4, u=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| n=4, u=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| n=4, u=13 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=14 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=4, u=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |

## Boundary

- largest solved: `round_robin_arbiter_unreal1_pb_4_5_pe_.ltl` (35.75 s)
- first unsolved: `round_robin_arbiter_unreal1_pb_3_9_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `round_robin_arbiter_unreal1_pb_3_9_pe_.ltl` | 755 | 99 | 1288 | 17 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 2: `round_robin_arbiter_unreal1_pb_3_8_pe_.ltl`, `round_robin_arbiter_unreal1_pb_4_5_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
