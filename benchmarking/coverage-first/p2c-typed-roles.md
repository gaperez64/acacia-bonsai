# P2c: typed REAL lifting, opt-in

The experiment is `--r-typed-roles on`; `off` is the incumbent default.
It applies to the combined R/direct worker and the pure R measurement hook.
The option is recorded before native preparation in phase attribution. Enabled
typed roles are also recorded in checked proof evidence; off-mode evidence retains
the incumbent bytes. No worker is added, removed, or reordered.

R and U share the frontend-bound variable/monitor inventory, distinct variable
role signatures, ordered anchor selection, and normalized projection keys in
`gr1_shared.hh` / `gr1_typed.cc`. R retains its representative-based learning,
selecting the first structurally compatible goal/fairness predicate in each seed
by distinct typed kind and ordered owner equality patterns. Every selected seed
predicate must reconstruct exactly; the complete target proof is independently
checked. Requiring all compatible extraction ranks to be identical was rejected
because goal-order-dependent seed ranks would exclude incumbent candidates.
Shared state stays unowned; pairwise monitors retain both ordered coordinates;
representation-bit positions remain local. Typed window eligibility includes
coordinate equality patterns and local variable inventories. Ambiguous siblings,
changed encoding widths, unsupported strict reductions, changed role inventories,
and unsupported bounded projections decline with typed causes.

Seeds are still expanded solely from the current source's declared PARAMETERS,
using the existing consecutive seed search and confirmation. All existing global
seed, subset, arity, BDD, artifact and proof budgets are unchanged. No tuning or
family selectors are introduced. Versioned API entry points preserve incumbent
public struct layouts. Region-first candidate construction and the independent
checker are unchanged: a candidate cannot produce a verdict.

The correctness panel covers shared and pairwise monitors/goals, same-shaped
source roles, renamed/reordered declarations, parameterized fairness, local enum
bits, changing encoding width, ambiguous metadata, and a well-formed false
winning predicate rejected by the actual target checker. It checks one successful
target verification and one prepared original target in the combined worker.
Parameterized fairness and the pairwise controls reach learning but safely
decline where seed ranks exceed the unchanged bounded projection grammar; these
declines are not claimed as supported proofs. Existing R regressions include `native_lift_api`, `native_real_both_api`,
`native_both_api`, `native_failure_census`, the native param/lift CLI tests,
`subprojects/tlsf-tools/test/oracle/test_native_lift_differential.py`, and the Acacia lift pytest oracles. The general
40-row G1 sentinels remain in `tests/suites/benchmarks/regress-expected.tsv` (the driver runs G1).

The historical pure-R archive `gr1-par2-20260923-perarm-m1` contains 19 REAL
successes: the decomposed-lock instances at sizes 4–15, and buffered arbiters at
sizes 4–10. The verified combined rows in `opt20260927-p4n4` add the decomposed-lock
instance at size 16. The union for preservation contains 20 inputs.
`prepare-p2c-screen.py` constructs lists from these explicit read-only rows and
fresh P1 attribution, without routing decisions in solver code. The correctness
recheck uses `tests/check-p2c-r-regressions.py` with the pure R hook and records
verdicts only, not timing measurements.

Fresh P1 supplies seven P2c targets at 17 s. Four have observed R schema stops;
the three lift inputs have no observed R terminal stop in the fresh records.
The seven-input list is screened at both 17 and 60 s. This choice is a 60 s
screen of the fresh frontier, not a manufactured 60 s obstruction census. The
historical 60 s missing list has six inputs and omits the ordinary arbiter.
Raw lists and selection provenance live in `_bm-logs.p2c/lists/`.

The historical 19 are selected from
`/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/restore/gr1-par2-20260923-perarm-m1/benchmarking/gr1-par2-20260923/campaign/perarm-m1/legs/arm-7/arm-7-cap60.tsv`.
The combined preservation rows are read from
`/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/restore/opt20260927-p4n4/build_scratch/optimize-20260927/p4n4/standalone.tsv`;
the fresh verified R phase records are under
`/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/p1-ablation/full17/phases/D/17`.
These files are read-only evaluation evidence. List preparation is reproducible:

