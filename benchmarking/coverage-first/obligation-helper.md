# Obligation arm

The shipping presets with explicit portfolios declare `both:obligation:spot` in
`config/acacia-presets.json`, through the existing `acacia_default_arms` build option.
The first `docker_default` preset includes it, as do the non-native sparse formula fallback
and the bounding-box-tree portfolio. The arm uses Spot directly and needs neither native
arms nor OxiDD. Historical presets, including inherited sweep portfolios, and the empty
historical-default path retain their previous lists. Meson's `intro-buildoptions.json`
records the shipped membership; there is no runtime append or helper toggle.

It races one supervised original-game worker for eligible inputs alongside the
existing arms. Decision work is bounded by the invocation/race, without a local wall allowance.
`--arms both:obligation:spot` runs it alone, with supervised preparation;
an ineligible input returns UNKNOWN without starting an obligation solver. Explicit `--arms`
lists run exactly their named arms in decision mode. Synthesis retains an ordinary
fallback even when the obligation arm is selected alone. A preset without the arm preserves
the original route. For the first shipping preset, the previous TLSF decision portfolio is:

```sh
previous_arms=real:small:backward,real:small:forward
previous_arms="$previous_arms,unreal:formula:spot-guarded-sparse"
previous_arms="$previous_arms,unreal:automaton:forward,both:gr1-real-lift:oxidd"
acacia-bonsai -T spec.tlsf --arms "$previous_arms"
```

For LTL input, omit the native GR(1) arm, which requires `-T`. Explicit `-r`/`-u` selections
also run only their requested routes. Synthesis (`-s`) can accept a retained strategy only
through AIGER export as a trusted exact Spot result. Export failure or no retained strategy
leaves the ordinary synthesis route running. With the arm alone and `-s`, the ordinary REAL/UNREAL routes are added as fallbacks.
Replay and native checking retain their existing routes.

## Driver screens and full-corpus admission (13 October 2026)

The driver compared frozen helper implementation `0e6edbd9` (PMC2) with frozen post-memory
master M. On the 223 inputs whose objectives are syntactic obligations, the enabled helper solves
215 versus 207 for the helper-disabled route and master at 17 s: eight additional REAL solves, zero losses and
paired PAR-2 −286 s. The helper-disabled route matches master exactly in this historical screen.

At 60 s on the 16 obligation inputs unresolved at 17 s, the enabled helper solves nine versus one each for
the helper-disabled route and master. The same eight gains complete in 0.1–1.1 s, with zero losses. On 156
ineligible controls at 17 s, the enabled helper and the helper-disabled route both solve 62; paired PAR-2 changes by +1.8 s.
These historical driver screens are archived in `postmem20261011-p2`, with frozen
PMC2 in `binary-PMC2-1f64f527`.

The completed full-corpus campaign `postmem20261012-closing` admits the shipping arm at
PR #244 head `188cef33` (PMC4, `binary-PMC4-b3953b43`) against post-memory master
`b95777aa` (PMM, `binary-PMM-99ca4055`). Fresh paired 17 s races solve 1,222 versus
1,214; fully fresh measured 60 s races solve 1,255 versus 1,247, each on all 1,524 inputs.
Eight REAL gains and zero losses are confirmed at both caps. Raw paired PAR-2 deltas are
−284.885539 s and −972.094862 s. The raw 17 s near-cap loss `07.ltl` and gain
`g-unreal-310.ltl` are not confirmed by three cooled repetitions; original counts and
PAR-2 remain unchanged. The fresh 60 s series reuse no 17 s rows. ltlsynt/TACAS23 are
archived separate-session references, with derived 60 s observations. See
[RESULTS.md](../RESULTS.md) and the [decision record](../post-memory-frontend/decisions.md)
for exact counts, validation fallback and trust disclosures. Once #244 merges, PMC4 is
the same-configuration previous-revision G1 baseline for the next shipping change.

## Spot 2.16 API audit

Authoritative files: `/usr/local/include/spot/twaalgos/mtdswa.hh` and
`~/opt/build/spot-2.16-usrlocal/spot/twaalgos/mtdswa.cc`. The audited synthesis implementation
finishes by determining the initial root's winner and setting acceptance `t` (REAL) or `f`
(UNREAL). Its ltlsynt caller likewise tests `aut->acc.is_f()`.

