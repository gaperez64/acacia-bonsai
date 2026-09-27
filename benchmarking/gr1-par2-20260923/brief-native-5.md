# Brief N5 — Acacia param-lift arm (native-design.md step 5)

Workspace /home/gperez/GIT-repos/acacia-bonsai (branch sprint/gr1-par2-20260923; I commit; NEVER stage
or commit by any path). tlsf-tools submodule is at 2be5915 (native lifting: include/tlsf/gr1_lift.h,
tlsf_gr1_lift_v1). TIMED BENCHMARKS are running from another worktree: one core, one job for every
compile (meson compile -j 1), bounded test runs, never /tmp; use a debug or debugoptimized build dir
build_native5/ (no LTO release builds). Spec: benchmarking/gr1-par2-20260923/native-design.md (arms
section, step 5 row), decisions.md (owner rules), and the step-3 gr1 arm implementation
(src/native_gr1_arm.hh and its dispatch) which this must mirror.
Implement `real:param-lift:oxidd`: REALIZABLE only; calls tlsf_gr1_lift_v1 in-process in the forked
child with the -T source snapshot, the outer monotonic deadline and memory caps; only an
independently VERIFIED result maps to EXIT_CODE_REAL; everything else (including the documented
no-PARAMETERS decline) is EXIT_CODE_UNKNOWN with a structured diagnostic; NO fallback to direct gr1 or
a legacy arm inside this arm; reject `unreal:param-lift:*` with a clear error; help text; parser and
portfolio_arm kind as designed; built only with acacia_native_arms. Tests: standalone
`--arms real:param-lift:oxidd` on (a) a generated parametric family that lifts (reuse tlsf-tools'
generator/fixtures), (b) a non-parametric input -> UNKNOWN fast, (c) a parametric input that does
not lift -> UNKNOWN; a mixed list with a legacy arm; renaming/obfuscation invariance on 3 twins
(benchmarking/gr1-par2-20260923/obfuscate-tlsf.py); adversarial prefixed names and enum buses;
deadline kill and reap of a lifting child that is inside a long Spot call; the unit suites with
the option on and off; config validation. VERDICT at end.
