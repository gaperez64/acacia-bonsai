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

## Opt-in expanded translation policy (2026-10-11)

`--event-univ-policy guarded|expanded` defaults to `guarded`, retaining the master translation
input and phase records, with only an additional policy metric when the guard fires. The explicit
runtime argument reaches the existing portfolio workers, component and weakening runners,
degenerate-alphabet translations, and synthesis translations/checks. The worker objective remains
bound to the original formula; only its local translation copy can change.

Above Spot's acceptance width, both policies use the existing rewrite. At or below the width,
`expanded` computes `r = favor_event_univ(f)` and `r0` with the same syntactic simplifier options
except `favor_event_univ=false`. It passes `r` precisely when `r != r0`; otherwise it passes the
original `f`, including when ordinary simplification alone would change it. Translator options,
acceptance target, output preference, preprocessing, input timing, K policy, worker membership and
proof checking remain unchanged.

Spot's installed `tl_simplifier` uses syntactic rules here, with containment checks disabled, and
exposes no cooperative-cancellation API. This is opt-in synchronous work, without a claimed hard
allowance. Both below-width calls run inside `translation-event-univ`; all simplifier state is
destroyed before automaton construction. There is no memo or additional long-lived state, and the
helper does not read `ACACIA_OUTER_DEADLINE_MONOTONIC`. Expanded phase records use the existing
bounded channel for entry/completion, policy, change flag, promise counts, DAG sizes and elapsed
wall/CPU time. No formula strings or new before/after formula identities are emitted into
telemetry. Existing opt-in worker captures also record the policy.

The extended census retains its original columns and appends frame/component rows, actual
expanded-input promise and DAG counts, the over-width guard, `r != r0`, rewrite microseconds,
Spot's cheap syntactic safety/guarantee/obligation/recurrence/persistence flags on each side, and
`expected_components` for that input/frame. The shared probe/selection helper supplies the same
translation input as the expanded solver path. Each diagnostic frame measures both simplifier
calls to establish the comparison; over-width runtime
translation still performs just the incumbent call. Counts below are per input: guard-firing takes
precedence if different components occupy different categories. The last column includes any
additional below-width component, including inputs that also fire an existing guard elsewhere.

The initial single-process diagnostic invocation was **incomplete**: 449 of 1,524 inputs had
all seven frames, and 1,075 shipping inputs remained unknown. The requested serial 8 GiB, zero-swap scope was stopped and reaped
after 63 minutes without completing raw-positive source row 450 (exit 143, no OOM; peak scope
memory 5,402,796,032 bytes). A read-only stack snapshot places it in Boolean-BDD construction for
Spot's syntactic implication rules. Disabling language containment does not bound that work. These
are partial counts, with no extrapolation:

| Frame (per input) | Unchanged | Guard fires | Expanded only | Any below-width change |
|---|---:|---:|---:|---:|
| Raw positive | 284 | 5 | 160 | 160 |
| Raw negative | 285 | 11 | 153 | 153 |
| Decision-simplified positive | 259 | 2 | 188 | 188 |
| Decision-simplified negative | 272 | 11 | 166 | 166 |
| Prospective REAL components | 273 | 11 | 165 | 165 |
| Prospective formula-UNREAL components | 256 | 2 | 191 | 191 |
| Prospective automaton-UNREAL components | 260 | 2 | 187 | 187 |
| Shipping union | 246 | 13 | 190 | 194 |

The census creates no automaton and cannot determine whether a live run reaches the translation
boundary. Its retained artifacts are `_bm-logs.pm-p1/census/census.tsv`, `summary.json`,
`expanded-changed.list`, and `identity.json` in the same directory. The list contains 194 known
inputs with at least one below-width change in a prospective shipping frame; four also have an
existing guard in another frame. It is explicitly marked incomplete. The identity binds the source
map, executable, build options, source hashes, linked libraries and uncommitted Git state.

The driver subsequently completed the census using one fresh process per input, a one-row source
map, an 8 GiB zero-swap scope, and a **120 s evaluation bound per input**; see
`_bm-logs.pm-p1/census-v2/run.sh`. Source row 450 took about 8 s in a fresh process, whereas the
single-process run stalled there as state accumulated across inputs. Use the fresh-process method
for subsequent census evaluation. This external evaluation bound does not bound the opt-in
synchronous simplifier in the solver. The table above preserves the initial partial observation;
it is not the driver's completed census summary.