With `realizability=true`, a normal return has one `bddtrue` or `bddfalse` root, respectively,
with matching colors. **This flag distinguishes both verdicts.** With `false`, REAL retains a
strategy; UNREAL still returns one `bddfalse` root with acceptance `f`. Decision mode makes
exactly one call with `true`, keeps no strategy and performs no re-check. Synthesis uses
`false` and can answer only REAL with a retained, exported and published trusted Spot
controller; a losing result declines to the ordinary route. `debug=-1` is mandatory:
partial debug automata are not decisions.

Null/empty objects have no normal contract and mean error, never UNREAL. Malformed root/colors
or acceptance results likewise mean error. `std::runtime_error` can report non-obligation input,
unsupported operators, or incompatible pre-registered quantified-variable levels. Standard and
unknown exceptions mean error; `std::bad_alloc` means exhausted. Non-obligations decline before
calling Spot. Signals, abnormal exits and interruption cannot supply a verdict. Only the explicit
completed losing representation yields `completed_UNREAL`; the parent also requires a published
completed outcome matching the helper's exit code.

## Eligibility, timing and execution

Eligibility is solely Spot's cached DAG property `is_syntactic_obligation()` on the positive
original normalized objective, after common realizability simplification in decision mode,
and before decomposition or UNREAL polarity/X transformations. Synthesis skips realizability
simplification, as the ordinary synthesis route does: eliminated signals are not patched back
into the strategy. No names, fingerprints, size rules or fitted thresholds
select the helper. An existing legacy child publishes eligibility through a shared atomic mailbox
at that boundary. In a native-only portfolio, one existing native child prepares that same
objective; preparation failure leaves the ordinary native solve running. When the obligation
arm is selected alone, no ordinary child exists, so one supervised preparation child prepares that
same objective under the ordinary deadline, signal and resource handling; on an ineligible input
it exits and the invocation returns UNKNOWN without starting the helper. (Observations from before
this change, in which the parent prepared the objective and no process started, are historical.)
In the default portfolio, preparation delays no other arms and an ineligible input starts no
extra process.

The parent launches all ordinary arms before consuming eligibility. The optional arm is removed
from their construction/thread/RSS budget counts, preserving their existing resource shares on
ineligible inputs as well as their launch order. It performs no serial formula
pre-pass and launches no helper for an ineligible input. Exactly one eligible helper uses the
existing fork/process-group/supervisor machinery and inherits the invocation cgroup. It repeats
normalization inside its bounded process and rechecks eligibility. Winner, deadline, interruption
and parent-error cleanup kill and reap it through the ordinary loser path. Linux additionally uses
`PR_SET_PDEATHSIG=SIGKILL` for abrupt parent death.

Registration follows ltlsynt: traverse the current formula DAG and pre-register only its
first timing block, inputs for Mealy and outputs for Moore. Spot registers the remaining
block in formula traversal order; declared unused signals are retained in the controller
interface. Inputs still precede outputs for Mealy, and outputs precede inputs for Moore.
Production LTL is Mealy. The linked TLSF frontend already supplies effective Mealy, including
Moore lowering and strict normalization: the helper solves that objective without another shift
or polarity swap. Raw Moore tests detect registration as Mealy.

Decision and synthesis work have no fixed local wall allowance. The parent supervises and
reaps the worker under the existing winner, signal and invocation deadline handling. In `-s`
mode the worker builds the controller, writes directly to the requested file through the same
`std::ofstream`/Spot ASCII AIGER writer used by ordinary synthesis, closes the stream, and exits
REAL only after successful export. An open, write, close or export exception makes the arm
exit UNKNOWN; ordinary workers keep running. The parent handles that exit like any worker,
without a second publication process, stopping/resuming ordinary writers, private publication
files, rename/rollback or a separate publication allowance.

File guarantees match master's ordinary synthesis: concurrent workers can open/truncate/write
the same destination, and cancellation can leave a partial or truncated file. Existing output
bytes are not restored. A controller is checked offline after a successful run; file preservation
or transactional publication is not promised. A blocked writer remains inside its supervised
worker, so the parent can kill and reap it on a winner, interruption or invocation deadline.

Address space is capped at eight GiB. The soft RSS share follows native construction:
cgroup memory limit (bounded by the address cap) / launched workers including helper / 2.
The parent polls and kills/reaps on that limit; scheduling can overshoot the soft limit. These
are global resource choices. An earlier invocation deadline still governs answer acceptance.

