# Brief P1.4 — fix the many-mode checker regressions (tlsf-tools)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch gr1-par2-checker at 0435385 (I commit). Read the
plan §4 (esp. 4.2 step 5: "release mode-local roots before the next mode unless a bounded measured
cache retains them") and the attribution in
/home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/decisions.md (2026-09-24 entry)
and p1-attribution/l0-vs-p1-20260924.tsv. Regressions vs L0 (old checker =
/home/gperez/GIT-repos/acacia-bonsai/subprojects/tlsf-tools/build-L0/tlsfcertcheck, read-only):
round_robin_arbiter_unreal2 n=7 (+11%, environment cert), amba_decomposed_lock n=15 (+14%),
prioritized_arbiter n=12 (+70%). Frozen artifacts: find the triples for these targets under
/home/gperez/GIT-repos/acacia-bonsai/_bm-logs.gr1-par2-p0-replay-L0/ (read-only); argv is in
p1-attribution/README.md.
1. Diagnose with --stats (and perf if available; one run at a time, no parallel runs, use
   `timeout`): where does the extra time go — per-mode recompilation of counter-independent cones,
   substitution construction, cache misses, GC? Write findings to build-p1-4/DIAGNOSIS.md.
2. If it is counter-independent cone recompilation: compile the counter-INDEPENDENT part of the
   selected policy cones once (policy inputs that are not counter bits), retain those roots across
   modes (bounded, released after the last mode), and specialise only the counter-dependent part per
   mode. Must keep all-zero and each one-hot mode as separate proof obligations. Otherwise fix what
   the diagnosis shows, minimally.
3. Tests: status equality with the hidden oracles on all certificate fixtures + mutations (same as
   P1.2/P1.3 tests); a diagnostic count showing cross-mode reuse.
4. Measure: on the three regressed targets plus cancel n=9, lbu2 n=7, inpchange n=6 (as controls of
   the wins), run old L0 vs your new build, 3 alternating runs each, serial, report medians in
   DIAGNOSIS.md. These are informal (no cgroup available to you); I'll re-measure formally.
Build one job only in a new dir build-p1-4/ configured like build-P1-0435385 (`meson setup
-Dbuildtype=release -Doxidd=enabled -Dresearch_tools=true`, PKG_CONFIG_PATH=/usr/local/lib/pkgconfig);
scratch under build-p1-4*/ never /tmp; full suite `meson test --num-processes 1` green;
clang-format. Leave uncommitted. Finish with VERDICT.
