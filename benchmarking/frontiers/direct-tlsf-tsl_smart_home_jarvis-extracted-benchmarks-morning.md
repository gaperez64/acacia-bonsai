# Frontier dossier: `Morning`

- **family key**: `direct:tlsf/tsl_smart_home_jarvis/extracted-benchmarks:Morning`
- **failure kind at the boundary**: memory_limit
- **points observed in 2026**: 16
- **frozen targets from this family**: `Morning2_06e9cad4.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| Morning2_06e9cad4.ltl | TIMEOUT | MEMOUT | TIMEOUT | 60.38 |
| Morning2_1cf58fc1.ltl | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 4.99 |
| Morning2_2c5b09da.ltl | TIMEOUT | TIMEOUT | TIMEOUT | 60.07 |
| Morning2_407db2cc.ltl | MEMOUT | CRASH | TIMEOUT | 49.48 |
| Morning2_4d4ca5c4.ltl | REALIZABLE | REALIZABLE | REALIZABLE | 0.03 |
| Morning2_63831f8c.ltl | TIMEOUT | MEMOUT | TIMEOUT | 60.09 |
| Morning2_68c86764.ltl | MEMOUT | MEMOUT | TIMEOUT | 57.89 |
| Morning2_9cac58d3.ltl | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| Morning2_d65ed84e.ltl | CRASH | MEMOUT | TIMEOUT | 45.98 |
| Morning_14a3b3a2.ltl | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 15.77 |
| Morning_4b5e6eaa.ltl | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 15.61 |
| Morning_88c5c1c3.ltl | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| Morning_9cac58d3.ltl | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| Morning_c92eb242.ltl | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 15.65 |
| Morning_f1477cc5.ltl | UNREALIZABLE | UNREALIZABLE | UNREALIZABLE | 14.47 |
| Morning_f2774e0b.ltl | UNREALIZABLE | UNREALIZABLE | TIMEOUT | 15.20 |

## Boundary

- largest solved: `Morning_f2774e0b.ltl` (14.06 s)
- first unsolved: `Morning2_06e9cad4.ltl` (MEMOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `Morning2_06e9cad4.ltl` | 198 | 183 | 0 | 17 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 4: `Morning_14a3b3a2.ltl`, `Morning_4b5e6eaa.ltl`, `Morning_c92eb242.ltl`, `Morning_f2774e0b.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