Typed outcomes are `completed_REAL`, `completed_UNREAL`, `inapplicable`, `exhausted`, `cancelled`
and `error`; only completed decisions map to the parent's REAL/UNREAL codes. Eligibility, start,
completion, outcome/reason and elapsed wall time use the existing bounded nonblocking phase
channel. Obligation records and worker specifications identify `both:obligation:spot`;
`parent_winner` identifies the accepted worker. Parent records cover killed helpers. Missing/dropped records remain uncertain.
A portfolio without the arm allocates no mailbox, checks no eligibility, samples no helper
clocks/RSS and emits no helper events or workers. Membership uses the existing compile-time
`acacia_default_arms` registry option; its empty default remains unchanged across frontends.

## Trust and checks

Decision REAL and UNREAL and synthesis REAL/controllers are **trusted exact Spot library
results**. Both modes declare `trust=spot_exact`, `same_library_checked=false` and
`independent_checked=false`. Decision mode retains no strategy and performs no re-check.
Synthesis performs no runtime independent controller check or general Büchi translation.
Exceptions, interruptions, null/malformed library returns, failed exports and mismatched exits
never become verdicts. No UNREAL certificate is produced by the synthesis arm.

Synthesis converts the retained MTBDD strategy to a machine and exports the same ASCII AIGER
format and declared IO names as ordinary synthesis, using Spot's used-signal-first export
convention and appending unused declarations. Used signal order follows the strategy; it need
not equal declaration order. The trusted strategy is serialized directly to the file without
runtime reparsing or controller checking. Spot's quantified BDD registration order is
part of the semantics contract: formula traversal registers inputs before outputs for Mealy,
outputs before inputs for raw Moore. Production LTL uses Mealy; TLSF uses the frontend's
normalized effective Mealy objective, including strict assumptions, and no extra shift.

Moore target export reuses ordinary `mealy_to_moore()` conversion, including advancing source
latches through the dummy initial round for Moore-to-Mealy input lowering. True resets use
complemented physical latches so every emitted AIGER latch starts at zero. The worker writes
these bytes directly to `-s`; the parent applies its existing deadline/interruption checks before
accepting the worker's REAL exit. Export failures leave ordinary synthesis running.

These semantic guarantees are verified **offline**. Generated Mealy and raw Moore obligations
are synthesized by the same helper API and checked by the existing general controller product
in the C++ test only. The CLI panel checks final Mealy/Moore AIGER bytes with the independent
explicit safety-product oracle, including strict REQUIRE/ASSERT assumptions, unused declared
signals, autonomous controllers and true/false Moore initialization. A Moore objective
registered as Mealy is detected by the offline controller check; the converse is detected by
the expected synthesis result. Test-only strategy and output corruptions are accepted as
trusted Spot results at runtime and rejected by the offline oracle, making the boundary
explicit. An interposed general-check exception confirms the arm no longer calls that checker.
Tests require both ordinary and obligation workers to serialize through a file stream opened
on the requested destination and compare their format on constant controllers. Open, stream
and exception export faults must leave an already-running ordinary worker answering with an
oracle-valid controller. Stalled synthesis/export and FIFO writers exercise the ordinary
winner, deadline, interruption and reaping paths. Synthesis lasting beyond five seconds can
still win under the invocation deadline. The special publication-protocol tests were removed.
The synthesis suite subset also runs through `tests/check-real-correct.sh` with
`AB_OPTS='--arms both:obligation:spot'`.

The C++ unit covers eligibility, shared DAGs, abbreviations, constants, unused signals, timing,
`realizability=true`, malformed/null results, nondecision exit mapping and offline checks of
generated and mutated strategies.
Actions execute outside assertions. The CLI panel cross-checks Mealy/Moore with ltlsynt general
construction (`--obligation-synthesis=no --bypass=no`) and Acacia's existing route, including
strict TLSF safety assumptions. It tests single arm membership, exact `--arms` selection, default eligibility, launch order, native-only and
opposite-polarity selection, post-simplification eligibility, ordinary-only selection, synthesis/fallback, exceptions,
null/empty/allocation/abnormal exits, wall/RSS stops, cancellation, deadlines, parent termination
and no remaining child processes/groups. Faults use a test-only preload library, never linked
into or installed with a solver; release binaries contain no new mutation hook.

