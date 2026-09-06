# Frontier dossier: `full_arbiter_unreal1`

- **family key**: `param:tlsf/full_arbiter_unreal/parametric/full_arbiter_unreal1.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 46
- **frozen targets from this family**: `full_arbiter_unreal1_pb_3_8_pe_.ltl`, `full_arbiter_unreal1_pb_4_6_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=2, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=2, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=2, u=8 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=9 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.04 |
| n=2, u=10 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.06 |
| n=2, u=11 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.10 |
| n=2, u=12 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.20 |
| n=2, u=13 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.45 |
| n=2, u=14 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.93 |
| n=2, u=15 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.90 |
| n=2, u=16 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 4.25 |
| n=2, u=18 | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 21.22 |
| n=2, u=20 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |
| n=2, u=21 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=2, u=22 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=2, u=24 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=2, u=26 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |
| n=3, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=3, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=3, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.02 |
| n=3, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.07 |
| n=3, u=5 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.24 |
| n=3, u=6 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 1.45 |
| n=3, u=7 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 9.46 |
| n=3, u=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=3, u=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.16 |
| n=3, u=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=3, u=13 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=14 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=3, u=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=4, u=1 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=4, u=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.05 |
| n=4, u=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.20 |
| n=4, u=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 2.73 |
| n=4, u=5 | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 53.30 |
| n=4, u=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.09 |
| n=4, u=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=4, u=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.08 |
| n=6, u=8 | TIMEOUT | TIMEOUT | TIMEOUT | 60.09 |

## Boundary

- largest solved: `full_arbiter_unreal1_pb_4_5_pe_.ltl` (53.30 s)
- first unsolved: `full_arbiter_unreal1_pb_2_20_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `full_arbiter_unreal1_pb_3_8_pe_.ltl` | 40 | 7 | 14120 | 18 |
| `full_arbiter_unreal1_pb_4_6_pe_.ltl` | 52 | 9 | 25968 | 18 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 2: `full_arbiter_unreal1_pb_2_18_pe_.ltl`, `full_arbiter_unreal1_pb_4_5_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
