# Brief — fixes from the A–D review (tlsf-tools build-cleanup/REVIEW.md, ACCEPT WITH FIXES)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api, uncommitted A–D state. NEVER stage or
commit by any path. TIMED BENCHMARKS are running: one job for every build, bounded serial tests, never
/tmp, PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. No network.

Fix exactly the three review findings:
1. Make the CI clang-format command cover every project-owned C/C++ file, including `examples/`
   (enumerate the tracked files, e.g. with `git ls-files` filtered by extension, excluding
   external/ and build directories), and correct the "all C/C++" claim in build-cleanup/README.md.
   Run the exact new CI command locally until it is clean.
2. Rename `patches/oxidd-local-store-generation.{patch,md}` to a name that describes GC-thread
   retirement (e.g. `oxidd-gc-thread-retirement`). Update every reference: scripts/build_oxidd.sh,
   the CI consumer jobs and cache key, meson.build (patch record or info path), README/docs, and
   build-cleanup/README.md. Change the build-info patch record string to match. Rebuild the patched
   OxiDD archive with `scripts/build_oxidd.sh` (offline, locked, -j 1), then run `--verify`.
3. Restore the public API contract comments on the unsuffixed `Gr1CertificateOptions` fields in
   include/tlsf/gr1_oxidd.h: the policy AAG's inputs and outputs, and why strict reductions must not
   export an environment certificate. Take the removed text from HEAD and adapt it to the current
   field names.

Validation: the full serial meson test in the oxidd+native_gr1 build dir, the exact CI clang-format
command, YAML parsing. Update the D patch file (or add E-review-fixes.patch) and build-cleanup/README.md.
VERDICT at end.