Checked/release unit suites passed 90/90 and 87/87; obligation outputs agree byte-for-byte.
Diagnostics-enabled CLI checks and ASan/UBSan passed. Leak scanning was disabled because sandbox
ptrace supervision blocks LeakSanitizer. Full pytest: 2,299 passed, 40 skipped, 23 passing subtests;
hardcoding: 96 passed, one skipped. Ruff and config frontend checks passed. Ten generated games
match frozen post-memory master in exit code, stdout and worker specifications with the then-used helper-disabled route.

Four serial release diagnostics used the first registry-resolved `docker_default` preset, built
via `BENCHMARK_COMPILE_JOBS=4 ./self-benchmark.sh -R -c <preset>`, in
`systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0`. Selection used census
`raw-positive` obligation flags, then smallest DAG and ordinal; the ineligible row also required a
non-obligation `simplified-positive` flag. Ordinals are TLSF source-map data rows.

| Ordinal | Eligible | Verdict | Winner | Helper outcome |
|---|---|---|---|---|
| 52 | yes | UNREAL | formula UNREAL, spot-guarded-sparse | cancelled by winner |
| 493 | yes | REAL | REAL, backward | cancelled by winner |
| 489 | yes | REAL | REAL, backward | cancelled by winner |
| 964 | no | REAL | REAL, backward | inapplicable |

All spawned workers have parent reaping records. Logs, binaries and checks remain under
`build_scratch/pm-p2-impl/` in this worktree. These diagnostics make no timing claim.

## Named-arm validation (12 October 2026)

The first registry-resolved `docker_default` preset passed 90/90 checked and 87/87 release
unit tests after exposing the worker as an arm. Obligation unit output is byte-identical
between builds. The CLI checks cover exact selection, default membership, no solver spawn
on ineligible inputs, ordinary worker specifications unchanged on ineligible controls,
conversion errors, ordinary-only routing, timing, typed faults, synthesis and cleanup.
Ten generated Mealy/Moore and strict games with the then-used helper-disabled route still match frozen master in exit code,
stdout and worker specifications. No resource thresholds or solving semantics changed.

Full pytest passed 2,299 tests and 23 subtests, with 40 existing conditional skips and no
added exclusions or skip plugin. The temporary-repository `git fast-import` fixtures ran.
Hardcoding pytest passed 96 tests with one existing skip; Ruff, config frontends, shell
syntax and diff checks passed. Scratch and pytest `TMPDIR` are under
`build_scratch/pm-p2-arm/`; binaries, build identity and validation logs are preserved there.

The same four original single-instance diagnostics ran serially under
`systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0`, once with the default
portfolio and once with `--arms both:obligation:spot`. These make no timing claim.

| Ordinal | Portfolio | Verdict | Winner | Obligation outcome | Solver workers |
|---|---|---|---|---|---|
| 52 | default | UNREAL | spot-guarded-sparse | cancelled | 6 |
| 493 | default | REAL | backward | cancelled | 6 |
| 489 | default | REAL | backward | cancelled | 6 |
| 964 | default | REAL | backward | inapplicable | 5 |
| 52 | alone | UNREAL | obligation | completed_UNREAL | 1 |
| 493 | alone | REAL | obligation | completed_REAL | 1 |
| 489 | alone | REAL | obligation | completed_REAL | 1 |
| 964 | alone | UNKNOWN | none | inapplicable | 0 |

All spawned workers have parent reaping records and no surviving worker PID. Ineligible
arm-only selection spawns zero solver processes. The scope bus required sandbox escalation;
the initial blocked attempt ran no solver. Full-corpus admission is now recorded above in
`postmem20261012-closing`; these earlier diagnostics remain correctness observations.


## Registry membership validation (12 October 2026)

The obligation arm now joins through the existing build option, rather than a runtime
append. All three shipping presets with explicit portfolios declare it; the two shipping
presets with empty historical defaults stay unchanged. Resolved options for every
non-shipping preset remain identical to their previous values, including the inherited
sweep and diagnostic portfolios. The non-native fallback can run the arm because its
Spot implementation is compiled independently of native arms.

The first registry-resolved shipping preset passed 90/90 checked and 87/87 release unit
tests. Its release build used `BENCHMARK_COMPILE_JOBS=4 ./self-benchmark.sh -R -c <preset>`;
`meson-info/intro-buildoptions.json` records the complete six-arm list, including
`both:obligation:spot`. No new compile option or fallback default was introduced.
Registry validation and all 49 shared scalar frontend checks pass, with the existing
documented TLSF frontend divergence.

