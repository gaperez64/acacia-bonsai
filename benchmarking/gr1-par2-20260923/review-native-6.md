# Review brief — native step 6 (Python lifting route off the shipped path; --help fix)

Reviewer; do not edit tracked files; write your report to build_scratch/review-native-6/REPORT.md.
TIMED MEASUREMENTS ARE RUNNING on this machine: do NOT compile anything and do NOT run any solver,
campaign or meson test. Static review plus fast pure-Python tests only (tests/pytest, ruff),
serially, with `python3 -s` / PYTHONNOUSERSITE=1.

Scope: the uncommitted diff in this worktree EXCEPT benchmarking/gr1-par2-20260923/brief-tt-cleanup.md
and subprojects/tlsf-tools (not part of this step). Brief: the step-6 section of
benchmarking/gr1-par2-20260923/native-design.md and decisions.md. Owner rule: no Python anywhere on
the solver path; the Python lifting route survives only as a research/test oracle.

Check hardest:
1. Nothing shipped can still reach the Python route: Docker image (Dockerfile + .dockerignore),
   meson install rules, scripts/acacia-bonsai.sh, README usage. Follow every entrypoint, not just
   grep for the old path.
2. config/docker-default.list is a committed copy of the docker_default group. CLAUDE.md says the
   group is the source of truth and nothing may hardcode preset names. Is the test that checks the
   copy against the registry run in CI (which job, which suite) so drift fails before merge? Would
   generating the list at image-build time (Python allowed in the build stage, not at run time) be
   strictly better? Recommend one; do not implement.
3. src/arg_parser.hh --help/-h: exits 0 with usage, no side effects, and does not change parsing of
   any other option (including options whose values begin with '-h'). tests/check-help.py covers it
   and is registered in tests/meson.build for both native-arm settings.
4. Tests moved to the oracle: were any assertions weakened or deleted rather than moved? Compare the
   old and new versions of each changed test in tests/pytest; list every removed assert.
5. The oracle directory's imports resolve without scripts/ on sys.path; no test silently skips
   because an import path broke (count skips before/after).
6. Anything in the diff unrelated to step 6, and any change to committed evidence (TSV/JSON results).

End the report with a line `VERDICT: ACCEPT`, `VERDICT: ACCEPT WITH FIXES` (list them) or
`VERDICT: REJECT`.
