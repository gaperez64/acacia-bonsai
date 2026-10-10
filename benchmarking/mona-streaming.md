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


## Morning2s timeout peak investigation

Offline comparison of `i200-adj-r{1,2,3}` and the earlier 80-instance `i200-60`
screen identifies earlier allocation of the final action table during decoding.
This is a real increase in the memory needed at the 60 s cutoff, not evidence
of additional search. No unintended decoded-buffer retention, duplicate final
table, or lazy-precomputer buffer was found. No solver change was made.

The frozen executables are LEGP0 (`54ddfb83`, SHA-256 `cdb3e51f5bd274a1137033eed8640dc38b54522d36135af9e750e29f867aba75`)
and LEGI200 (`ff2caea8`, SHA-256 `48d5d3e9ed0b77c40f87cb47254fb537e86828e28361d88becf55b7907902005`).
Memory sidecars agree with primary rows; all six repeated invocations time out
without OOM. GiB below means bytes / 2^30.

| Repetition | Scope peak, old / streamed (GiB) | Streamed complete input classes | Streamed decoded endpoints | Retained action payload (bytes) | Forward worker last sampled peak RSS (KiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 2.739 / 3.582 | 19 | 32,372,556 | 1,070,716,352 | 1,270,548 |
| 2 | 2.686 / 3.342 | 18 | 27,776,268 | 918,890,400 | 1,110,248 |
| 3 | 2.734 / 3.576 | 19 | 32,372,556 | 1,070,716,352 | 1,270,584 |

In both binaries, REAL backward/forward workers remain in translation; native
OxiDD remains in trusted preparation. Formula-UNREAL sparse search reaches K=2
on the same 352-state/8,004-edge graph, with 339 numeric and 13 Boolean dimensions.
Its interrupted search publishes no rows/query/game-state totals: they are
unknown, not zero. The affected automaton-UNREAL forward worker has exactly
348 states, 8,000 edges, 335 numeric and 13 Boolean dimensions in every run.
Neither binary reaches picker preparation, a K attempt, or search in that worker.

The maximum recorded per-worker high-water marks are below (r1/r2/r3, KiB).
They are checkpoint observations, not terminal peaks; the two REAL translators
and OxiDD publish only early samples. Blank graph/count fields in the raw table
remain unknown. No search occurred in workers 0, 1, 3 or 4.

| Worker | Route | Terminal phase, both | Old sampled peak RSS | Streamed sampled peak RSS |
| --- | --- | --- | ---: | ---: |
| 0 | REAL backward | translation | 16,124 / 16,108 / 16,116 | 16,124 / 16,112 / 16,128 |
| 1 | REAL forward | translation | 16,124 / 16,044 / 16,116 | 16,124 / 16,112 / 16,128 |
| 2 | formula-UNREAL sparse | search, K=2 | 26,492 / 26,540 / 26,484 | 26,620 / 26,544 / 26,624 |
| 3 | automaton-UNREAL forward | IO decoding | 177,904 / 177,780 / 177,856 | 1,270,548 / 1,110,248 / 1,270,584 |
| 4 | native OxiDD | trusted preparation | 1,812 / 1,812 / 1,812 | 1,816 / 1,812 / 1,816 |

The sparse worker's translation takes 8.482/9.310, 9.571/9.881 and
9.304/10.263 s (old/streamed); preprocessing and Booleanization finish within
another 0.008 s. Its started K remains 2. Extra progress within that censored K
cannot be measured from the available packets and must not be inferred from
its startup RSS sample.

Forward translation takes 30.348/30.182, 31.604/31.699 and 31.081/31.088 s
(old/streamed). IO decoding begins at elapsed 40.077/38.924, 40.833/40.723 and
40.306/39.846 s. Master has no completed decode and no action construction at
termination. Streaming interleaves action construction with decoding. Its
maximum one-class decoded payload is 53,297,180 bytes in all repetitions.
Retained action payload and RSS grow together at classes 4, 5, 7, 11, 12, 14
and 19. Repeated classes leave retained payload unchanged; pending duplicate
actions are released normally. The screen reaches class 20 and retains
1,222,611,936 action bytes, consistent with the same mechanism.

The action representation allocates a vector for every state for every decoded
output set, even when that state's row is empty. Each large unique input has
12,288 output sets: its 348-state row-vector arrays alone occupy 102,629,376
bytes, before endpoints, versus roughly 53 MB for its compact decoded sets.
At class 19, eight retained input lists (one small and seven large) contain
86,881 output actions; their row-vector arrays account for 725,630,112 bytes,
about 68% of the estimated action payload. This representation predates
streaming; streaming makes those arrays resident earlier.

The current input's decoded list dies on return from its callback. The actioner
retains only the sorted, deduplicated table, which the old constructor would
also retain; extraction moves its contents without copying nested vectors.
`prepare.hh` holds an automaton shared pointer and three BDD handles, not a
materialized range. MONA's residual-root set is untouched in the plain,
diagnostics-disabled build. The MONA BDD also remains live until streaming
finishes; previously its lifetime ended before action construction began.

At the identical decoded prefix of 19 classes, summing recorded per-class
capacities projects the old decoded payload to 374,460,408 bytes. Streaming has
1,070,716,352 action bytes plus the current 53,166,108 decoded bytes before its
callback returns: 749,422,052 more accounted live bytes at that checkpoint.
At class 18 the corresponding difference is 597,764,356 bytes. These are
source-based payload projections with verified LP64 object sizes, not measured
old RSS or evidence of the old binary's exact cutoff prefix. Both omit allocator
nodes, BDD manager allocations and other workers. The projections explain the
direction and scale of the scope increase without assuming additional search.

Recorded phase RSS is a high-water mark sampled on stage completion. SIGKILL
prevents final samples in censored stages; per-worker final peaks and interrupted
search counts cannot be reconstructed from the scope maximum. In particular,
master's 177,904/177,780/177,856 KiB forward checkpoints precede decoding and
must not be compared as if they were final peaks. No measured equal-progress
Morning2s RSS comparison is claimed.

Classification: preparation-phase overlap / earlier final-table allocation,
with a real fixed-deadline peak regression, rather than a decoded-buffer leak
or extra search progress. This diagnosis does not establish performance admission
or justify closing the remaining large-action-table problem. Zero new diagnostic
solver runs were needed. Revalidated: 58 unit tests; full pytest 2,210 passed,
40 skipped, 23 subtests passed; hardcoding guard 96 passed, one skip.

Raw offline tables, input digests, source-layout check, reproduction script and
validation logs are in `build_scratch/i200-investigation/_bm-logs.analysis/`
and `build_scratch/i200-investigation/analyze.py`. Original rows and phases stay
unchanged in `/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.legacy/`.
