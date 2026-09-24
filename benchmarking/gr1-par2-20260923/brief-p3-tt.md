# Brief P3-tt — tlsf-tools: independent checker cache cap + P1.4 test nits

Workspace /home/gperez/GIT-repos/tlsf-tools, branch gr1-par2-checker at dbe8c2b (I commit). Read the
plan (/home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/plan.md) §6.1-6.4 and
§9.1; include/tlsf/oxidd_common.h (OxiddSolveOptions / OxiddRun / resource-policy conventions);
docs on tlsfsolve's node/cache options; build-p1-4-review/REPORT.md findings 1-2 (test nits).
1. tlsfcertcheck currently sets apply-cache capacity equal to node capacity (setup_bdds). Add a
   --cache-cap N option (and, if tlsfsolve has a ratio/cap convention, mirror its name/semantics
   exactly, e.g. a ratio option) independent of --node-cap, reusing the existing option parsing /
   OxiddSolveOptions conventions. Default MUST reproduce the current behaviour exactly (cache =
   node cap) so L0/P14 remain the baseline; stdout byte-identical by default. Validate inputs
   (0, > node cap, overflow) with clear errors. Report the effective node/cache capacities in
   --stats.
2. Do NOT add retry-with-doubled-capacity logic here.
3. Fix the two P1.4 review nits: add a tracked regression test for the bounded selected-output
   cache (forces eviction; statuses equal the oracle), and make the seeded-gate low-level test
   use a non-constant boundary.
4. Tests for (1): default equals old; smaller cache still gives identical statuses on all
   certificate fixtures; invalid values rejected.
Build one job only under build-p3/ (meson setup -Dbuildtype=release -Doxidd=enabled
-Dresearch_tools=true; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig), full suite `meson test
--num-processes 1`, clang-format, scratch never in /tmp. Leave uncommitted. Finish with VERDICT.
