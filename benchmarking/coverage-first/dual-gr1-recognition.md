# P4a step 1: exact complementary-GR(1) recognition

This opt-in research route runs only after an original native exact reduction
returns an applicability decline at `mp-class`. Enable it with
`--dual-gr1 recognize`; `off` is the default. The direct and combined native
GR(1) workers share the probe. Existing accepted games keep their incumbent
route. Recognition does no seed discovery, lifting, solving, policy generation,
or checking. An accepted reduction still returns UNKNOWN through the original
arm. No coverage improvement or portfolio admission is claimed.

The versioned `tlsf_gr1_recognize_dual_v1` API adds new result/observer types and
preserves all existing C structure layouts and entry points. It reuses the
exact reducer's classifier, complete deterministic Buchi monitors, one-hot
encoding, budgets and publisher. It rejects strict semantics and strict-only
reductions before construction. General complementation is not a closure rule
for GR(1): a dual outside the existing exact reducer's supported monitor classes
is declined with the ordinary typed cause.

## Construction and timing proof

The immutable source is freshly loaded through the existing pipeline, which
expands it and calls `spec_adapt_target` once. If SEMANTICS and TARGET differ,
this applies the existing AP delay once; otherwise it is the identity. We then
lower the complete target-adapted objective Phi, including INITIALLY, PRESET,
REQUIRE, ASSERT, ASSUME and GUARANTEE. Complementation applies to that objective,
not to exchanged fairness lists or exchanged assumption/guarantee sections.
For example, ordinary lowering has the shape
`E_init -> (S_preset & ((G E_require & A) -> (G S_assert & Gua)))`;
its Boolean/temporal negation retains the distinct scopes of all these parts.

Let i be the original environment move and o the original controller move.
For an input-first (Mealy) original the controller predecessor is
`forall i exists o`, including round zero. Its dual is output-first (Moore),
with original environment choices satisfying `exists i forall o`.
The transformed controller owns i and the transformed environment owns o.
To compile this dual into the reducer's input-first game, replace every dual
input AP o by X o in `not Phi`, leaving i unchanged. A compiled word `(u,c)`
represents the original word `i[t]=c[t], o[t]=u[t+1]`. Thus c[t] may depend on
u[0..t], which exposes only original outputs through round t-1. It cannot
observe original o[t]. The unused u[0] is universal: fix any one value to
extract a legal Moore strategy from a compiled winning strategy; conversely,
a Moore strategy ignores u[0] and implements a compiled one. This proves the
initial quantifier order as well as the order in every later round.

For an output-first (Moore) original, the dual is input-first (Mealy): original
o[t] precedes the dual controller's i[t]. Swap ownership and complement, with
no additional input-push. Apply these rules in the source TARGET frame, after
the pipeline's one adaptation. No adaptation of the already transformed AST
occurs. The uncompiled objective/partition/timing dual is an involution; its
round-trip test deliberately precedes scheduling compilation.

This follows the formula input-push in `solver_invoker.cc` and the matching
saved-input automaton construction in `utils/push_aps.hh`. The emitted-controller
`mealy_to_moore.hh` construction delays outputs and picks an initial value; it
is a synthesis export operation, not a substitute for this objective/game
transformation. No realizability simplification is applied in the swapped frame.

The exact reducer rebuilds deterministic monitors of the complemented objective.
Initial constraints and safety predicates are included in their languages;
they become exact recurrence monitors rather than strict safety release rules.
Every monitor is deterministic state with a derived guarantee/justice role,
not an additional controllable action. The complete derivation, original
frontend conjunct/declaration provenance, ownership map, original and dual
initial quantifiers, source/target timing, adaptation count and push count are
retained in construction/provenance JSON. Metadata hashes bind source bytes,
construction and encoded game. This is a recognition binding, not a checked
original-game proof. Future solving needs a separately reviewed proof mapping.

## Attribution and bounds

The existing nonblocking, bounded `PIPE_BUF` transport records:

