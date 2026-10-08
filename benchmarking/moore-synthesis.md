# Moore controller correctness investigation — 2026-10-07

**Conclusion: Acacia's emitted Moore controller is wrong; the reviewer's timing and
counterexample are correct.** The minimal specification is realizable, but the
controller loses the first real input. The defect is an initialization mismatch
between the TLSF Moore-to-Mealy formula reduction and the subsequent controller
conversion. The fix and independent semantic regression tests were developed in the
investigation worktree. They are now ported directly onto master in
fix/moore-synthesis-controllers at d9d3fd437c425861eed803872e26c60a2eb0d355,
with all changes uncommitted. No PR was created.

The original investigation below is preserved from e0b8ea21. Its historical
validation results and build_scratch/moore/ artifacts belong to the separate
investigation worktree; the master-port validation is recorded at the end.

The inspected investigation branch was investigate/moore-synthesis. Its actual starting HEAD was
cb0b2f8567dacaade8ed4380b3d58556122273d7, rather than the task's stated d9d3fd43.
Read-only Git comparison finds no difference between those revisions in
src/solver/mealy_to_moore.{cc,hh}, src/tlsf_frontend.cc, or
tests/check-real-correct.sh.in. All four supplied AIGs fail identically. The
pinned frozen S4 binary independently reproduces the failure.

For round t, write x_t for the environment's input, p_t for the controller's
output, and s_t for its state before the round. Mealy permits
p_t = O(s_t,x_t) and updates s_{t+1} = T(s_t,x_t). Moore requires
p_t = O(s_t): it may depend on the initial state and inputs
x_0,...,x_{t-1}, but not on x_t. Its initial output is chosen before any real
input. The LTL word contains the paired letters (x_t,p_t), so
G(X p <-> x) requires p_{t+1}=x_t for every t>=0; p_0 is unconstrained.
It is realizable under both semantics.

TLSF SEMANTICS describes the source formula's timing; TARGET describes the
requested implementation. They must not be conflated. The target adaptation is:

| Source semantics | Target | Target-frame formula |
|---|---|---|
| Moore | Moore | phi |
| Mealy | Mealy | phi |
| Moore | Mealy | phi[input := X input] |
| Mealy | Moore | phi[output := X output] |

