# Review brief — P2a (native simultaneous composition + compiled-AIG contexts)

Reviewer; do not edit tracked files; report to build_scratch/review-p2a/REPORT.md.
TIMED MEASUREMENTS ARE RUNNING on this machine: do NOT compile anything, do NOT run
test_generalize_gr1.py or any solver/campaign. Static review plus fast tests only (test_request.py,
test_tool_config.py, test_s0_diagnostics.py, ruff), serially. Heavy validation happens later.
Scope: uncommitted diff (generalize_gr1.py, test_generalize_gr1.py, buddy_veccompose.py, native/,
p2a-feasibility.md, p2a-diagnostics.md). Brief benchmarking/gr1-par2-20260923/brief-p2a.md; plan
§2 (6), §5.1, §5.2, §6.1. Patch notes in build_scratch/p2a/step*-note.md.
Check hardest:
1. Native adapter soundness: it must use the SAME libbddx the Python `buddy` module has mapped
   (no second copy/global table); bddPair lifetime (freed, no leak per call, no use after free);
   refcounts of returned proxies (bdd_addref/delref balance); behaviour under GC and reordering
   (are node ids read from proxies while a GC can run?); every failure raises, never returns a
   wrong BDD; thread safety is not required but global BuDDy state (bdd_error handler, pair
   registry) must be restored.
2. How/where the adapter .so is BUILT and LOADED at runtime: is it compiled on the fly into
   build_scratch/p2a? That is not acceptable for a production/timed route (charged time, hidden
   compiler dependency, unreproducible). Say exactly what happens on a clean checkout, what the
   fallback is if it is missing, and whether a timed invocation would pay a compile. Propose the
   minimal reproducible arrangement (e.g. an explicit build step/Meson target or a
   tool_config-configured prebuilt path + probe) — do not implement.
3. Simultaneous semantics: x:=y,y:=x; replacements that mention substituted vars; variables outside
   support — verify from code and tests.
4. Compiled-AIG context and caches: keys include everything the plan lists (seed certificate
   identity, predicate, role/anchor/goal relation, arity, normalization mapping, manager lifetime);
   no raw node id reused across instance/manager; caches hold owning references (a collected node id
   can't be reused while a key refers to it); explicit release at the end of the window; bounded.
5. Generalizer test count went 11 → 14: what was added; were any existing assertions weakened?
6. Anything in the diff unrelated to the brief; any change to m4-*.tsv or other evidence.
End with `## VERDICT` ACCEPT / ACCEPT-WITH-NITS / REJECT and numbered findings with severity.
