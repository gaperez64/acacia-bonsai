# Brief — move Acacia's native arms to the cleaned-up tlsf-tools API and to yyjson

Workspace /home/gperez/GIT-repos/acacia-bonsai, branch sprint/gr1-par2-20260923. NEVER stage or
commit. A TIMED BENCHMARK is running from another worktree: one job for every build (meson compile
-j 1, cargo -j 1), serial tests (--num-processes 1), never /tmp, no network,
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Do not touch build_perarm_m1/ or build_perarm_m2/ (frozen
evidence) or any other write-protected directory. Use fresh build directories under build_scratch/.

State prepared for you:
- subprojects/tlsf-tools is checked out at bf3e7e7 (tlsf-tools PR #39 head; the pointer change is
  unstaged and part of this work).
- Its external/oxidd is clean at be2f69b, with crates fetched.
- Its subprojects/ holds the yyjson 0.12.0 wrap with the source downloaded.

Build the patched OxiDD archive first: `scripts/build_oxidd.sh` inside subprojects/tlsf-tools. It
applies patches/oxidd-gc-thread-retirement.patch, which is identical to OxiDD PR #49.

Changes since 85e3a31, which the current Acacia code targets:
- No ABI layer: `tlsf/native.h` is gone; the `V2` option types are gone; the `abi_version` and
  `struct_size` fields are gone; entry points are renamed.
- Flat src/: `src/native/*` became `src/gr1_check.c`, `gr1_reduction.cc`, `gr1_lift.cc`,
  `gr1_service.c` and `pipeline_source.c`.
- OxiDD at be2f69b plus the PR #49 patch.
- yyjson replaces Boost.JSON and the hand-written JSON code.

The caller migration list is in /home/gperez/GIT-repos/tlsf-tools/build-cleanup/README.md and
F-REPORT.md.

1. Update the native arms and every other tlsf-tools caller to the new API: src/native_gr1_arm.hh,
   src/native_param_lift_arm.hh, src/native_proof_binding.hh, and any other file (grep includes of
   `tlsf/` and the old names). Behaviour must not change: the same verdicts, diagnostics and exit
   codes, and the same proof binding (source, game, certificate and policy hashes compared with
   the snapshot).
2. Replace Boost.JSON in Acacia with yyjson, as a reader, plus the debug-only evidence-rewriting
   test hooks in native_param_lift_arm.hh.
   - Owner decision: decoded-JSON semantics, the same as the tlsf-tools checker.
   - Reject duplicate keys (after decoding) in every document the arms read.
   - Keep the field and SHA-256 checks length-aware.
   - Free every `yyjson_doc` on all paths.
   - Take yyjson through tlsf-tools' propagated dependency or `dependency('yyjson')`, never a
     second copy. Acacia must not need its own wrap.
   - Afterwards, `git grep -n boost -- src tests meson.build` must show nothing new outside the
     posets subproject.
3. The family-hardcoding guard, tests/pytest/test_acacia_lift_generic_guard.py: its NATIVE_ROUTE
   globs `subprojects/tlsf-tools/src/native/*.c`, which no longer exists. Point it at the new
   flat files, and assert that each listed path exists, so that a future move fails the test
   instead of silently shrinking the scan.
4. Fix any other references to old tlsf-tools paths or names in tests, scripts and current docs.
   Leave dated sprint records alone.

Validation:
- Debug builds with `-Dacacia_native_arms=true` and `=false`: `meson test --suite unit` for each.
- The native CLI tests: check-native-gr1-cli.py, check-native-param-lift-cli.py,
  check-native-gr1-same-thread.py, check-portfolio-deadline.py and check-native-release-no-hooks.py,
  plus the pytest suite (python3 -s).
- A release-profile configure (no full build needed unless cheap), checking that native arms still
  configure with LTO.
- Smoke: the native-arms debug binary on tlsf-corpus/arbiter_with_buffer_pb_8_pe_.tlsf with
  `--arms real:param-lift:oxidd` and `--arms real:gr1:oxidd` (expect REALIZABLE), and a
  non-parametric one (expect UNKNOWN for param-lift), each inside
  `systemd-run --user --scope -p MemoryMax=3G`.
VERDICT at end, with the list of files changed.