The completed fresh-process census (tool with the shared probe helper and `expected_components`)
classifies 1,523 of 1,524 inputs; source row 455 exceeded the 120 s evaluation bound and is
unknown. The existing guard fires on 40 inputs, the same union as the original #240 census.

| Frame (per input, 1,523 complete) | Unchanged | Guard fires | Expanded only |
|---|---:|---:|---:|
| Raw positive | 803 | 31 | 689 |
| Raw negative | 762 | 35 | 726 |
| Decision-simplified positive | 746 | 28 | 749 |
| Decision-simplified negative | 716 | 35 | 772 |
| Prospective REAL components | 720 | 33 | 770 |
| Prospective formula-UNREAL components | 662 | 28 | 833 |
| Prospective automaton-UNREAL components | 747 | 28 | 748 |
| Shipping union | 610 | 40 | 873 |

883 inputs (57.9%) have at least one below-width change in a prospective shipping frame,
including ten that also fire the existing guard in another frame. Raw rows, statuses,
`summary.json` and `expanded-changed.list` are in `_bm-logs.pm-p1/census-v2/`.

For each fresh process, retain its header and data rows, rewrite `source_row` from the one-row
map's local ordinal 1 to the full source-map ordinal, and concatenate data rows under one header.
Aggregate the driver's `.status` rows into a headerless TSV with exactly three fields:
`ordinal`, `ok|exhausted|error_N`, and elapsed `seconds`. Pass it to the summary tool:

```sh
python3 tests/event-univ-census-summary.py SOURCE_MAP CENSUS_TSV CENSUS_TOOL \
  --input-status INPUT_STATUS_TSV --allow-incomplete
```

An input contributes counts and changed-list membership only after all seven frames contain
exactly components 1 through `expected_components`, with consistent counts and no truncated row.
When a status file is supplied, the input must also have status `ok`; an exhausted, errored or
missing status makes it unknown even if apparently complete data rows remain. Status `ok` cannot
rescue missing components. Without `--allow-incomplete`, any unknown input rejects the summary.
Frame row totals retain observed rows; per-input classifications use only complete inputs.
The JSON reports unknown ordinals, run-status counts and recorded elapsed seconds; the identity
also binds the status file path and SHA-256. Older census rows without `expected_components`
cannot establish completion and need regeneration by the driver. No full census was rerun for
this correction.

The correction passes the census self-test, all 89 checked unit tests with four test processes,
and the full pytest suite: 2,322 passed, 40 skipped and 23 passing subtests. That suite includes
23 new census-summary regressions covering interrupted components and per-input statuses.
Ruff and the separate hardcoding check (96 passed, one skipped) also pass. Correction logs are
under `build_scratch/pm-p1-fix1/`; compilation used `-j 4`.

Before this correction, the checked and first `docker_default` release builds succeeded.
Expanded unit suites passed all 89
checked tests (two test processes) and all 86 release tests (serial). The full Python suite passes
2,299 tests, with 40 skips and 23 passing subtests. Earlier serial checked runs had an intermittent
missing-terminal-record failure in the unchanged standalone transport helper; the final complete
unit run passes that test. Generated formula/automaton equivalence, independent REAL/UNREAL parity
checks, master HOA identity, reused-translator, fork-propagation and proof-mutation checks pass.

The expanded checked corpus run completed 465 tests: 383 passed, 82 synthesis tests timed out, and
none failed an expected verdict or semantic controller check. The existing controller checker works
under expanded, including Moore initialization; timeouts leave full corpus validation incomplete.
Two additional captured Mealy/Moore synthesis cases exercise below-width changes and pass the
existing checked-build model checker. The Moore AAG also passes an independent output-independence
check over every reachable state, including all initial states. These small-case artifacts are
under `_bm-logs.pm-p1/checks/synthesis-changed/`; they do not complete the corpus gate. Expanded
remains opt-in. Admission and any performance or race measurements are the driver's job.
