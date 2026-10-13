# Obligation decision arm

The shipping presets with explicit portfolios declare `both:obligation:spot` in
`config/acacia-presets.json`, through the existing `acacia_default_arms` build option.
The first `docker_default` preset includes it, as do the non-native sparse formula fallback
and the bounding-box-tree portfolio. The arm uses Spot directly and needs neither native
arms nor OxiDD. Historical presets, including inherited sweep portfolios, and the empty
historical-default path retain their previous lists. Meson's `intro-buildoptions.json`
records the shipped membership; there is no runtime append or helper toggle.

It races one bounded original-game decision worker for eligible inputs alongside the
existing arms. `--arms both:obligation:spot` runs it alone, with supervised preparation;
an ineligible input returns UNKNOWN without starting an obligation solver. Explicit `--arms`
lists run exactly their named arms. A preset without the arm also preserves the original
route. For the first shipping preset, the previous TLSF decision portfolio is:

```sh
previous_arms=real:small:backward,real:small:forward
previous_arms="$previous_arms,unreal:formula:spot-guarded-sparse"
previous_arms="$previous_arms,unreal:automaton:forward,both:gr1-real-lift:oxidd"
acacia-bonsai -T spec.tlsf --arms "$previous_arms"
```

For LTL input, omit the native GR(1) arm, which requires `-T`. Explicit `-r`/`-u` selections
also run only their requested routes. Synthesis (`-s`) uses the ordinary default route;
explicitly selecting this decision-only arm with `-s` is an error. Replay and native
checking retain their existing routes.

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
A portfolio without the arm allocates no mailbox, checks no eligibility, samples no helper
clocks/RSS and emits no helper events or workers. Membership uses the existing compile-time
`acacia_default_arms` registry option; its empty default remains unchanged across frontends.

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
opposite-polarity selection, post-simplification eligibility, ordinary-only selection, synthesis exclusion, exceptions,
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
