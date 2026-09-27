# Brief TC — tlsf-tools native-api cleanup (owner feedback on PR #39)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api (85e3a31, open as PR #39 against main
0e22383). NEVER stage or commit by any path. TIMED BENCHMARKS are running on this machine: one core,
one job for every build (meson compile -j 1, cargo -j 1), bounded test runs, never /tmp;
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Produce separate patch files (A, B, C) under
build-cleanup/ applying in order, and leave the tree in the final state.
Owner decisions:
A. **No ABI compatibility layer.** tlsf-tools is in A/B testing; breaking API/ABI conventions is fine.
   Remove the old-ABI compatibility machinery added in step 1: test/old_abi*, test/old_abi/, duplicated
   V1/V2 option structs and compatibility entry points kept only for old clients; extend or replace
   the public structs directly; remove include/tlsf/native.h if it only exists as a versioning umbrella
   (keep a single version constant only if something actually checks it). Update all callers, tests and
   the CLIs. Keep the real behaviour (limits, cancellation, transactional results, no printing).
B. **No ad-hoc src/native/ directory.** Move its sources into the existing flat src/ layout with names
   consistent with neighbouring files (e.g. src/gr1_reduction.cc, src/gr1_check.c, src/gr1_lift.cc,
   src/gr1_service.c or fold into gr1_oxidd.c if that is the natural home; source.c/source_internal.h
   into the pipeline/source files they belong with). Do not invent a new directory structure; a
   structural reorganisation is out of scope. Update meson.build, includes and tests.
C. **Bump OxiDD.** external/oxidd is pinned at 9158645 (v0.11.2) with our build-time patch
   patches/oxidd-local-store-generation.patch. Upstream main (e.g. be2f69b; fetch is not available to
   you — the submodule already has origin/main and tags fetched locally) contains 9fd1ed0 "Fix stale
   `current_store` address in `LocalStoreStateGuard::drop`" (not in v0.12.0). Move the submodule
   checkout to upstream origin/main HEAD (record the SHA), adapt to any API changes, and run our
   same-thread regressions (test/oxidd_manager_lifetime.c manager and checker modes, the Rust test if
   it still applies) WITHOUT our patch. If they pass reliably (e.g. 10 repeated runs), delete our patch,
   its build-time application/verification and build-info record, and state that upstream 9fd1ed0
   fixes it; if they fail, rebase our patch onto upstream main keeping only what upstream lacks, and
   rewrite patches/oxidd-local-store-generation.md to describe exactly that difference. Report which.
Validation after each patch: full meson test serially (bounded), clang-format, the native lift/
reduction differential tests, ASan/UBSan on the native API tests. Also list what an Acacia-side caller
must change (includes, struct/function names) so I can update Acacia's native arms. VERDICT at end.
