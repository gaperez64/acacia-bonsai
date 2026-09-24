# Generic exact GR(1) reduction census

Actual TLSF inputs, no parameter overrides. One serial reduction attempt per input, bounded at 11.333333 s (2/3 of the 17 s campaign cap). This measures **reducibility under the lift slice**, not actual Stage A admission.

- Inputs: **1,524**
- Reducible: **572**
- Declined: **952**

| Reduction time | Reducible inputs |
|---|---:|
| (0, 0.1] s | 406 |
| (0.1, 0.5] s | 108 |
| (0.5, 1] s | 24 |
| (1, 2] s | 18 |
| (2, 5] s | 10 |
| (5, 11.3333] s | 6 |
| > 11.3333 s | 0 |

| Population | min | median | P90 | P99 | max |
|---|---:|---:|---:|---:|---:|
| Reducible | 0.060 s | 0.075 s | 0.518 s | 5.008 s | 9.444 s |
| All attempts | 0.057 s | 0.077 s | 11.368 s | 11.474 s | 11.738 s |

Decline reasons:

- `unsupported_or_failed`: 681
- `budget_exhausted`: 271

## Actual admission under the unchanged wrapper gate

The wrapper caps eligibility at `min(1 s, 5% of cap)`. We reran exact reduction for every input at each gate, serially. The default lift slice is longer than either gate. Admission means exact reduction finished within this gate; it does not imply a later solver/checker verdict.

| Outer cap | Eligibility gate | Admitted | Declined |
|---:|---:|---:|---:|
| 17 s | 0.85 s | 527 | 997 |
| 60 s | 1.00 s | 534 | 990 |

Relative to the 572 slice-reducible inputs, 45 miss the 17 s gate and 38 miss the 60 s gate. The 60 s set adds 7 inputs; the 17 s set has 0 unique inputs.

Exact observed sets: `generic-admission-17.list` and `generic-admission-60.list`; per-input status, elapsed time, and decline reason: `generic-admission.tsv`.

Rows: `generic-census.tsv`. The time distribution is observed wall time for the reduction attempt, including process cleanup; the tool deadline is 11.333333 s, so timeout rows can exceed it slightly. Censored timeouts are included in the all-attempts distribution.
