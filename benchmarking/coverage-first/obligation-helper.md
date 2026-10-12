# Optional obligation decision helper

`--obligation-helper off|on` defaults to `off`. This is one optional, original-game decision
worker. Synthesis (`-s`), replay, native checking and the default portfolio retain their existing
routes. There is no performance or coverage admission claim.

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
objective; preparation failure leaves the ordinary native solve running.

The parent launches all ordinary arms before consuming eligibility. It performs no serial formula
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
channel. Worker specifications identify `obligation-helper`; `parent_winner` identifies the
accepted worker. Parent records cover killed helpers. Missing/dropped records remain uncertain.
Off allocates no mailbox, checks no eligibility, samples no helper clocks/RSS and emits no helper
events or workers. This runtime-only option needs no compile-option registry entry.

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
strict TLSF safety assumptions. It tests single helper membership, launch order, native-only and
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
