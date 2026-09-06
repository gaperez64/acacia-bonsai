# Frontier dossier: `full_arbiter_unreal2`

- **family key**: `param:tlsf/full_arbiter_unreal/parametric/full_arbiter_unreal2.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 6
- **frozen targets from this family**: `full_arbiter_unreal2_pb_5_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.03 |
| n=3 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 0.07 |
| n=4 | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 4.80 |
| n=5 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=6 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=7 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |

## Boundary

- largest solved: `full_arbiter_unreal2_pb_4_pe_.ltl` (4.80 s)
- first unsolved: `full_arbiter_unreal2_pb_5_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `full_arbiter_unreal2_pb_5_pe_.ltl` | 43 | 21 | 249408 | 18 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 0

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
