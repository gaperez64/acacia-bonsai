# Frontier dossier: `collector_v3`

- **family key**: `param:tlsf/collector/parametric/collector_v3.tlsf`
- **failure kind at the boundary**: time_limit
- **points observed in 2026**: 9
- **frozen targets from this family**: `collector_v3_pb_9_pe_.ltl`

## Parameter series

| parameters | B | S | F | B time |
|---|---|---|---|---:|
| n=3 | REALIZABLE | REALIZABLE | REALIZABLE | 0.02 |
| n=5 | REALIZABLE | REALIZABLE | REALIZABLE | 0.05 |
| n=7 | REALIZABLE | REALIZABLE | REALIZABLE | 0.98 |
| n=9 | TIMEOUT | TIMEOUT | TIMEOUT | 60.06 |
| n=10 | TIMEOUT | TIMEOUT | TIMEOUT | 60.08 |
| n=11 | TIMEOUT | TIMEOUT | TIMEOUT | 60.12 |
| n=12 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| n=13 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |
| n=15 | TIMEOUT | TIMEOUT | TIMEOUT | 60.13 |

## Boundary

- largest solved: `collector_v3_pb_7_pe_.ltl` (0.92 s)
- first unsolved: `collector_v3_pb_9_pe_.ltl` (TIMEOUT)

## Worker mechanism at the boundary

| target | aut states | rank coords | actions/pass | workers |
|---|---:|---:|---:|---:|
| `collector_v3_pb_9_pe_.ltl` | 2835 | 10 | 0 | 10 |

`actions/pass` is the cumulative action count the backward fixed point processed, not the size of an action table.

## Forward solver

- solves that B and S do not: 0
- fails where B or S succeed: 0

## Structural conjecture

TODO — not auto-generated. A conjecture produced from the same table it is meant to explain would be a restatement, not a hypothesis.

## Next theorem

TODO.
