# Frontier dossier: `collector_v1`

- **family key**: `param:tlsf/collector/parametric/collector_v1.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 9
- **frozen targets from this family**: `collector_v1_pb_11_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=3 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=5 | REALIZABLE | REALIZABLE | REALIZABLE | 0.04 |
| n=7 | REALIZABLE | REALIZABLE | TIMEOUT | 0.09 |
| n=9 | REALIZABLE | REALIZABLE | TIMEOUT | 2.74 |
| n=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.09 |
| n=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.04 |
| n=13 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=14 | TIMEOUT | TIMEOUT | TIMEOUT | 60.18 |
| n=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.22 |

## Boundary

- largest solved: `collector_v1_pb_9_pe_.ltl` (2.71 s)
- first unsolved: `collector_v1_pb_11_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `collector_v1_pb_11_pe_.ltl` | 15372 | 15371 | 90 | 23 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 2: `collector_v1_pb_7_pe_.ltl`, `collector_v1_pb_9_pe_.ltl`

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
