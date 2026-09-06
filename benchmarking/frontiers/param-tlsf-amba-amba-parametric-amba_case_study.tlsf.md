# Frontier dossier: `amba_case_study`

- **family key**: `param:tlsf/amba/amba/parametric/amba_case_study.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 3
- **frozen targets from this family**: `amba_case_study_pb_3_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=2 | REALIZABLE | REALIZABLE | TIMEOUT | 0.93 |
| n=3 | TIMEOUT | TIMEOUT | TIMEOUT | 60.17 |
| n=4 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |

## Boundary

- largest solved: `amba_case_study_pb_2_pe_.ltl` (0.93 s)
- first unsolved: `amba_case_study_pb_3_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `amba_case_study_pb_3_pe_.ltl` | 104 | 20 | 253248 | 17 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 1: `amba_case_study_pb_2_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
