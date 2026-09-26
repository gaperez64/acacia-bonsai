# Brief N2 — native GR(1) reduction in C++ (native-design.md step 2)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api at 634ea99 (step 1 committed). NEVER
stage or commit by any path. One job; build dirs build-native2*/; never /tmp;
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Spec: /home/gperez/GIT-repos/acacia-bonsai/benchmarking/
gr1-par2-20260923/native-design.md (step 2 row and the C ABI section), decisions.md there (no Python
on the solver path; no hardcoding; renaming invariance), and the step-1 API already in the tree
(pipeline.h byte loading/overrides, provenance, gr1_oxidd.h versioned solve options, gr1_check.h).
Port scripts/gr1_monitor_game.py to C++ inside the native_gr1 library (e.g. src/native/
gr1_reduction.cc, using Spot's C++ API that the build already links — one Spot/BuDDy copy), exposed
through the existing public API style (extend, do not duplicate: a reduction entry point that takes
the already-loaded pipeline/source snapshot, semantics exact|strict, deadline/cancellation and
memory caps, and returns an in-memory game Aig + game metadata + provenance mapping + canonical-name
sidecar exactly as the Python produces them). Preserve: per-conjunct deterministic Büchi monitor
reduction (DBA check, deterministic fallback behaviour), exact vs strict semantics rules, Mealy/
Moore/target handling and rejections, unsupported-fragment declines, frontend provenance consumption
and fail-closed ambiguity, the disjoint namespace + canonical role/declaration-order names from
8b158d7. The Python script stays as a reference oracle (test only).
Acceptance (design table): differential tests vs gr1_monitor_game.py — exact/strict game AIG
semantics (compare games by AIG semantics or, where the construction is deterministic, bytes),
metadata, provenance, canonical names — on (a) all existing GR(1) fixtures/tests, (b) ≥100 SYNTCOMP26
TLSF inputs from /home/gperez/GIT-repos/acacia-bonsai/tlsf-corpus/ covering reducible and
unsupported ones, parametric and not, and (c) generated small specs incl. alpha-renamed and
adversarial names (controllable_*, uncontrollable_*, latch-like, @, primes); independent Spot
language-equivalence checks on small cases; non-Mealy, unsupported MP, safety/release edge cases; a
different rejection ORDER is acceptable, a different game language or ownership is not. Timing: report
reduction time C++ vs Python on the corpus sample (expect a large speedup; not a gate). Full meson test
serially; clang-format; ASan/UBSan on the new tests. Patch + notes in build-native2/. VERDICT at end.