Full pytest passed 2,300 tests and 23 subtests, with 40 existing conditional skips and the
temporary-repository commit fixtures enabled. Local Python bindings and Spot bindings
were supplied through the same module-path setup as CI. Hardcoding pytest passed 96 tests
with one existing skip; Ruff, shell syntax and diff checks passed. The CLI checks reject the
removed helper toggle and preserve explicit arm selection, eligibility, full normalized
record parity apart from the explicit inapplicability record, typed faults, cancellation,
partition validation, synthesis exclusion and cleanup. Checked/release obligation-unit
output is byte-identical. Ten generated games match frozen master in exit code, stdout and
worker specifications in both builds using explicit previous-arm selection.

The same four structural selections ran serially under 8 GiB, zero-swap systemd scopes
with the default portfolio and the obligation arm alone. These are correctness smokes,
with no timing or full-corpus admission claim.

| Ordinal | Default verdict | Default solver workers | Arm-only verdict | Arm-only solver workers |
|---|---|---|---|---|
| 52 | UNREAL | 6 | UNREAL | 1 |
| 493 | REAL | 6 | REAL | 1 |
| 489 | REAL | 6 | REAL | 1 |
| 964 | REAL | 5 | UNKNOWN | 0 |

The three eligible default helpers were cancelled by the ordinary winner. The ineligible
default input emitted no helper records and started no extra process. Arm-only
inapplicability used one supervised preparation child and never entered obligation
synthesis. All spawned processes have parent reaping records and no surviving PID or
process group. The existing wall/address/RSS allowances, racing, trust boundary and
same-library REAL check are unchanged.

Logs, frozen binaries, build options, master parity and smoke records are retained under
`build_scratch/pm-p2-registry/`. Large temporary checked/Python build directories were
removed after validation. All source changes remain unstaged and uncommitted.

## Issue #248: negative diagnosis (13 October 2026)

This is the preserved investigation of the earlier contract. The owner's subsequent
13 October decision implemented below supersedes its decision-mode checking and wall policy.

No solver change is proposed. The existing preparation already performs ltlsynt's
polarity/global-equivalence simplification. Formula traversal BDD order improves the
synthesis call, but does not close these inputs under the existing five-second allowance
and required REAL strategy check. Removing that check or choosing a longer allowance to
fit these rows would change the requested contract. This investigation does not rule out a
future on-the-fly strategy checker or other general algorithm changes.

Selection was limited to the three documented evaluation rows in #248, then verified with
`is_syntactic_obligation()` on their positive normalized objectives. Builds resolve the
first `docker_default` preset from the registry, use Spot 2.16 at base `e762e736`, and match
the normal release profile (`release`, optimization 3, LTO, compiler profile `release`).
Every solver diagnostic ran serially under
`systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0`. These are single uncooled
observations, not a benchmark campaign or admission evidence. Early diagnostics overlapped
compilation; the driver must measure any future implementation independently.

### Preparation and preprocessing

The arm-alone release runs all exhaust the fixed allowance, even when the outer invocation
could allow 60 seconds. The parent reports `wall_allowance`, kills/reaps the worker, and
returns UNKNOWN. Its existing `obligation_synthesis` worker stage groups the library call
and REAL check, so scratch calls separate those costs below.

| Input | Startup to parent eligibility observation (s) | Simplification phase (s) | Helper wall at stop (s) |
|---|---:|---:|---:|
| `chomp_pb_3_5` | 0.0193 | 0.0092 | 5.0280 |
| `chomp_pb_4_4` | 0.0298 | 0.0133 | 5.0301 |
| `chomp_pb_3_6` | 0.0300 | 0.0145 | 5.0480 |

The eligibility observation includes supervisor polling, so it is an upper bound on
preparation. Neither TLSF conversion nor normalization accounts for the misses.
`helper_objective()` invokes `realizability::apply_simplifier()`, which constructs Spot's
`realizability_simplifier` with its default polarity and global-equivalence options.
ltlsynt uses the same options for Mealy. On each input, its verbose `new formula:` output
matches `tlsf-frontend-inspect --rsimp` byte for byte and parses to the same Spot formula.
Both already eliminate `oto := !oti`. ltlsynt reports one subformula in every case;
decomposition supplies no advantage here.

### Synthesis, order and checking