The repository's local corpus documentation identifies the official TLSF format
and its model-checking tool in
[the SYNTCOMP corpus readme](../tests/syntcomp-benchmarks/readme.md) and
[the TLSF track readme](../tests/syntcomp-benchmarks/tlsf/readme.md).
Both point to the [TLSF specification](https://arxiv.org/abs/1604.02284).
There is no full SYNTCOMP rulebook or TLSF specification PDF/text in this checkout,
and no docs/ directory. Thus no purported quote from those documents is used
here. The adaptation above is directly inspectable in
[tlsf-tools' target adaptation](../subprojects/tlsf-tools/src/lib/spec.c)
(spec_adapt_target, lines 80–105); the effective-target selection is in
[Acacia's frontend](../src/tlsf_frontend.cc) (lines 70–81).
The original source semantics and target survive in metadata. Strict source
semantics use the same timing adaptation after their obligation semantics are
lowered; the added strict-label cases have no assumptions, and do not constitute
a complete independent audit of strict assumption semantics.

An AIGER circuit evaluates output and next-latch functions from the **current**
latch valuation and the current input, then updates all latches simultaneously.
There is no implicit extra sampling delay for a Moore target. Moore's output
function must be independent of current inputs on every reachable state.
For a latch row 'current next [reset]', omitted reset means zero; explicit reset
is zero, one, or the latch's own literal for an unspecified initial value.
Undefined initial values require a checker policy; this investigation's small
oracle universally enumerates them. The supplied and synthesized controllers
all use specified zero resets. Spot's emitter uses zero-reset physical latches;
a logical initial one can be represented by complementing the stored latch and
its exposed function. Local references include
[tlsf-tools' AIGER API](../subprojects/tlsf-tools/include/tlsf/aiger.h),
the canonical AIGER library's
[aiger.h](/home/gperez/GIT-repos/strahler-knors/src/aiger.h:206), and Spot's
[initialization API](/usr/local/include/spot/twaalgos/aiger.hh:425).

The installed /usr/local/bin/ltlsynt is Spot 2.16. Its manual describes
--semantics=Moore as output-first and Mealy as input-first
[at the semantics option](/usr/local/share/man/man1/ltlsynt.1:47).
The corresponding local
[Spot documentation](/home/gperez/opt/build/spot-2.16-usrlocal/doc/org/ltlsynt.org:146)
states that Moore cannot see the current input. One example sentence in that
document incorrectly says G(i <-> X o) is unrealizable under Mealy.
The executable verifies it under both semantics; that sentence is not used as
evidence. The direct-copy formula G(p <-> x) is realizable under Mealy and
unrealizable under Moore, as expected.

Spot's --verify translates the negated original formula and tests its
intersection with the emitted AIG's as_automaton(false). The conversion labels
edges with outputs evaluated from the source state and current input and puts the
next-latch valuation in the destination state. This applies to both Mealy and
Moore circuits, without a timing adjustment in verification. For a non-AIG
strategy, Spot's is_valid_strategy checks the strategy against the original
formula. See the local
[verification implementation](/home/gperez/opt/build/spot-2.16-usrlocal/bin/ltlsynt.cc:1039)
and [AIG automaton construction](/home/gperez/opt/build/spot-2.16-usrlocal/spot/twaalgos/aiger.cc:1248).
The verification check establishes the formula language; the independent oracle
also explicitly checks Moore output independence.

The supplied AIG is:

~~~text
aag 4 1 2 1 1
2
4 1
6 8
6
8 2 4
i0 x
o0 p
~~~

Let its latches be r (literal 4) and z (literal 6). Its equations are
r_0=z_0=0, r'=1, z'=x & r, and p=z.

| Round | Input | Old r | Output p | Next z |
|---|---|---|---|---|
| 0 | 1 | 0 | 0 | 0 |
| 1 | 0 | 1 | 0 | 0 |
| 2 onward | 0 | 1 | 0 | 0 |

At round 1, p_1=0 contradicts x_0=1. This is precisely
'!p & x; cycle{!p & !x}'. Resetting r to one would repair these equations;
treating a zero-reset AIG as though r were initially one is an incorrect
checker interpretation.

[The independent oracle](../tests/moore_aiger_oracle.py) does not use Spot's
AIG parser, AIG simulation, synthesis code, or formula translation. It parses AAG,
evaluates literals/AND gates itself, and exhaustively explores reachable products
with a deterministic safety monitor. For the minimal formula, the monitor is
initial, expect-false, expect-true, or rejecting: the first letter records
x; subsequent letters require p to equal the recorded input and then record
the new input. Reachability of rejection is equivalent to a violating infinite
word because the circuit is total. All input valuations are explored, all
specified initial states are honored, and a state cap raises an inconclusive
error instead of passing. The reviewed product reaches rejection after the
two-letter prefix shown above. Positive controls, complemented literals, gate
evaluation, explicit/undefined resets, Moore independence, and the cap are
checked by [pytest tests](../tests/pytest/test_moore_aiger_oracle.py).

The second checker is build_scratch/moore/check_aiger.cc, a standalone Spot
parser/automaton intersection helper. It independently reproduces the exact
reported lasso on the reviewed controller and accepts the repaired one.

The core Spot cross-check is:

~~~sh
/usr/local/bin/ltlsynt -f 'G (X p <-> x)' --ins=x --outs=p \
  --semantics=Moore --aiger --verify --hide-status
~~~

It succeeds and emits q'=!x, p=!q, with q_0=0. Thus p_0=1 and
p_{t+1}=x_t. Both the explicit oracle and the standalone Spot product accept
this circuit. A second invocation with --bypass=no --obligation-synthesis=no
--decompose=no also succeeds and verifies, exercising the game construction.
Spot's own --verify and the explicit oracle both accept all 86 non-strict
Moore/Moore generated specifications.

The cause is that Acacia solves Moore/Moore requests using the effective Mealy
formula. For the example this is G(X p <-> X x). That formula ignores the
initial input and permits the initial Mealy output to be arbitrary.
The emitted internal strategy has the startup latch r, output r & x, and
r'=1. Delaying its output without advancing its state causes the first real
input to be consumed in this otherwise ignored startup round.

More generally, let the Mealy strategy have transitions T and outputs O,
with zero initial state s_0. Choose a dummy input d=0. To implement the
original Moore formula, run the Mealy strategy on
d,x_0,x_1,... and expose its outputs y_0,y_1,y_2,... at original rounds
0,1,2,... . Correct conversion therefore initializes the emitted state to
(T(s_0,d), O(s_0,d)), and on each real input x updates
(s,y) to (T(s,x),O(s,x)), with output y.
The old converter initialized (s_0,O(s_0,d)), so it duplicated the initial
round instead of consuming it.

[The local fix](../src/solver/mealy_to_moore.cc) optionally evaluates both
initial outputs and the initial **successor** latch valuation. Logical true
latch resets are encoded by complementing their state representation, retaining
ordinary zero-reset AIGER emission. [The caller](../src/solver/solver_invoker.cc)
selects this mode only for Moore source semantics and a Moore target, including
Strict,Moore. Mealy source/Moore target conversion retains its existing output
delay: its target formula shifts outputs with X, so physical output zero is
unconstrained and the source must start in s_0. Both the regular strategy and
no-input lasso emission paths use the corrected selection. The repaired minimal
AIG has r'=0, z'=x & !r, p=z, with both physical resets zero.

[The generated panel](../tests/check-moore-synthesis.py) enumerates all four
one-input and all sixteen two-input Boolean functions, delays 1–3, both initial
output presets where tested, and two no-input alternating-output controllers.
Each of eight source-semantics/target combinations has 86 cases.

| Source semantics | Target | Clean original | Frozen S4 | Fixed |
|---|---|---:|---:|---:|
| Moore | Moore | 32/86 | 32/86 | 86/86 |
| Strict,Moore | Moore | 32/86 | 32/86 | 86/86 |
| Moore / Strict,Moore | Mealy | 172/172 | 172/172 | 172/172 |
| Mealy / Strict,Mealy | Moore | 172/172 | 172/172 | 172/172 |
| Mealy / Strict,Mealy | Mealy | 172/172 | 172/172 | 172/172 |
| Total | | 580/688 | 580/688 | 688/688 |

There are 108 bad controllers in the affected 172-case panel. Strategies whose
dummy round leaves their internal state unchanged can still convert correctly. This is a
correctness sample, not an estimate of corpus prevalence. All 516 controls
outside Moore-source/Moore-target conversion emit byte-identical AIGs before and
after the fix. The frozen S4 executable is
/home/gperez/GIT-repos/acacia-bonsai/build_s4_50384cf6/src/acacia-bonsai,
SHA-256 2e10b4feb2e455ce13d32007cbcc1e5cccbee4024d3291c2473409c8450cf535,
matching benchmarking/baselines.tsv.

The original synthesis suite does **not** validate native Moore controllers
semantically. Its 429 legacy entries all pass raw .ltl files via -F, selecting
the default Mealy frame; [Meson constructs those entries](../tests/meson.build)
without their TLSF timing metadata. [The shell harness](../tests/check-real-correct.sh.in)
compares exit statuses to expected realizability and never reads the AAG.
Assertions in checked Acacia builds model-check the internal Mealy AIG against
the effective Mealy formula, rather than checking the final converted AIG.
The former conversion unit test only asserted the output-delay behavior.
The new moore-synthesis-semantics test checks 64 controllers through the public
native TLSF CLI against the explicit monitor products, in the unit, tlsf,
and synthesis suites when the frontend is enabled. The conversion unit test
also exercises nonzero logical successor-state resets over every five-input
Boolean word.

Complete validation results and reproduction commands follow below. Raw
correctness artifacts are retained under build_scratch/moore/; no benchmark
timings, coverage campaigns, staging, commits, or publication were performed.

The full synthesis run completes with 344 passes, 87 synthesis timeouts, and no
other failures. This includes 342 successful legacy Mealy synthesis cases and the
two synthesis unit/regression entries. A separate Spot product check verifies
312 of the 342 emitted legacy controllers. Twenty-nine exceed the explicit
checker limits (more than 8 inputs or 12 latches), and the Sensor.ltl check reaches
its 15-second limit. Those 30 certificates and the 87 synthesis timeouts are
inconclusive; no invalid legacy controller was found in the completed checks.
Each product check has a 2 GiB address-space limit. These global structural and
resource limits bound explicit automaton construction, and do not select by
expected verdict or benchmark identity. All old suite entries use the Mealy
frame, so this is a regression check, not a Moore corpus prevalence measurement.

The checked correctness build uses debugoptimized, the native TLSF frontend,
and Python bindings, with native GR(1) arms disabled. The first default Meson
setup automatically attempted missing yyjson downloads and failed. Subsequent
configuration uses --wrap-mode=nodownload and a local yyjson 0.12.0 static
library/pkg-config file built from the already cached source. All build and
test temporary files use this worktree's build_scratch/moore/tmp. Compile jobs
and Meson test processes are capped at 3. Python/pytest/ruff use the repository's
.venv. The final full pytest run uses the installed Spot Python package and the
newly built Acacia Python module, as CI does.

Original reproduction, from the investigation worktree:

~~~sh
export TMPDIR="$PWD/build_scratch/moore/tmp"
export PKG_CONFIG_PATH="$PWD/build_scratch/moore/yyjson/lib/pkgconfig:/usr/local/lib/pkgconfig"
export PYTHONPATH="/usr/local/lib64/python3.14/site-packages:$PWD/build_scratch/moore/build/src/python"
export LD_LIBRARY_PATH=/usr/local/lib
PY=/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3

meson compile -C build_scratch/moore/build -j 3
"$PY" -m pytest tests/pytest/ -q
meson test -C build_scratch/moore/build --suite unit --num-processes 3
meson test -C build_scratch/moore/build --suite synthesis --num-processes 3
"$PY" tests/check-moore-synthesis.py build_scratch/moore/build/src/acacia-bonsai \
  --exhaustive --output-dir build_scratch/moore/generated-after
"$PY" build_scratch/moore/crosscheck_spot.py
"$PY" build_scratch/moore/verify_legacy.py
~~~

The original source changes moved three diagnostics-census line references;
its generated decline-causes.md was refreshed accordingly. That attribution-only
change is excluded from the master port. Large object caches are removed and
large Meson logs compressed after validation; executables, controller artifacts,
checker source, exact-check result JSON, test summaries, and compressed full logs
are retained under build_scratch/moore. Nothing is staged or committed.

## Master port — 2026-10-07

This port starts at master d9d3fd437c425861eed803872e26c60a2eb0d355 on
fix/moore-synthesis-controllers. The tlsf-tools submodule remains pinned at
404963544436013d11ca232f4721d6da4499681f. Changes are direct working-tree edits;
HEAD and the index remain unchanged. The source/target timing choice is the same
as e0b8ea21, including Strict,Moore and both controller emission paths. There
are no new solver options, thresholds, attribution events, or worker records.

All port scratch and temporary files live under build_scratch/moore-port/,
with TMPDIR set to its tmp/ subdirectory. The checked build uses debugoptimized,
assertions, the native TLSF frontend, and Python bindings; native GR(1) arms
remain disabled. Meson setup uses --wrap-mode=nodownload and a local yyjson
0.12.0 static dependency rebuilt from already cached source within this task's
scratch directory. There was no download attempt. Compile jobs and Meson test
processes are capped at three. The repository .venv supplies Python, pytest,
and ruff. Full pytest imports installed Spot 2.16 and this build's Acacia module.

For a matched master reference, the unmodified mealy_to_moore.{hh,cc} and
solver_invoker.cc are read with git show d9d3fd43 into scratch. The two original
solver objects are compiled with the checked build's flags and original header,
and linked with the remaining unchanged objects. No tracked source is restored
or switched. The helper and exact compile/link arguments are retained as
build_master_reference.py and master-reference-commands.json in the task scratch.

The exact panel reproduces master at 580/688 and validates the port at 688/688.
Both Moore/Moore rows improve from 32/86 to 86/86; all other source/target rows
remain 86/86. All 108 original failures occur in Moore-source/Moore-target
requests. All 516 unaffected controllers are byte-identical across the matched
master and port binaries. This is a generated correctness panel, not a timing
campaign or corpus-prevalence estimate.

On the minimal native TLSF specification G(X p <-> x), explicit simulation
with x_0=1 produces p_1=0 on master and p_1=1 after the port. The standalone
Spot parser/product checker reproduces exactly !p & x; cycle{!p & !x} on master
and verifies the repaired final AIG. Spot's default Moore --aiger --verify
invocation and its forced game-construction variant both verify the formula.
The full Spot/explicit-oracle panel passes 86/86. Current raw controller and
checker results are retained in generated-master/, generated-fixed/,
spot-generated/, master-panel-comparison.json, and minimal-comparison.json.
The earlier frozen-incumbent and legacy Spot-product results above are
historical evidence from the investigation, not additional port test runs.

Reproduction, from this port worktree after preparing the cached local yyjson
library/pkg-config file described above:

~~~sh
export TMPDIR="$PWD/build_scratch/moore-port/tmp"
export PKG_CONFIG_PATH="$PWD/build_scratch/moore-port/yyjson/lib/pkgconfig:/usr/local/lib/pkgconfig"
export PYTHONPATH="/usr/local/lib64/python3.14/site-packages:$PWD/build_scratch/moore-port/build/src/python"
export LD_LIBRARY_PATH=/usr/local/lib
PY=/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3

meson setup build_scratch/moore-port/build --buildtype=debugoptimized \
  --wrap-mode=nodownload -Dacacia_enable_tlsf_frontend=true -Dbuild_python=true
meson compile -C build_scratch/moore-port/build -j 3
"$PY" -m pytest tests/pytest/ -q
meson test -C build_scratch/moore-port/build --no-rebuild --suite unit --num-processes 3
meson test -C build_scratch/moore-port/build --no-rebuild --suite synthesis --num-processes 3
"$PY" tests/check-moore-synthesis.py build_scratch/moore-port/build/src/acacia-bonsai \
  --exhaustive --output-dir build_scratch/moore-port/generated-fixed
"$PY" build_scratch/moore-port/build_master_reference.py
"$PY" tests/check-moore-synthesis.py build_scratch/moore-port/acacia-master \
  --exhaustive --output-dir build_scratch/moore-port/generated-master
# The previous command deliberately exits 1: the original has 108 invalid AIGs.
"$PY" build_scratch/moore-port/crosscheck_spot.py
build_scratch/moore-port/check-aiger build_scratch/moore-port/fixed-minimal.aag 'G (X p <-> x)'
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/ruff check \
  tests/moore_aiger_oracle.py tests/check-moore-synthesis.py tests/pytest/test_moore_aiger_oracle.py
clang-format --dry-run --Werror src/solver/mealy_to_moore.{hh,cc} \
  src/solver/solver_invoker.cc tests/mealy_to_moore_test.cc
git diff --check
~~~

## Exact differences from e0b8ea21

The port changes nine source/report files against master d9d3fd43. It preserves
all behavior of the reviewed correctness fix. No changes from the unrelated
cb0b2f85 attribution base were imported.

- `src/solver/mealy_to_moore.hh`, `src/solver/mealy_to_moore.cc`, and
  `tests/mealy_to_moore_test.cc` match the reviewed contents exactly after
  clang-format 22.1.8. Their only differences from the commit are formatting:
  continuation indentation in the header; signature, reset expression, and
  next-latch call wrapping in the converter; constructor wrapping in the test.
- `src/solver/solver_invoker.cc` implements the same three conversion modes,
  metadata selection, normal strategy conversion, and no-input lasso conversion.
  After formatting both files, its entire difference from the reviewed file
  consists of these five inherited master sections: (1) the translation wrapper
  retains master's exception handling without `worker_stopped` calls or the
  added bad_alloc/catch-all handlers; (2) requested-backend capture uses master's
  backend argument rather than `active_worker_record`; (3) the lazy-provider
  route record is absent; (4) the frozen-fallback route record is absent; and
  (5) the solve-game route record is absent. These belong to the unmerged
  attribution base, not the Moore fix. Formatting also sorts existing includes
  and wraps existing code to the repository style; no attribution code is added.
- `tests/meson.build` adds exactly the same seven-line semantic regression.
  Its other differences from the reviewed file are inherited master contents:
  no record-transport/native-attribution-api/native-structure-guard tests;
  the phase-record failure worker retains its test-hook compile flag;
  native-release-no-hooks remains release-only; spot-lazy-buchi-view keeps its
  original Spot/BuDDy dependencies; game-backend-integration does not force the
  forward-safety macro; no attribution-contracts/attribution tests; and the
  game-backend-integration argument still reads the forward-safety build option.
  None of those unrelated base changes belongs in this port.
- `tests/moore_aiger_oracle.py`, `tests/check-moore-synthesis.py`, and
  `tests/pytest/test_moore_aiger_oracle.py` are byte-for-byte identical to the
  reviewed commit. The monitor, panel, caps, timeouts, and expected results are
  unchanged.
- The investigation write-up is moved from
  `benchmarking/coverage-first/moore-synthesis.md` to
  `benchmarking/moore-synthesis.md`, because master has no coverage-first
  directory. Relative links change from `../../` to `../`. Its opening and
  reproduction labels distinguish historical investigation evidence from this
  port, the original summary is replaced by current port results, and the
  master-specific comparison, validation, and reproduction details are appended.
- `benchmarking/coverage-first/decline-causes.md` is omitted entirely, as
  requested: it only refreshed attribution-census line numbers.

Complete normalized caller and Meson differences are retained at
`build_scratch/moore-port/invoker-differences-from-reviewed.diff` and
`build_scratch/moore-port/meson-differences-from-reviewed.diff`. An automated
comparison asserts the three formatted C++ files and three exact Python files
above match e0b8ea21.

## Master-port validation results

The full synthesis suite has 344 passes, 87 timeouts, and zero other failures.
All 431 per-test statuses match the recorded e0b8ea21 investigation run exactly,
including the identities of all 87 timeouts. Meson exits 1 because of those
permitted timeouts; they are explicitly inconclusive, not successful semantic
checks. There are no false-verdict markers in the completed harness results.
Full pytest has 1,171 passes, 31 skips, and 23 passing subtests. Its count differs
from the historical 1,917 because the investigation's unrelated unmerged base
had additional tests; this run used all of master's tests/pytest/ with the new
oracle tests, without exclusions. The unit suite passes all 53 tests, including
the new 64-controller native-TLSF semantic regression.

No configuration options changed, so the conditional config-frontend check was
not required. Ruff and clang-format 22.1.8 pass on all touched Python/C++ files;
the oracle/hardcoding pytest panel has 109 passes and one skip. The normalized
comparison confirms the converter/header/C++ unit test match e0b8ea21 after
formatting and all three new Python files match byte-for-byte. The final source
review confirms exactly nine requested files changed, no new attribution
records, and no staged changes. Large object caches are removed and large logs
compressed after validation; executables, Python bindings, controller artifacts,
checker sources, complete compressed logs, and compact summaries are retained.
Build digests and compiler/dependency pins are in build-provenance.json;
validation-summary.json contains the complete current-port result summary.

SUMMARY: Ported the dummy-initial-transition Moore correctness fix onto master d9d3fd43.
The converter now starts at the dummy round's successor while retaining its output.
Moore and Strict,Moore source/Moore-target requests use the new mode on both emission paths.
All 688 generated controllers pass; all 516 unaffected control AIGs remain byte-identical.
The port retains master's caller/build context and leaves all nine files uncommitted.
FILES: src/solver/mealy_to_moore.{cc,hh}; src/solver/solver_invoker.cc;
tests/mealy_to_moore_test.cc; tests/meson.build; tests/check-moore-synthesis.py;
tests/moore_aiger_oracle.py; tests/pytest/test_moore_aiger_oracle.py;
benchmarking/moore-synthesis.md.
TESTS: meson compile -C build_scratch/moore-port/build -j 3 -> PASS.
python3 -m pytest tests/pytest/ -q -> 1171 passed, 31 skipped, 23 passing subtests.
meson test --no-rebuild --suite unit --num-processes 3 -> 53 passed.
meson test --no-rebuild --suite synthesis --num-processes 3 -> 344 passed, 87 timeouts, 0 other failures.
Per-test comparison with reviewed run -> all 431 statuses identical, including every timeout.
Generated exact panel -> matched master 580/688; port 688/688; 516 byte-identical controls.
Spot Moore --aiger --verify plus independent oracle -> 86/86 passed; default/game minimal checks passed.
Standalone Spot final-AIG product -> original exact lasso reproduced; repaired controller VERIFIED.
Oracle plus hardcoding pytest -> 109 passed, 1 skipped.
Ruff on touched Python -> PASS.
clang-format 22.1.8 --dry-run --Werror on touched C++ -> PASS.
git diff --check -> PASS; HEAD and index unchanged.
DEVIATIONS: The exact per-file differences from e0b8ea21 are listed above: C++ formatting;
master's five attribution-free caller sections and original Meson context; unchanged Python
oracles; relocated report, repaired relative links, historical labels and fresh master evidence;
no attribution-only decline-causes.md change. No semantic change to the reviewed fix.
A fresh original-master reference was built in scratch for the 688-case comparison.
Historical frozen-incumbent and legacy Spot-product results were retained, not rerun.
OPEN QUESTIONS: None for this port. The driver retains the review/integration decision.
VERDICT: DONE
