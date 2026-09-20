# Dual-frontier replay study

Task: `delta`. Rows: 25.

## Process outcomes

| mode | outcome | rows |
|---|---|---:|
| - | ok | 24 |
| - | skipped_incomplete_capture | 1 |

## Replay statuses

| mode | status | rows |
|---|---|---:|
| - | - | 1 |
| - | work_limit | 24 |

## Delta census by family and frontier bucket

Censored rows remain in the denominator; medians use only exactly certified rows.

| family | before bucket | events | complete | censored | censorship | median B_before/Max_before | median B_after/Max_after | median B_delta/Max_before | median unchanged maxima |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| arbiter_on_inpchange | 1024-4095 | 10 | 0 | 10 | 100.0% | - | - | - | - |
| control_06 | <1024 | 1 | 0 | 1 | 100.0% | - | - | - | - |
| prioritized_arbiter | 1024-4095 | 1 | 0 | 1 | 100.0% | - | - | - | - |
| round_robin_arbiter | 1024-4095 | 1 | 0 | 1 | 100.0% | - | - | - | - |
| round_robin_arbiter | 16384+ | 4 | 0 | 4 | 100.0% | - | - | - | - |
| round_robin_arbiter | 4096-16383 | 5 | 0 | 5 | 100.0% | - | - | - | - |
| simple_arbiter_with_hints | 1024-4095 | 1 | 0 | 1 | 100.0% | - | - | - | - |
| simple_arbiter_with_hints | 4096-16383 | 2 | 0 | 2 | 100.0% | - | - | - | - |

## Gate C0

**FAIL** — 0 complete large events across 0 families; 0/0 have delta <= before/8; median unchanged maxima 0.000000; semantic mismatches 0.

- [ ] at least 3 unrelated families
- [ ] at least 20 complete events with before_maxima >= 1024
- [ ] delta <= before/8 on at least half
- [ ] median unchanged-maxima fraction >= 0.5
- [ ] all qualifying deltas exactly certified
- [x] zero semantic mismatches

Decision: Gate C0 does not admit a lazy/subtractive prototype; stop at the measured census.