- `dual_gr1_original_rejection`: the original exact `mp-class` applicability decline;
- `dual_gr1_construction`: constructed, or rejected with status, stage and typed cause;
- `dual_gr1_reduction`: accepted, or rejected with status, stage and typed cause;
- `dual_load`, `dual_construct`, `dual_reduce`: wall/CPU and memory costs, plus
  reduced artifact bytes, formula work and monitor counts/states.

Worker lifecycle records identify the `dual_gr1` route as reduction, with no
verified proof polarity. An accepted reduction ends as `dual-recognition-only`;
resource/deadline/cancellation/integrity failures remain STOPPED with their typed
cause. A missing outcome after an external kill remains censored. The existing
writer/drop/completeness machinery is unchanged; producer events open no files.

There is one attempt per original rejection and no recursive portfolio or
fallback solve. The existing global native limits remain unchanged: 4,096
formula nodes, 2,048 APs, 1,024 conjuncts, 1,024 nodes per conjunct, temporal depth
16, predicted monitor states 2^24, total monitor states 10,000, edges 150,000,
64 MiB per artifact, and the incumbent arm RSS share. The probe retains the same
absolute invocation deadline. Spot calls are cooperatively bounded between calls;
the existing forked worker and external driver cap supply the hard time bound.
No thresholds were tuned on the frontier.

## Correctness and evidence

The explicit source oracle uses hand-built parity monitors and a turn-based
Zielonka solver. An independent reachable explicit generalized-Buchi game solver
checks the resulting AAGs. All 16 Boolean predicates are tested as initial,
safety and stability objectives for both source timing conventions and both
TLSF targets (192 cases). Equality's original Mealy controller is REAL;
its correctly compiled environment dual loses. The intentionally mistimed
Mealy dual wins, showing that the sentinel discriminates the illegal dependency.
Renamed controls retain unused signals. Lasso checks independently inspect
initial, safety and temporal/inconsistent assumptions and undo scheduling on
words to check complementation. Round trips, strict rejection, ownership,
provenance hashes, result reuse, typed caps/deadlines/cancellation and source
mutation rejection are covered. CLI tests require complete attribution and no
verification/winner event from recognition. Off-path comparisons use a frozen
clean-build executable and compare exit status, stdout and stderr bytes.

The checked build passes 64 root unit tests. The full CI-style pytest run with
locally built Python bindings passes 1,912 tests, with 34 skips and 23 passing
subtests. A frozen clean build matches exit/stdout/stderr on 21 native controls
and explicit-backward raw-LTL/synthesis controls; no dual telemetry occurs while
off. The checked build enables the forward backend required by the existing
attribution panel. Ruff, config-frontends, clang-format 22.1.8 and the hardcoding
guard pass. All 324 functional tests in the expanded tlsf-tools research
configuration pass. Layout and OxiDD stamp checks also pass in their existing
required standalone/checkout contexts. Its certificate stress test crossed the
initial 180-second harness cap; the final checked run passes with timeout
multiplier 2. Solver caps and deadlines are
unchanged. The optimized production binary passes the recognition CLI tests and
the release test-hook isolation guard.

Final verification commands (from this worktree):

```sh
export TMPDIR="$PWD/build_scratch/p4a"
export PYTHONPATH="$PWD/build_scratch/p4a/checked/src/python:/usr/local/lib64/python3.14/site-packages"
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64
meson compile -C build_scratch/p4a/checked -j 3
meson compile -C build_scratch/p4a/release -j 3
meson test -C build_scratch/p4a/checked --no-rebuild --num-processes 3 --suite unit
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 -m pytest tests/pytest/ -q
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/ruff check \
  scripts/acacia-dual-gr1-list.py tests/check-dual-gr1-cli.py \
  tests/pytest/test_dual_gr1_list.py \
  subprojects/tlsf-tools/test/oracle/test_dual_recognition.py
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 tests/check-config-frontends.py
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 tests/check-dual-gr1-cli.py \
  build_scratch/p4a/checked/src/acacia-bonsai build_scratch/p4a/checked \
  build_scratch/p4a/incumbent
```

The final integrated tlsf-tools invocation selects every subproject test except
its two existing metadata-context tests, then runs those in their proper contexts:

