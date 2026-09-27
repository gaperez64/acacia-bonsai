# Brief N1 — tlsf-tools native C boundary (native-design.md step 1)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api (created for you from generic-provenance
8b158d7). NEVER stage or commit by any path. One job; build dirs build-native1*/; never /tmp.
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Spec: /home/gperez/GIT-repos/acacia-bonsai/benchmarking/
gr1-par2-20260923/native-design.md (read all of it; implement its "tlsf-tools C ABI" section for
step 1 and the "Build and rollout" step 1 row) and the owner rules in decisions.md there (no Python on
the solver path, no hardcoding, renaming invariance).
Deliver step 1 only: include/tlsf/native.h (versioned C ABI exactly as designed: opaque handles,
ownership, error model, deadlines/cancellation, memory caps), src/native/source.c (immutable source
snapshot + sha256, parse/expand with overrides, provenance), src/native/gr1_service.c (library-ised
GR(1) solve from the existing tlsfsolve core, certificate/policy export to memory objects),
src/native/gr1_check.c (library-ised tlsfcertcheck methods, independent of the solver algorithm);
refactor tlsf2tlsf, tlsfsolve and tlsfcertcheck CLIs to call these shared functions (no behaviour
change); the meson `native_gr1` feature building one static library as designed. The GR(1)
REDUCTION stays in Python for this step (step 2 ports it); the API should accept an already-built
game for solve/check in step 1.
Acceptance (as the design's table says): differential comparison of parse/override/provenance bytes,
solve side, AAG/JSON exports and checker method results between old CLIs (build from 8b158d7 into
build-native1-ref/) and the refactored ones AND direct C-API calls (a small C test harness and a C++
one, both linked against the static library), on positive, negative, malformed and OOM/allocation-
failure cases; full meson test serially; clang-format; ASan/UBSan build of the API tests if feasible
with one job. Write patches + notes in build-native1/ and a VERDICT at the end.

## Correction (owner): reuse the existing public API
tlsf-tools ALREADY has a public API that Acacia links and uses: `tlsf_pipeline_load` (pipeline.h:
parse/expand), `tlsf_ast_from_file/_string(_ex)` (ast_api.h), `tlsf::decompose` (decompose.hpp, used
by acacia-bonsai's src/tlsf_frontend.cc for -T), and `solve_gr1_oxidd(_ex)_with_certificate`
(gr1_oxidd.h: GR(1) solve on an Aig with certificate). Do NOT create a parallel parse/info/solve
layer. Extend these existing headers and functions; add new public entry points ONLY for what is
genuinely missing as a library: (a) parameter overrides + the frontend provenance output (today in
tlsf2tlsf's main), (b) the independent certificate/policy checker (today all in
main_tlsfcertcheck.c) as library functions, (c) deadline/cancellation and memory-cap parameters for
the existing GR(1) solve entry points, (d) whatever versioning/error-model glue the design needs,
kept minimal. If a small umbrella header is still useful (e.g. to version the ABI), it must only
include/declare the existing API plus these additions, not wrap parse/solve again. Keep the CLIs as
thin callers of the same library functions. A previous attempt was stopped mid-way and left partial
edits (include/tlsf/native.h, src/native/, test/native_api.c, main_tlsfcertcheck_legacy.c, changes to
gr1_oxidd.*, meson): keep what fits this correction, remove what duplicates existing API.
