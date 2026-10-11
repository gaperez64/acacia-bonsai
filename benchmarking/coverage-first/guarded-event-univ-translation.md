# Guarded eventual/universal translation (2026-10-10)

The shared `create_automaton()` path now counts distinct F/U/M nodes in the unabbreviated
negative-normal-form DAG of the actual worker objective. Only when that count exceeds
`spot::acc_cond::mark_t::max_accsets()` does it run Spot's language-preserving simplification
with `favor_event_univ=true`. The installed width is 64; the code reads it from Spot rather
than setting a new threshold. The simplifier uses the existing `tls-impl=1` syntactic rules
without language-containment checks. The original worker formula, alphabets, timing
transformations, translator options, output preference, Büchi target, backend and independent
certificate checker are retained. No polarity step or production option was added.

The guard covers REAL, both UNREAL timing routes, degenerate alphabets, derived weakening jobs,
synthesis checks and callers using this shared translation helper. At or below the width it
passes the original formula to the original translator. When enabled, phase records contain
`translation-event-univ` entry/completion plus `distinct_promises`, `acceptance_set_width`
and `simplified_promises` metrics. Observations do not enable the rewrite.

The DAG count is a structural eligibility condition, not an exact simulation of Spot's
acceptance allocator. Ordinary simplification can already remove promises, and the guarded
rewrite can still leave too many. Neither exception avoidance nor guard eligibility is a
realizability verdict.

## Frontend-only census

All 1,524 entries of `tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv` converted and parsed
successfully. The separate, uninstalled `event-univ-census` tool builds no automaton or game
and solves nothing. It reports raw frontend counts and prospective shipping decision-mode
counts after the existing Spot realizability simplifier, decomposition and worker timing
transformations. As in the actual runner, a sole decomposition result is discarded in favor
of the original objective; multiple REAL components receive the existing component-local pass.
Eligibility does not predict whether bypass, weakening, another worker or cancellation will
prevent a particular translation from being reached. Derived weakening objectives and
synthesis-mode translations are not part of this census.

| Count frame | Inputs exceeding 64 |
|---|---:|
| Raw frontend, positive objective | 31 |
| Raw frontend, negative objective | 35 |
| Existing decision simplification, positive objective | 28 |
| Existing decision simplification, negative objective | 35 |
| Prospective REAL translation components | 33 |
| Prospective formula-UNREAL translation components | 28 |
| Prospective automaton-UNREAL translation components | 28 |
| Union of prospective shipping translation guards | 40 (2.62%) |

The two UNREAL sets agree. The union, expressed as one-based data-row ordinals excluding the
source-map header, is:

```
4 5 6 7 8 9 10 11 12 34 412 438 439 470 878 888 890 892 894 897
909 911 913 915 932 942 959 993 1008 1016 1019 1020 1198 1201 1205
1308 1460 1464 1466 1468
```

`build_scratch/guarded-event-univ/firing-rows.tsv` records each arm's eligibility separately;
`census.tsv` records every input and count, and `census-summary.json` contains the complete
sets. `identity.json` binds the source map by SHA-256. An initial census incorrectly used
Spot's sole rewritten component. Its output is retained as
`census-before-single-component-fix.tsv`; the corrected tool has a regression self-test.

## Four shipping diagnostics

Exactly one invocation per requested source ran sequentially, using the shipping default
portfolio, its existing internal threads and a 60-second monotonic invocation deadline.
The existing task scope had `memory.max=8589934592` and `memory.swap.max=0`. It also retained
an inherited `memory.high=5368709120`: this soft 5 GiB limit throttled the last diagnostic.
It was removed for subsequent correctness checks. No `systemd-run`, campaign, paired timing
screen, network access or commit was used.

| Source-map row | Result | Recorded guard entries | Completed translation | Acceptance-limit errors |
|---:|---|---:|---|---:|
| 1201 | UNREALIZABLE, exit 1 | 2 | UNREAL: 2 states / 1 set | 0 |
| 1205 | UNREALIZABLE, exit 1 | 2 | UNREAL: 2 states / 1 set | 0 |
| 1198 | UNREALIZABLE, exit 1 | 4 | REAL and UNREAL: 2 states / 1 set | 0 |
| 1308 | UNKNOWN at deadline, exit 2 | 4 | REAL: 16,578 states / 1 set | 2 |

Each conclusive diagnostic contains a completed independent certificate check and zero
reported dropped phase records. Portfolio cancellation explains incomplete records in losing
workers. On row 1308, the REAL rewrite reduces 65 promises to 2 and reaches action construction;
both UNREAL objectives retain 65 promises and hit the width limit. The native worker declines
its structural construction bound. No worker verifies a verdict. The parent reports deadline
expiration; cleanup finishes 61.88 seconds after launch, with no launcher safety kill. There
is no OOM or hard memory-limit event. This diagnostic does not establish behavior without the
inherited soft throttle and is not a fresh timing comparison.

Shipping binary SHA-256:
`8a24f5ea0a26d60984e6c20425397b91b68f3710e40ab5d7709c26e9e2d5ae87`.
Source base: `5e065b64982d2f7d10f987189046d8b2f6f0538d`;
tlsf-tools: `c31109e45ee026858ca244a15ceb3fa75a1f5d7f`; local Spot 2.16 / 64 sets.
Build arguments select the first current `docker_default` preset, release compiler profile,
LTO and Python bindings. A separate `debugoptimized` build keeps assertions for unit tests.
All compilation uses `-j 3`. Raw logs, commands, hashes, CPU affinity, scope settings and
records remain in ignored `build_scratch/guarded-event-univ/`.

## Correctness and handback

The new generated tests check normalization, shared nodes, negative and abbreviated operators,
the exact width boundary, renamed inputs, reuse after a guarded translation, and preservation
of the bound worker formula. Both state-based and transition-based acceptance are tested.
Across 80 paired cases (20 above the guard), Spot's `are_equivalent` confirms automaton language
agreement; independent deterministic-parity games agree on realizability with the option off
and on, including 40 REAL and 40 UNREAL results. At or below the width, 222 fresh-process
comparisons produce byte-identical HOA output under Small, Any and Deterministic preferences.
The phase-marker regression also passes. The master reference is the unmodified translation
body from the pinned base, compiled only into an uninstalled test executable.

The checked full unit suite passes all 87 tests with `--num-processes 1`. The initial
`--num-processes 3` run passed 86 and failed the existing `unreal-weakening-records` test:
38 dropped phase records made its census incomplete. The full serial rerun passes that test
with complete delivery. Both logs are retained; no producer or assertion was changed to hide
the failure. The explicit anti-hardcoding check passes 96 tests with one skipped. Ruff,
configuration consistency (49 options, one documented divergence) and `git diff --check` pass.
The FULL CI-style command `python3 -m pytest tests/pytest/ -q` passes: 2,299 passed,
40 skipped and 23 subtests passed, using the freshly built shipping Python bindings.
An additional scratch probe checks 300 fresh-process nested formulas under all three
preferences; every automaton is byte-identical to the master reference.
Performance admission, full-corpus solves and issue closure remain the driver's work.

The original cgroup limits were restored after validation. Cleanup removed 370 regenerated
object files (975,189,336 bytes) and temporary correctness-test/cache copies. Binaries, runtime
libraries, configuration and raw records remain. Rebuilding removed intermediates requires
`-j 3`. No file was staged or committed.