```sh
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 - <<'PY'
import json
from pathlib import Path
import subprocess
build = Path('build_scratch/p4a/checked')
tests = json.loads((build / 'meson-info/intro-tests.json').read_text())
names = ['tlsf-tools:' + row['name'] for row in tests
         if 'tlsf-tools' in row['suite'] and row['name'] not in
         ('source_layout', 'oxidd_build_stamp')]
subprocess.run(['meson', 'test', '-C', str(build), '--no-rebuild',
                '--num-processes', '3', '--timeout-multiplier', '2', *names], check=True)
PY
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 \
  subprojects/tlsf-tools/test/api/check_layout.py \
  subprojects/tlsf-tools build_scratch/p4a/tlsf-layout
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 \
  subprojects/tlsf-tools/test/api/test_oxidd_stamp.py \
  subprojects/tlsf-tools/scripts/oxidd_stamp.py \
  /home/gperez/GIT-repos/acacia-bonsai/subprojects/tlsf-tools/external/oxidd \
  build_scratch/p4a
```

Binary SHA-256 values:

- Frozen clean checked incumbent: `d40b7f3a157f58bcb3145ffdebd6fcb2de4823f1490503b9e6586a51a2bda6cc`.
- Checked candidate: `f266087f68d6101f03c15143aa818e6cf82544176174c066891e6f9033f5171d`.
- Optimized candidate: `41d48ebdee4438f5e47f07f73a795858dc80fc1209535e085c33428044eba4d3`.

The initial existing test harnesses used their standard temporary-file defaults;
final runs set `TMPDIR` to `build_scratch/p4a`. The offline yyjson cache is linked
read-only into the subproject; OxiDD's existing archive is reused and verified.
Cleanup reclaimed 1.73 GiB of task-only objects, old test executables and pytest
temporary files. Production/checked binaries, new probes, bindings, shared
libraries and evidence remain. Rebuild before rerunning the full Meson suites.

The 103-input list was derived read-only from the fresh guard screen's observed
`decline` records in `trusted_prepare`, not from instance identities in solver
code. `_bm-logs.p4a/mp-class-103.evidence.json` links each entry to its record.
The guard screen's precheck-off configuration is solely the cohort's provenance;
the proposed recognition treatment leaves this branch's incumbent structural
precheck at its default. This attribution-based branch predates P2b's separate
`--native-structure-guard-scale` research option; no P2b changes are imported.
The screen has not been run here, as the charter assigns measurements to the driver.
The independent P1 attribution TSV also confirms 17 unique native
`trusted_prepare` inputs with terminal reason `mp-class`.

Reproduce the list in this worktree:

```sh
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 scripts/acacia-dual-gr1-list.py \
  /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/p2b-guard-screen60 \
  _bm-logs.p4a/mp-class-103.list
```

The optimized measurement build is `build_scratch/p4a/release`; the checked
correctness build is `build_scratch/p4a/checked`. Source pins are Acacia
`e6cd4156dde0fdaea4aaf8a9115b0930ccc7b2b1` on `sprint/cov-p4a-dual-gr1` and
tlsf-tools `f6b5ecc99079b3a32a785b81335033e551265d65` on `p4a-dual-gr1`, plus
uncommitted P4a changes in each. Spot is 2.16. Binary hashes and original pins
are retained in `_bm-logs.p4a/`. No stage, commit, network operation or campaign
was performed. Existing worktrees and evidence were preserved.

From this worktree, the driver's exact standalone-native screen command is:

```sh
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 \
  /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/screen.py \
  "$PWD/_bm-logs.p4a/screen60" \
  "$PWD/_bm-logs.p4a/mp-class-103.list" 60 \
  "[[\"P4a-recognize\",\"$PWD/build_scratch/p4a/release/src/acacia-bonsai\",\"e6cd4156+p4a-uncommitted\",\"--arms both:gr1-real-lift:oxidd --dual-gr1 recognize\"]]"
```

The driver must examine original default-precheck declines separately from
attempted dual reductions, and classify syntax/semantics rejection separately
from resource exhaustion and incomplete telemetry. Affordable accepted exact
dual reductions justify a future decision/proof-binding experiment; recognition
itself adds no solve. If none are newly supported, apply P4's negative stop rule.
