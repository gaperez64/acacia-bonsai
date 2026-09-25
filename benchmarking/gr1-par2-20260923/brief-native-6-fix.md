# Brief — native step 6 fixes (from build_scratch/review-native-6/REPORT.md, ACCEPT WITH FIXES)

Workspace: this worktree, uncommitted step-6 state. NEVER stage or commit by any path. TIMED
BENCHMARKS are running on this machine: one job for every build (meson compile -j 1), bounded
serial test runs, never /tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig; Python with -s /
PYTHONNOUSERSITE=1. Use the existing debug build directories from step 6 if present.

Fix exactly these review findings (read the report for detail):

2. Restore the live guard in tests/pytest/test_acacia_lift_generic_guard.py: `assert not
   violations(...)` over the moved oracle wrapper and every oracle package .py file, at their new
   paths. Keep tlsf-tools' `scripts/gr1_monitor_game.py` in the scan if it still exists in the
   submodule checkout; if it does not, say so and drop only that entry. Show that a mutant (e.g. a
   family-name table inserted into a copy of an oracle file) makes the live check fail.
3. `--help` / `-h` must print usage and exit 0 regardless of ACACIA_OUTER_DEADLINE_MONOTONIC: handle
   help before that environment validation in src/acacia-bonsai.cc (smallest change; no other
   behaviour change). Extend tests/check-help.py: help with an invalid deadline variable exits 0;
   stderr empty; no files created in the cwd; an option whose required value starts with `-h`
   (pick a real value-taking option) is still parsed as a value, not as help.
4. Historical references rewritten to oracle/ paths that do not exist (e.g.
   brief-eligibility-budget.md:7 `oracle/acacia_lift/capabilities.py`; generic-design.md:14-25
   `oracle/acacia_lift/data/`, `generalizer.py`, `instantiate.py`): point each at where the file
   actually lives now (legacy area under benchmarking/param-lift-20260922/legacy_lift/ or git history
   at a named commit), or restore the original path text marked as historical. Do not invent files.
   Leave campaign/eligibility-census.py's stale import as is but add a one-line comment that it is
   archived and ran against the pre-generic package.

Validation: tests/pytest (full, serial), ruff on changed Python, debug `meson test --suite unit` in
both native-arms settings (-j 1 compile, --num-processes 1). VERDICT at end.