The tiny scratch C++ program calls `obligation_synthesis` on Acacia's objective and on the
exact formula extracted from ltlsynt's trace, in fresh dictionaries. It varies only
registration and `realizability`, keeps `simplify_terms=true` and `debug=-1`, and times
preparation, registration, the library call, and the existing `check_strategy()` separately.
Acacia pre-registers all inputs and outputs in declaration order. ltlsynt pre-registers
only the first timing block, in formula traversal order, and lets Spot register the rest
in traversal order. Input-before-output order remains Mealy; no game polarity or timing
transformation is involved.

| Input | Declaration order, decision (s) | Formula order, decision (s) | Declaration order, retained strategy (s) | Formula order, retained strategy (s) |
|---|---:|---:|---:|---:|
| `chomp_pb_3_5` | 4.031 | 1.848 | 16.770 | 4.299 |
| `chomp_pb_4_4` | 11.344 | 4.460 | 32.990 | 5.116 |
| `chomp_pb_3_6` | >120 | 52.180 | >70 | 60.602 |

Decision calls use `realizability=true`; retained-strategy calls use `false`, as production
does. The formula-order decision calls closely explain the issue's 1.9/4.6/45-second
reference, especially the first two rows. The early full ltlsynt CLI reruns took
8.864/22.463/>70 seconds during compilation; those are not paired performance comparisons.
Even the fastest completed third-row decision is far outside the helper allowance.

| Input | Existing check, declaration order (s) | Existing check, formula order (s) | General low-level translation check (s) |
|---|---:|---:|---:|
| `chomp_pb_3_5` | 13.564, passed | >235 | >67, translation incomplete |
| `chomp_pb_4_4` | 29.618, passed | >65 | >60, translation incomplete |
| `chomp_pb_3_6` | not reached | >10 | >18, translation incomplete |

Lower bounds end at diagnostic cutoffs, not completion. Formula-order checking reached
neither an accepted REAL result nor a failed strategy result: the same-library language
check was unfinished. These C++ runs do not split negated-objective construction from
intersection. The general-check trial retained input coverage
and intersection against the original objective, replacing the negated-obligation
construction with Spot's ordinary translator at low optimization. Synthesis took
2.628/9.239/51.355 seconds and strategy conversion only 0.00077/0.00321/0.00407 seconds;
translation of the negated objective then consumed the remaining 70-second diagnostic
budget on every input. Thus conversion is cheap, and swapping to ordinary translation
does not make the required check fit. ltlsynt's ordinary `--realizability --verbose`
invocation does not request `--verify` and supplies no equivalent language check.

The normal library REAL returns in unfinished scratch checks are not accepted arm
verdicts. The original same-library check remains required and trusted exact Spot remains
the declared trust boundary. No new same-library check is presented as an independent
certificate.

Spot's ordinary level-2 LTL simplifier was also tried, entirely in scratch. Its
language-equivalent formulas remain syntactic obligations, but retained-strategy synthesis
took 2.840/4.087/42.326 seconds, followed by checks still unfinished at the 70-second
cutoffs (>67/>65/>27 seconds respectively). This does not close the gap either. No such
transformation was installed in the arm. The existing checked/release unit and CLI oracle
panel exercises the unchanged arm, including Mealy and Moore semantics.

There is no implemented after version: the negative branch requires leaving solver code
unchanged. The tables compare the current arm with single-run scratch alternatives, not
new shipping performance. Eligibility, original-frame timing, racing, reaping, resource
limits, decision-only selection, trust declarations, and the route for ineligible inputs
retain their existing implementation exactly. No names, sizes, fitted thresholds or
precomputed decisions enter the solver.

### Validation and retained evidence

- Checked build: `meson compile -C build_scratch/i248/checked -j 4`;
  `meson test -C build_scratch/i248/checked --suite unit --num-processes 4 --no-rebuild`: 90/90.
- Release build: `meson compile -C build_scratch/i248/release -j 4`;
  `meson test -C build_scratch/i248/release --suite unit --num-processes 4 --no-rebuild`: 87/87.
- Checked/release obligation-unit stdout is byte-identical. The CLI tests cover timing,
  general-construction Mealy/Moore oracles, eligibility, racing, faults and cleanup.
- `/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 -m pytest tests/pytest/ -q`: 2,296
  passed, 40 existing conditional skips, 20 passing subtests, four failures. All four are
  `test_evidence_tools.py` history fixtures attempting to create commits with
  `git fast-import`. The user's explicit prohibition on staging/committing applies to
  throwaway repositories too. A scratch `sitecustomize.py` audit guard blocks those
  commands before execution, including absolute executable paths; no tests were excluded
  and no commit was created. Python/Spot bindings were supplied through `PYTHONPATH` as in
  CI, with `TMPDIR` under this task's scratch directory. A fully passing pytest run remains
  unavailable under that prohibition.
