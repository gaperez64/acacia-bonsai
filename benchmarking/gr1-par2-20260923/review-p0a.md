# Review brief — P0a (dependency repair + explicit tool configuration)

You are a reviewer. Do not edit tracked files; write your report to build_scratch/review-p0a/REPORT.md.
Scope: `git diff` of benchmarking/param-lift-20260922/{generalize_gr1.py,param-lift-campaign.py,
test_generalize_gr1.py} plus new tool_config.py, test_tool_config.py, and
benchmarking/gr1-par2-20260923/{p0-dependency-manifest.json,p0-replay/README.md}. Brief that produced
it: benchmarking/gr1-par2-20260923/brief-p0a.md; plan: benchmarking/gr1-par2-20260923/plan.md §3.
Do NOT compile anything (build-L0 is frozen read-only). You may run the Python tests
(.venv/bin/python -m pytest ...) and ruff.
Check: (1) with nothing configured, behaviour is byte-for-byte the old defaults (historical commands
still work); (2) flag > env > default precedence, errors are clear, probe runs no solver work;
(3) no remaining host-specific hardcodes on the runtime path; (4) the determinism-test change
("exclude timing-bearing evidence") does not weaken what the test was checking; (5) the replay
commands really select the 22 REAL + 4 UNREAL decisive M6 rows and the controls, at 120 s, cgroup
wrapped per invocation (whole process tree), writing to new files only; (6) manifest accuracy
(spot-check two hashes with sha256sum); (7) anything in the diff unrelated to the brief.
End REPORT.md with `## VERDICT` : ACCEPT / ACCEPT-WITH-NITS / REJECT and a numbered list of findings
with severity.
