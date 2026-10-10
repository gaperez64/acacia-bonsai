# MONA input streaming (#200)

The MONA traversal now calls the actioner with one complete input class, then
releases its decoded sets. The actioner keeps its original sorted-set insertion,
first-input deduplication and extraction order. Output traversal and semantic
quotienting are unchanged. Forward, backward and equivariant callers consume the
stream; representative filtering preserves each retained input BDD. Existing
callers can still request the materialized range explicitly. No option, threshold,
budget, picker, RNG call or benchmark-dependent solver logic was added.

The test freezes both old implementations from b9b95ef6, independently of the
new traversal and constructor. On 64 generated games and two repeated-relation
controls, for plain and semantic MONA, it compares serialized action tables,
flat action IDs, forward/backward applications, action splices, selections and
all four pickers' persistent RNG state byte-for-byte. Generated backward and
forward solves compare their execution traces, winning bounds and regions.
A counted transition container verifies that decoded endpoints are released
and that their peak fits one complete input class.

## Memory diagnostics

Each invocation ran alone through the existing `benchlib.run_systemd_scope`
collector: an 8 GiB workload cgroup with no swap inside a delegated user scope.
The small lifecycle owner holds the cgroup after exit so peak/events are read
before deletion. These are memory diagnostics; admission remains with the
driver's paired screens. All generated runs completed without OOM or timeout.

The controls have 128 states, three output propositions and parity over 10 or
12 input propositions. Many decoded relations repeat across the input paths.
Both modes use the same test executable, with the frozen old traversal/actioner
or the streaming traversal/actioner selected only in this test program.

| Input paths | Worker peak RSS, old | Worker peak RSS, streamed | Decoded endpoint peak, old | Decoded endpoint peak, streamed | Final action payload, both |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 218.3 MiB | 18.2 MiB | 194.5 MiB | 196 KiB | 413.3 KiB |
| 4,096 | 820.9 MiB | 18.1 MiB | 778 MiB | 196 KiB | 413.3 KiB |

Worker RSS comes from `getrusage(RUSAGE_SELF).ru_maxrss` inside the test process.
The respective scope peaks were 214.5 -> 13.6 MiB and 818.7 -> 13.7 MiB.
RSS and cgroup charges are different measurements, including shared-page
accounting. Live decoded endpoint bytes at completion were zero for streaming.
Endpoint counts exclude unused vector capacity and allocator overhead. Action
payload estimates count vector capacities and exclude allocator/node overhead.

The source map resolves `robot_grid_pb_8_8_pe_.ltl` to
`/home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus/robot_grid_pb_8_8_pe_.tlsf`.
The optimized diagnostic uses `otf_sparse_formula` settings, diagnostics enabled,
LTO off and exactly `--arms real:small:forward`, with a 60 s cap.

It **still MEMOUTs**: scope peak 8,589,934,592 bytes; one `oom_kill`; maximum
reaped-process RSS 8,580,128,768 bytes (7.991 GiB); exit 2 / UNKNOWN.
The worker was SIGKILLed in **action-construction**, input class 1,320, after
1,319 completed classes. Decoded payload peak was only **2,820 bytes estimated**.
The retained action payload had already reached **8,497,246,776 bytes (7.914 GiB)**.
The census still reports 16,578 states, 302,426 edges, 4,096 input paths and
102,901 output paths. No search or complete action table existed at termination.
The complete corpus table cannot be measured under this limit; its partial
retained payload is reported separately, without treating it as a final total.
No phase packets were dropped and the writer reported no delivery failures.

The first corpus probe omitted creation of the phase-record directory. Its
writer exited, leaving valid OOM/RSS observations but no precise attribution.
That incomplete capture is retained as `corpus-forward`; one corrective repeat,
`corpus-forward-valid`, supplies the phase evidence above. This was the only
repeat and used the same binary, arm, cap and memory limit.

## Validation and reproduction

All builds used `-j 3`. The checked build used `debugoptimized`, no LTO, Python
bindings, MONA, rank-bucketed vectors, TLSF, forward solving and guarded backends.
Diagnostics were off: the existing observation-off fixture uses integer ranks
and does not compile with diagnostics enabled. The separate optimized diagnostic
build uses the recorded preset arguments with diagnostics on and LTO off.

- `meson test -C build_scratch/i200/checked --suite unit --num-processes 3 --no-rebuild`: 58 passed.
- `/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 -m pytest tests/pytest/ -q`: 2,210 passed, 40 skipped, 23 subtests passed.
- `/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 -m pytest tests/pytest/test_no_benchmark_hardcoding.py -q`: 96 passed, one optional-corpus skip.
- `git diff --check`: passed. No tracked Python or tlsf-tools source was changed.

Pytest used `/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3`, with
`PYTHONPATH=/usr/local/lib64/python3.14/site-packages:$PWD/build_scratch/i200/checked/src/python`
and the corresponding Python extension directory in `LD_LIBRARY_PATH`.
Setup exported `PKG_CONFIG_PATH=/usr/local/lib/pkgconfig` and used offline wraps.
The existing cached yyjson tree was linked locally. Source HEAD is b9b95ef6;
Spot is 2.16; GCC is 16.2.1. No commits were made.

Raw diagnostics, scope identities/events, binary/source SHA-256 manifests and
summaries remain in `build_scratch/i200/_bm-logs.memory/`. The exact setup command
is in `build_scratch/i200/diagnostic-setup-command.json`, and the generic scratch
collector is `build_scratch/i200/diagnose.py`. For generated controls it invokes:

```
python3 build_scratch/i200/diagnose.py --label generated-10-after --cap 180 -- \
  build_scratch/i200/diagnostic/tests/mona-streaming-test --memory-stress streamed 10 128
```

Use `materialized` for the old reference and input width 12 for the larger control.
The recorded corpus command invokes the same collector with `--cap 60`,
`ACACIA_DIAG=1`, and the exact arm/source above. These commands must run outside
the sandbox so the delegated user cgroup can be created.

Paired admission, full-corpus campaigns and durable evidence publication belong
to the external driver. This change adds streaming and validates its memory
boundary; it does not establish a coverage or runtime gain, or close the remaining
corpus action-table allocation problem.

## Changed files

- `src/ios_precomputers/mona.hh`, `src/ios_precomputers/prepare.hh`
- `src/actioners/standard.hh`
- `src/solver/k_bounded_safety_aut.hh`
- `src/solver/forward_k_bounded_safety_aut.hh`
- `src/solver/equivariant_k_bounded_safety_aut.hh`
- `tests/mona_streaming_test.cc`, `tests/mona_materialized_reference.hh`, `tests/meson.build`
- `benchmarking/RESULTS.md`, `benchmarking/mona-streaming.md`

After validation, 700.4 MiB of scratch objects and unused test targets were removed.
Validation logs/configuration metadata, the diagnostic executables, the new test
executable and the checked Python bindings remain available. Recompile with
`meson compile -C build_scratch/i200/checked -j 3` before rerunning the entire unit suite.