- Hardcoding pytest: 96 passed, one existing skip.
- `ruff check tests scripts benchmarking`: passed; the obligation CLI script also passes.
  An extra `ruff check .` finds 19 pre-existing errors in examples and dependency tooling;
  those files are unchanged.
- `python3 tests/check-config-frontends.py`: all 49 shared scalar options agree, with the
  existing documented TLSF frontend divergence. No options changed.

Raw phase records, objective/order equality, per-call logs, scratch C++/Python sources,
build options, binary hashes and validation logs remain under `build_scratch/i248/`. The
retained release binary has the same hash before and after this investigation. All three
original arm workers have parent terminal/reaping records and no surviving PID. Large
checked/release build directories and the copied dependency were removed after validation.
Only this documentation changes in the working tree; nothing is staged or committed.

The driver can decide whether to pursue a general on-the-fly REAL strategy checker or a
separate, independently justified helper-budget policy. Increasing the allowance to fit
these three timings, omitting the REAL check, or changing ineligible routing is not a fix
submitted by this task. Commit-based pytest fixtures and the unrelated broad Ruff errors
remain outside this diagnosis.


## Issues #248 and #249: earlier independently checked implementation (13 October 2026)

This section records the earlier implementation and its validation. The subsequent binding
owner decision below supersedes its synthesis trust and publication policy.

The owner changed the decision contract to one trusted exact decision call without a local
wall allowance, and authorized independently checked controller synthesis within an absolute
five-second allowance. The earlier diagnosis above is retained as evidence of the old contract;
its hypothetical alternatives are not performance claims about this implementation.

Validation used the first registry-resolved `docker_default` preset. Checked and release
builds used `meson compile -j 4`; release uses optimization 3, LTO and the normal release
compiler profile. Unit suites ran with `--num-processes 4` and passed 91/91 checked and
88/88 release. The initial checked run hit deadlines in two existing native attribution
tests while compilation/pytest overlapped; both passed targeted rechecks and the final
complete 91-test run. No timeout or assertion was weakened.

The decision oracle panel checks 74 Mealy/Moore rows, including 64 generated obligations,
against ltlsynt's general construction with obligation synthesis and bypass disabled.
Declaration, reverse-declaration and formula-traversal registration variants agree. Unit
stdout is byte-identical in checked/release. Test-only ELF interposition confirms the single
`realizability=true` call, decisions completing beyond five seconds, null/exception/abnormal
exit declines, memory limits, cancellation, deadlines and reaping. Synthesis tests reject
corrupt strategies and serialized controllers, exercise export/check exceptions and
five-second fallback with and without an outer deadline, and require the helper to produce
all 64 Mealy/Moore/strict and autonomous controllers accepted by the independent explicit
AIGER safety-product oracle. All actions run outside assertions.

The synthesis subset (`shift_2`, `shift_4`, `lilydemo13`) passed through the configured
`tests/check-real-correct.sh` with `AB_OPTS='--arms both:obligation:spot'`. Ten generated
ineligible Mealy/Moore/strict TLSF games match a release build of pinned master `b95777aa`
in exit code, stdout, stderr and all worker specifications; no helper event or process is
added. This master source was extracted with read-only `git archive`, without checkout,
staging or commits. Full pytest passed **2,300 tests and 23 subtests**, with 40 existing
conditional skips; the explicitly allowed commit-creating temporary-repository fixtures
ran normally. The first pytest run found stale generated attribution line references;
regeneration fixed them, and the complete rerun passed. Hardcoding pytest passed 96 tests
with one existing skip. Ruff on `tests scripts benchmarking`, configuration frontend checks
(all 49 shared scalars), and `git diff --check` passed.

The four authorized release diagnostics ran serially, after compilation and correctness
tests, in `systemd-run --user --scope -p MemoryMax=8G -p MemorySwapMax=0`, each with an
external 60-second cap and no outer-deadline environment variable. The three decision
runs select exactly `--arms both:obligation:spot`. The synthesis smoke is generated Moore
safety with nonzero output initialization, using the same arm selection and `-s`.
Times include scope/invocation overhead. These are single observations, with no performance
claim or new full-corpus admission evidence.

Diagnostic outcomes follow below. The externally capped third run lost terminal phase
records when the timeout signalled the process group (including the record writer);
completion/reaping telemetry for that run is incomplete, not an exact verdict.


