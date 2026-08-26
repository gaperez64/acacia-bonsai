# Zero-tail versus bare-vector study

## Decision

Use the bare `Vector` as the default. The controlled study does not reproduce
the previously reported catastrophic bare-vector regression: exact twins
execute the same solver work and the same dominant machine-code kernel. The
later memory-limit follow-up also gives the two types identical outcomes and
8 GiB peaks. The zero-length wrapper therefore does not justify a permanent
production type or source-level branch. The ordinary build also stops enabling
Posets' `x_and_bitset` component; it remains available only through the
explicit compile-all-components developer configuration.
The study is closed. Its harness, `benchmarking/state_vector_tail_study.py`,
was removed once the decision landed; recover it from the history of this file
if the tail representation is ever revisited.

## Controlled twins and protocol

Both variants were built from Acacia `e5b1d3b2571955b8a1397052ca0e5643c105e029`
with Posets `7562564163741d7378d4bbacd7d6d5e7b856d20d`. The only solver-source
difference was the state alias in the solver instantiation. GCC 16.1.1,
Meson 1.11.2, and Ninja 1.13 were used on an Intel i7-11850H. Runs alternated
variant order, used one user-systemd scope per solver, 8 GiB RAM, no swap, a
20-second deadline, five repetitions, and `perf stat` hardware counters.

| build | zero-tail SHA-256 / bytes | bare SHA-256 / bytes |
|---|---|---|
| release + LTO | `99d883eb8de4...` / 454,432 | `a26788b73bda...` / 453,816 |
| release, no LTO | `e3c884a671a9...` / 495,376 | `0349d60485bb...` / 496,120 |

The clean release/LTO builds took 59.87 s and 60.24 s respectively and peaked
at 586,804 KiB and 586,752 KiB. Thus the wrapper is not a meaningful compile
cost after static sizing was removed.

No separate always-512-bit build was run. The old shipping matrix already
contained a 512-bit specialization as the largest of nine tail widths, but its
coverage/timing cannot isolate that one type. Hardwiring it would also be a
different representation from both exact twins: small states would carry an
unused fixed tail and large states would still split at 512 bits. The follow-up
protocol explicitly excluded that speculative bucket, and the later decision
to keep only the bare vector removes the reason to add it now.

## Five-by-20-second results

All 30 LTO pairs timed out under both variants. The table reports the median
within-pair bare/zero ratio; for capped pairs, counters measure work retired in
the same wall-time budget, not time to completion.

| target | LTO cycles | LTO instructions | no-LTO cycles | no-LTO instructions |
|---|---:|---:|---:|---:|
| SYNTCOMP21 round-robin 4 | 1.0037 | 1.0020 | 0.9900 | 1.0065 |
| SYNTCOMP24 buffer 5 | 1.0004 | 0.9939 | 0.9780 | 0.9760 |
| SYNTCOMP24 hints 6 | 1.0031 | 0.9551 | 1.0362 | 0.9984 |
| SYNTCOMP24 round-robin 4 | 1.0021 | 0.9943 | 1.0063 | 1.0149 |
| SYNTCOMP25 buffer PB 5 | 0.9959 | 0.9949 | 1.0051 | 1.0026 |
| SYNTCOMP25 robot-resource | 1.0006 | 0.9902 | 1.0257 | 0.9994 |

In the no-LTO campaign, five targets again timed out 5/5 under both variants.
`robot-resource-1d-unreal` solved 5/5 under both: the median was 16.888 s for
zero-tail and 17.171 s for bare, a 1.69% bare regression. Retired instructions
were 0.06% lower for bare while cycles were 2.57% higher, so frequency and
thermal variation explain more of that wall-time delta than extra work.

A diagnostics-enabled paired run reached identical solver checkpoints and
exact work counts. On `hints6`, both performed 30 loops, three K attempts,
1,856 actions, and 18,287,364 meets; bare completed in 18.604 s and zero-tail
in 18.955 s. The round-robin and robot paths likewise matched their loop,
action, and meet counts. Diagnostics change code generation and are therefore
directional evidence only.

## Memory-limit follow-up

The final full-panel rerun placed every invocation in its own 8 GiB, zero-swap
cgroup. Bare vector reached that limit on `robot_grid6_6` and `robot_grid7_7`,
where the older aggregate zero-tail campaign had recorded one timeout and one
unknown. To distinguish a representation regression from a harness-label
difference, the release/LTO exact twins above were rerun in alternating order
for five 20-second repetitions on each target. The harness recorded cgroup
`MemoryPeak`; hardware counters were disabled for this memory-focused pass.

| target | zero-tail outcomes / median limit time | bare outcomes / median limit time | median peak, both |
|---|---:|---:|---:|
| `robot_grid6_6` | 5/5 resource / 8.63 s | 5/5 resource / 8.35 s | 8 GiB |
| `robot_grid7_7` | 5/5 resource / 14.93 s | 5/5 resource / 15.08 s | 8 GiB |

All 20 samples hit exactly 8,589,934,592 bytes. The small time-to-limit
differences reverse direction across the two targets; they do not show a
zero-tail advantage. The full-panel classification difference was caused by
the newer per-invocation resource isolation and strict OOM detection, not by
collapsing the wrapper to its underlying vector. Raw samples and hashes live
under `_bm-logs.final-v1-current-1d48a15f-20260825/tail-memory-5x20`.

## Profile and disassembly

A 20-second, 499 Hz DWARF call-graph profile of the LTO `hints6` run gave the
same shape:

| self cycles | zero-tail | bare |
|---|---:|---:|
| `vector_backed::insert` | 45.15% | 44.39% |
| `generic_partial_order` | 44.71% | 45.25% |
| critical input picker | 3.31% | 3.41% |
| LTL worker | 2.72% | 2.68% |
| BDD node creation | 1.13% | 1.09% |

The 192-byte `generic_partial_order` routine—the dominant inner comparison—has
byte-for-byte identical machine code in both binaries (normalized opcode hash
`b856ab3b90179acc116c53762e3212eba8cb75b892d807188c4395e5e08a6524`).
It performs the same AVX-512 `vpcmpleb` loop.

The enclosing `vector_backed::insert` is 1,303 bytes for zero-tail and 1,239
bytes for bare (328 versus 316 disassembly lines, including continuation and
alignment lines). The substantive difference is a small move/alias guard in
the zero-tail wrapper path; the comparison loops and call to
`generic_partial_order` are the same. Sampling places the time in those common
comparison loops, not in the wrapper-only move sequence.

This demystifies the earlier apparent 119% regression: exact twins execute the
same hot kernel and the same solver work. The earlier binaries/campaign did
not isolate the representation alias well enough for that claim. The bare type
is now the production default; the one no-LTO 1.69% timing delta is retained as
directional thermal/code-layout noise rather than treated as a wrapper benefit.
A future optimization effort should target the common `generic_partial_order`
/ `vector_backed::insert` comparison path, which accounts for about 90% of
cycles.