```sh
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 \
  benchmarking/tools/prepare-p2c-screen.py \
  --package-targets /home/gperez/GIT-repos/acacia-bonsai/build_scratch/cov/wt-p1-ablation/_bm-logs.p1-ablation-report/package-targets.tsv \
  --historical-r-rows /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/restore/gr1-par2-20260923-perarm-m1/benchmarking/gr1-par2-20260923/campaign/perarm-m1/legs/arm-7/arm-7-cap60.tsv \
  --route-rows /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/restore/opt20260927-p4n4/build_scratch/optimize-20260927/p4n4/standalone.tsv \
  --phase-root /home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/p1-ablation/full17/phases/D/17 \
  --out _bm-logs.p2c/lists
```

No race, timing, resource or admission campaign is run by this implementation
agent. New schema recognition, verified standalone solves and incremental race
coverage must be reported separately by the driver. #206 remains open; default
admission requires verified gains, no incumbent losses and no new memory failures.

## Validation

The correctness build passes 63 Acacia unit tests. Full pytest passes 1,911 tests,
with 34 skips and 23 passing subtests. The pure R correctness recheck passes all
20 evidence-selected inputs with the switch both off and on (40 verified REAL
results), including the historical 19. The new native typed-role unit passes;
the pairwise and parameterized-fairness grammar limitations are explicit negative
controls. Ruff, the runtime-option frontend census, and clang-format 22.1.8 pass.
Posets passes all 18 tests. The release binary passes the native CLI off/on
controls and contains no native test-mutation hooks. All 185 tlsf-tools tests pass
after two harness fixes: locate the enclosing Meson metadata for embedded builds,
and exercise stamp revision-mismatch failures with a controlled read-only source
identity even when the actual OxiDD export lacks Git metadata. The real export's
archive hash is checked separately; the missing Git identity remains disclosed.

The review follow-up preserves incumbent off-mode evidence by emitting
`r_typed_roles` only when enabled. `native_lift_artifacts` checks pure R, combined
R, and combined U through legacy, v1, v2-null, and v2-off APIs. A fresh library
built from `f6b5ecc99079b3a32a785b81335033e551265d65` produces byte-identical games,
certificates, policies, sidecars, evidence and verdict records for all 12 controls.
Checker JSON is also byte-identical after normalizing only CPU/elapsed-time
values. Reproduce that comparison with
`bash build_scratch/p2c-fix/reproduce-incumbent.sh`; the script exports the pinned
source using read-only `git archive` and builds offline with local dependencies.
Follow-up logs are in `build_scratch/p2c-fix/logs/`. The 63 Acacia units and full
pytest pass again. All 186 tlsf-tools tests pass across the full run and an
isolated retry of `native_failure_census`: its initial 90-second runner timeout
was extended to 180 seconds for the retry, with identical assertions and solver
budgets. The generated decline-cause census is refreshed for shifted source line
numbers; reviewed cause expectations are unchanged.

## Reproduction

The Acacia base is `e6cd4156dde0fdaea4aaf8a9115b0930ccc7b2b1` (dirty worktree),
and tlsf-tools starts at `f6b5ecc99079b3a32a785b81335033e551265d65` on
`p2c-typed-roles`. Local yyjson 0.12.0 is
copied from the existing checkout; setup uses `--wrap-mode=nodownload`.
OxiDD's stamp verifies its archive hash; its recorded revision is
`bc4354cbb86f3940bacc6675e4a0d2abb97ed8c4` (worktree git identity unavailable).
The compiler is GCC 16.2.1, Meson is 1.11.2, and Spot is 2.16.
Original build and test logs live under `build_scratch/p2c/`. The frozen release
executable predates the review repair; its SHA-256 is
`acb0930e9a9e51a0c90c5c4e0687825ec13a957b3a3b1ff04c850b5e55008f7d`.
Rebuild the measurement executable from the repaired sources before driver
screens; the follow-up validates a fresh correctness build.
Binary and source diff hashes are in `_bm-logs.p2c/pins.json`. No changes are staged
or committed. Build objects and the failed setup scratch were deleted; binaries,
test logs and build metadata remain for review and the driver's screens. One
initial setup attempted the absent yyjson wrap download and failed; the completed builds use the already-present local
source with `--wrap-mode=nodownload`. No dependency was fetched.