| Input | Mode | Observed scope/invocation time | Verdict | Accepted route / helper outcome |
|---|---|---:|---|---|
| `chomp_pb_3_5` | decision | 3.069 s | REAL | obligation / `completed_REAL` |
| `chomp_pb_4_4` | decision | 7.080 s | REAL | obligation / `completed_REAL` |
| `chomp_pb_3_6` | decision | 60 s external cap hit | UNKNOWN | none / incomplete termination telemetry |
| generated Moore safety | `-s` | 0.034 s | REAL | ordinary backward / helper cancelled by winner |

The third run's harness required terminal records before persisting its wrapper status and
total wall time; that assertion failed because group termination also killed the writer.
Its displayed time is therefore the known external cap, not an invented completed-run
measurement. It was not rerun. A separate host PID/process-group check confirmed no surviving
worker. The other three runs have parent terminal records for every spawned worker. The
synthesis smoke's helper lost the race safely; the emitted controller was accepted by the
independent explicit Moore AIGER oracle. The 64-controller test panel above requires helper
wins in both builds, separately establishing successful arm synthesis.

Raw diagnostic records, oracle results, build commands/options, binary identities, master
parity, and all validation logs remain under `build_scratch/i248-249/`. Large build objects,
throwaway repositories and copied dependency/source trees were removed; the three solver
binaries, required local shared library, unit binaries and Python bindings are retained.
Nothing in this worktree was staged or committed.


## Issue #249: trusted synthesis owner decision (13 October 2026)

The binding owner decision extends exact Spot trust to synthesis. The timed arm builds no
general Büchi automaton and runs no controller product or serialized-interface check. Both
modes record `trust=spot_exact`, `same_library_checked=false` and `independent_checked=false`.
Decision solving, eligibility, formula-order registration and global memory limits are unchanged.

The latest driver decision removes the special parent-side publication protocol completely.
The arm now publishes like any ordinary REAL synthesis worker: it converts Spot's retained
strategy with the CLI's Mealy/Moore semantics and writes directly to `-s` through the shared
ordinary ASCII AIGER writer. Successful export yields the ordinary REAL exit code. Failed
export yields UNKNOWN, and the already-running ordinary route can answer. There is no local
synthesis/publication allowance. The existing parent winner, deadline, cancellation and reaping
logic governs the arm, including blocked export. File guarantees are exactly those of master's
ordinary synthesis; concurrent writes and cancellation do not preserve an earlier destination.

Generated timing and strict-objective checks run only in the tests, including raw registration
order mutations and deliberately swapped CLI semantics. Strategy/output corruptions are
trusted at runtime and rejected offline. Two-input CLI cases preserve declared IO names
without imposing declaration order on Spot's used-signal-first AIGER interface. The shared
writer contract and export-failure fallback are tested directly; publication/rollback tests no
longer apply. Earlier validation of the removed publication protocol is superseded by this
implementation and the review findings that prompted the driver decision.

Validation of direct worker export used the first registry-resolved `docker_default` preset.
Both builds compiled with `-j 4`. Complete unit suites passed **91/91 checked and 88/88
release** with `--num-processes 4`; the separate obligation suites passed **3/3 per build**.
Each build's arm wrote **116 generated CLI controllers** accepted by the explicit AIGER
oracle; deliberately swapping either target semantics was rejected. Raw API controller
products, registration-order mutations, shared writer/format checks, export-failure fallback,
blocked export/FIFO supervision, deadline/interruption and worker reaping passed. Checked and
release obligation-unit stdout is byte-identical.

The synthesis subset (`shift_2`, `shift_4`, `lilydemo13`) passed **3/3 per build** through
`tests/check-real-correct.sh` with `AB_OPTS='--arms both:obligation:spot'`. Full pytest passed
**2,300 tests and 23 subtests**, with 40 conditional skips, using the authorized temporary
repository fixtures and `TMPDIR` under `build_scratch/i249-direct-worker/`. Hardcoding pytest
passed **96 tests**, with one existing skip. Ruff on `tests scripts benchmarking`, all 49
shared configuration frontend scalars and `git diff --check` passed. The generated decline
cause census was refreshed only for the three source-line references shifted by extracting
the writer. Changes remain uncommitted and the index is unchanged. Validation logs are retained
in `build_scratch/i249-direct-worker/logs/`; the large task build directories were removed.
