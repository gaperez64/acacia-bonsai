# Brief P2a-fix — address the P2a review (build_scratch/review-p2a/REPORT.md)

Uncommitted P2a work is in the tree; fix all four findings exactly as the reviewer describes.
1. Adapter build/load: add an explicit build step (benchmarking/param-lift-20260922/native/build.py
   or .sh, serial, one compiler invocation) that writes the .so plus a sidecar JSON binding it to the
   inputs (source sha256, compiler and version, flags, libbddx resolved path + sha256, bdd.h sha256,
   binding interpreter). Add a ToolConfiguration field --buddy-adapter PATH / ACACIA_BUDDY_ADAPTER.
   The runtime loader only loads and validates (mapped-object identity, symbol address, sidecar
   match); it never compiles. If no adapter is configured, the generalizer explicitly selects the
   retained two-pass route BEFORE constructing any adapter, and records the chosen compose route in
   diagnostics and in the evidence JSON. --probe validates the adapter (presence, sidecar match,
   dependency resolution, mapped-object identity, symbol address) when one is configured. Update
   test_tool_config for the new field. Default output location of the build step:
   build_scratch/buddy-adapter/ (gitignored); document the command in native/README.md.
2. BuDDy errors: install a scoped bdd_error_hook in the adapter that records any error, restore
   the exact prior handler on every exit path (RAII), keep pair cleanup under the same scope,
   distinguish an error from a legitimate bddfalse result, bounds-check variables against
   bdd_varnum in Python and native code; add a SUBPROCESS regression that reaches a real BuDDy
   error path (e.g. out-of-range variable) and asserts a Python exception, no abort, prior handler
   restored.
3. Put deterministic count/byte bounds on template_cache and _subset_metadata (LRU or streaming
   window), holding owning references only for live entries; test eviction keeps results
   correct and releases references.
4. Replace/extend the cache-locality test so it goes through GeneralizationAttemptContext.
   template_cache / predicate_cache_key and would fail if a key omitted source or manager identity.
MACHINE IS RUNNING TIMED MEASUREMENTS: before running test_generalize_gr1.py or any heavy work, wait
until build_scratch/seq1/progress.txt contains the line prefix "E2E DONE" (poll every 60 s with
sleep). Fast tests and the one small adapter compile are fine meanwhile. Then run the full
test_generalize_gr1.py, test_request.py, test_tool_config.py, test_s0_diagnostics.py, ruff,
serially. Refresh build_scratch/p2a step patches only if easy; otherwise say so. Finish with VERDICT.
