# Brief P2d — make projection/relabel cheaper inside BuDDy

Read plan.md §2 (6, 11), §5.1 last paragraph (permutation replace is NOT general functional
substitution), §5A S4; decisions.md (2026-09-24 "P2b end to end" and "P2c" entries); the P2c
e2e diagnostics benchmarking/gr1-par2-20260923/s0/raw-P2c-e2e/*.json (bdd_projection_relabel is
~60% on load_balancer n=8/9 and amba_decomposed_lock n=15). Code: generalize_gr1.py projection,
relabel, cube, substitute_variables, the native adapter. NEVER stage or commit by any path.
One job; scratch build_scratch/p2d/.
1. Instrument (behind --diagnostics only) bdd_projection_relabel's sub-steps: existential
   quantification, relabel/rename, cube construction, support, and count node-table growth / GC
   events around them. Run on the frozen target inputs load_balancer n=8 and amba n=15 in
   reproducer mode (tlsf-tools build /home/gperez/GIT-repos/tlsf-tools/build-P5-9212e2b, adapter
   built via native/build.py into build_scratch/p2d/adapter) — single runs, no systemd needed for
   this diagnostic, one at a time, `timeout 200`. Write findings to build_scratch/p2d/PROFILE.md.
2. Based on the profile, implement the cheapest exact fixes, e.g.: where a relabel is a pure
   variable permutation (injective var→var map with disjoint/verified targets), use BuDDy's pair
   replace (bdd_replace via the adapter, same libbddx, scoped error hook) instead of veccompose;
   fuse exist+relabel or and-exist (bdd_appex/bdd_relprod) where a conjunction is immediately
   quantified; avoid rebuilding identical cubes; batch multiple roots through one pair. Any
   permutation fast path must check permutation-ness at runtime and fall back to veccompose
   otherwise. Also consider BuDDy node-table/cache sizing (bdd_setcacheratio / initial nodes /
   bdd_setmaxincrease) ONLY if the profile shows GC/resize dominating — keep it configurable,
   default unchanged unless measured.
3. Tests: replace-vs-veccompose equivalence on random permutations incl. swaps and cycles;
   non-permutation maps take the veccompose path; appex equivalence to exist(and); certificates/
   policies BDD-equivalent to HEAD on the generalizer suite instances.
4. Report before/after sub-step timings for load_balancer n=8 and amba n=15 (informal; I measure
   formally). MACHINE MAY BE RUNNING TIMED MEASUREMENTS: before compiling or running test suites or
   the profiling runs, wait until build_scratch/seq6/progress.txt is absent or has a line starting
   "DONE" (poll every 60 s). Full suites + ruff. VERDICT at end.
