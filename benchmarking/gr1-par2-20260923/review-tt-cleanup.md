# Review brief — tlsf-tools native-api cleanup A–D (before commit to PR #39)

Reviewer; do not edit tracked files; write the report to
/home/gperez/GIT-repos/tlsf-tools/build-cleanup/REVIEW.md. Workspace /home/gperez/GIT-repos/tlsf-tools,
branch native-api at 85e3a31 plus the uncommitted A–D state (patches and README in build-cleanup/).
Briefs: benchmarking/gr1-par2-20260923/brief-tt-cleanup.md and brief-tt-ci.md in the Acacia repo.
TIMED BENCHMARKS are running: one job for every build, bounded serial tests, never /tmp,
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Prefer static review; existing build logs in
build-cleanup/ are evidence you can read.

Check hardest:
1. A (no ABI layer): no behaviour lost in the public entry points (limits, deadlines, cancellation,
   transactional results, no printing to stdout/stderr from the library). Every removed test
   covered only the ABI shims; list any removed assertion that tested real behaviour and was not
   carried over.
2. B (flat src/): pure moves plus path fixes? Diff each moved file against its original, and
   report anything other than include-path or build changes.
3. C (OxiDD be2f69b): the remaining patch contains only what upstream lacks: GC-thread retirement
   (lost Quit wakeup, joining on the last drop, atomic reference count) plus the reproducer test.
   Check whether any leftover generation/local-store logic duplicates or conflicts with upstream
   9fd1ed0. Rewrite of patches/oxidd-local-store-generation.md: is it accurate, and does the file
   name still fit its content? Is the patch small and self-contained enough to be submitted
   upstream as a PR? Say what an upstream PR would need beyond it (tests, docs, changelog).
4. D (CI): Meson >= 1.7 pinned in every job; OxiDD artifact upload and cache key complete (no
   restore of an unpatched build for a patched one); consumer jobs that "apply the source patch" —
   is that correct, or should they verify against the uploaded patch record instead? clang-format
   now covers all C/C++ files: formatting-only hunks must not change code; action bumps.
5. Anything else in A–D not asked for by the briefs.

End with `VERDICT: ACCEPT`, `VERDICT: ACCEPT WITH FIXES` (list them) or `VERDICT: REJECT`.