```sh
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig
export PYTHONPATH=/usr/local/lib64/python3.14/site-packages
export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib64:$PWD/build_scratch/p2c/correctness/src/python
export NINJA=/usr/bin/ninja
P2C_PRESET=$(/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 \
  scripts/acacia-config.py list-group docker_default | head -n 1)
meson setup build_scratch/p2c/correctness --wrap-mode=nodownload \
  --buildtype=debugoptimized -Dbuild_python=true \
  $(/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 scripts/acacia-config.py \
    meson-args "$P2C_PRESET")
meson compile -C build_scratch/p2c/correctness -j 3
meson setup build_scratch/p2c/posets subprojects/posets --wrap-mode=nodownload \
  --buildtype=debugoptimized
meson compile -C build_scratch/p2c/posets -j 3
meson test -C build_scratch/p2c/posets --num-processes 3 --no-rebuild
meson test -C build_scratch/p2c/correctness --suite unit --num-processes 3 --no-rebuild
meson test -C build_scratch/p2c/correctness tlsf-tools: \
  --num-processes 3 --no-rebuild
PYTHONPATH=/usr/local/lib64/python3.14/site-packages:$PWD/build_scratch/p2c/correctness/src/python \
  /home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 -m pytest tests/pytest/ -q
/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 tests/check-p2c-r-regressions.py \
  --binary build_scratch/p2c/correctness/src/acacia-bonsai \
  --list _bm-logs.p2c/lists/incumbent-R.list \
  --tlsf-map tests/suites/benchmarks/syntcomp26/tlsf-sources.tsv \
  --corpus /home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus
meson setup build_scratch/p2c/measurement --wrap-mode=nodownload \
  --buildtype=release -Db_lto=true -Dacacia_compiler_profile=release \
  $(/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3 scripts/acacia-config.py \
    meson-args "$P2C_PRESET")
meson compile -C build_scratch/p2c/measurement -j 3 acacia-bonsai
```

Driver commands (four matched full-portfolio screens; no routing deadline):

```sh
P2C_TREE=/home/gperez/GIT-repos/acacia-bonsai/build_scratch/cov/wt-p2c-roles
P2C_PY=/home/gperez/GIT-repos/acacia-bonsai/.venv/bin/python3
P2C_SCREEN=/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.cov-20261006/screen.py
P2C_BIN="$P2C_TREE/build_scratch/p2c/measurement/src/acacia-bonsai"
P2C_TREATMENTS=$($P2C_PY -c 'import json,sys; print(json.dumps([
  ["r-typed-off", sys.argv[1], sys.argv[2], "--r-typed-roles off"],
  ["r-typed-on", sys.argv[1], sys.argv[2], "--r-typed-roles on"]]))' \
  "$P2C_BIN" e6cd4156dde0fdaea4aaf8a9115b0930ccc7b2b1)
for P2C_CAP in 17 60; do
  env -u ACACIA_OUTER_DEADLINE_MONOTONIC "$P2C_PY" "$P2C_SCREEN" \
    "$P2C_TREE/_bm-logs.p2c/driver/targets-$P2C_CAP" \
    "$P2C_TREE/_bm-logs.p2c/lists/targets-$P2C_CAP.list" \
    "$P2C_CAP" "$P2C_TREATMENTS"
  env -u ACACIA_OUTER_DEADLINE_MONOTONIC "$P2C_PY" "$P2C_SCREEN" \
    "$P2C_TREE/_bm-logs.p2c/driver/incumbent-R-$P2C_CAP" \
    "$P2C_TREE/_bm-logs.p2c/lists/incumbent-R.list" \
    "$P2C_CAP" "$P2C_TREATMENTS"
done
```
