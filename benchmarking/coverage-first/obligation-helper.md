# Obligation decision arm

`both:obligation:spot` is part of the default portfolio in the CLI and Docker wrapper.
It races one bounded original-game decision worker for eligible inputs alongside the
existing arms. `--arms both:obligation:spot` measures this arm alone; an ineligible input
returns UNKNOWN with no solver process. Any explicit `--arms` list runs exactly its named
arms, so omitting this arm disables it for that selection.

There is no general CLI operation to subtract one arm from the default portfolio.
`--obligation-helper off` is therefore retained solely as that escape hatch; it restores
the frozen post-memory master route. `on` is removed: arms join through `--arms`.
Combining `off` with an explicitly selected obligation arm is an error. Synthesis (`-s`)
uses the ordinary default route; explicitly selecting this decision-only arm with `-s`
is an error. Replay and native checking retain their existing routes.

## Driver screens and pending admission

The driver compared frozen helper implementation `0e6edbd9` (PMC2) with frozen post-memory
master M. On the 223 inputs whose objectives are syntactic obligations, the enabled helper solves
215 versus 207 for `off` and master at 17 s: eight additional REAL solves, zero losses and
paired PAR-2 −286 s. Explicit `off` matches master exactly in this screen.

At 60 s on the 16 obligation inputs unresolved at 17 s, the enabled helper solves nine versus one each for
`off` and master. The same eight gains complete in 0.1–1.1 s, with zero losses. On 156
ineligible controls at 17 s, the enabled helper and `off` both solve 62; paired PAR-2 changes by +1.8 s.
These are driver-supplied screens, not a completed full-corpus admission result. The default
is enabled so the driver's full-corpus candidate campaign exercises shipping behavior;
final admission remains with that campaign.

## Spot 2.16 API audit

Authoritative files: `/usr/local/include/spot/twaalgos/mtdswa.hh` and
`~/opt/build/spot-2.16-usrlocal/spot/twaalgos/mtdswa.cc`. The audited synthesis implementation
finishes by determining the initial root's winner and setting acceptance `t` (REAL) or `f`
(UNREAL). Its ltlsynt caller likewise tests `aut->acc.is_f()`.

With `realizability=true`, a normal return has one `bddtrue` or `bddfalse` root, respectively,
with matching colors. **This flag distinguishes both verdicts.** With `false`, REAL retains a
strategy; UNREAL still returns one `bddfalse` root with acceptance `f`. The helper uses `false`
for strategy checking. `debug=-1` is mandatory: partial debug automata are not decisions.

Null/empty objects have no normal contract and mean error, never UNREAL. Malformed root/colors
or acceptance results likewise mean error. `std::runtime_error` can report non-obligation input,
unsupported operators, or incompatible pre-registered quantified-variable levels. Standard and
unknown exceptions mean error; `std::bad_alloc` means exhausted. Non-obligations decline before
calling Spot. Signals, abnormal exits and interruption cannot supply a verdict. Only the explicit
completed losing representation yields `completed_UNREAL`; the parent also requires a published
completed outcome matching the helper's exit code.

## Eligibility, timing and execution

Eligibility is solely Spot's cached DAG property `is_syntactic_obligation()` on the positive
original normalized objective, after common realizability simplification and before decomposition
or UNREAL polarity/X transformations. No names, fingerprints, size rules or fitted thresholds
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

Mealy registers inputs before outputs; Moore registers outputs first, as the API documents.
Production LTL is Mealy. The linked TLSF frontend already supplies effective Mealy, including
Moore lowering and strict normalization: the helper solves that objective without another shift
or polarity swap. Raw Moore tests detect registration as Mealy.

The fixed five-second wall allowance starts at fork, matching the existing optional-work reference
horizon. Address space is capped at eight GiB. The soft RSS share follows native construction:
cgroup memory limit (bounded by the address cap) / launched workers including helper / 2. The
parent polls and kills/reaps on either limit; scheduling can overshoot these soft limits. These
are global resource choices. An earlier invocation deadline still governs answer acceptance.

Typed outcomes are `completed_REAL`, `completed_UNREAL`, `inapplicable`, `exhausted`, `cancelled`
and `error`; only completed decisions map to the parent's REAL/UNREAL codes. Eligibility, start,
completion, outcome/reason and elapsed wall time use the existing bounded nonblocking phase
channel. Obligation records and worker specifications identify `both:obligation:spot`;
`parent_winner` identifies the accepted worker. Parent records cover killed helpers. Missing/dropped records remain uncertain.
Off allocates no mailbox, checks no eligibility, samples no helper clocks/RSS and emits no helper
events or workers. This runtime-only arm needs no compile-option registry entry.

## Trust and checks

Spot's completed decision is trusted exact. REAL additionally converts the strategy with
`mtdswa_strategy_to_mealy`, checks input coverage, and checks intersection against
`obligation_to_mtdswa(Not objective)->as_twa()`. This avoids a full Büchi LTL translation and is
a **same-library check, not an independent certificate**. Failed checks are errors. UNREAL has
no independent certificate. Phase records declare `trust=trusted_exact_spot` and whether the
same-library check passed. Existing replay/native checks are untouched.

The C++ unit covers eligibility, shared DAGs, abbreviations, constants, unused signals, timing,
`realizability=true`, malformed/null results, nondecision exit mapping and a mutated strategy.
Actions execute outside assertions. The CLI panel cross-checks Mealy/Moore with ltlsynt general
construction (`--obligation-synthesis=no --bypass=no`) and Acacia's existing route, including
strict TLSF safety assumptions. It tests single arm membership, exact `--arms` selection, default eligibility, launch order, native-only and
opposite-polarity selection, post-simplification eligibility, off, synthesis exclusion, exceptions,
null/empty/allocation/abnormal exits, wall/RSS stops, cancellation, deadlines, parent termination
and no remaining child processes/groups. Faults use a test-only preload library, never linked
into or installed with a solver; release binaries contain no new mutation hook.

Checked/release unit suites passed 90/90 and 87/87; obligation outputs agree byte-for-byte.
Diagnostics-enabled CLI checks and ASan/UBSan passed. Leak scanning was disabled because sandbox
ptrace supervision blocks LeakSanitizer. Full pytest: 2,299 passed, 40 skipped, 23 passing subtests;
hardcoding: 96 passed, one skipped. Ruff and config frontend checks passed. Ten generated games
match frozen post-memory master in exit code, stdout and worker specifications with helper off.

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
conversion errors, disabled routing, timing, typed faults, synthesis and cleanup.
Ten generated Mealy/Moore and strict games with `off` still match frozen master in exit code,
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
the initial blocked attempt ran no solver. Full-corpus admission remains the driver's campaign.
