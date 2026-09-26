# Brief — bump Acacia to tlsf-tools main after the source reorganisation (tlsf-tools #41, a294419)

Workspace /home/gperez/GIT-repos/acacia-bonsai, branch sprint/gr1-par2-20260923. NEVER stage or
commit. TIMED LEGS ARE RUNNING from another worktree: one job for every build (meson -j 1, cargo
-j 1), serial tests, never /tmp, no network, PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Do not touch
the frozen directories build_perarm_m1/, build_perarm_m2/ and build_final_a3c38777/, or any other
write-protected directory. Use fresh build directories under build_scratch/bump41/.

State prepared for you:
- subprojects/tlsf-tools is checked out at a294419 (tlsf-tools main with #41 merged); the pointer
  change is unstaged. Its external/oxidd is unchanged at be2f69b with the PR #49 patch applied,
  and its archive is already built.
- The yyjson source is in subprojects/tlsf-tools/subprojects/packagecache.

tlsf-tools #41 moved the sources into public include/tlsf/, src/lib/, src/tools/<tool>/ and
test/{unit,api,cli,oracle,fixtures}/. `tlsf_dep` now exposes public headers only. The OxiDD archive
is folded into libtlsf under native_gr1. `cli_parse` became `spec_parse`. The certificate
checker's CLI moved out of gr1_check.c; its engine is declared in the private
gr1_check_internal.h. `include/tlsf/oxidd_common.h` was dropped. A static check found every
header and identifier Acacia uses still public.

1. **The family-hardcoding guard** (tests/pytest/test_acacia_lift_generic_guard.py). NATIVE_ROUTE
   names the flat files and asserts that each exists, so it now fails.
   - Point it at the new locations: src/lib/gr1_check.c, gr1_reduction.cc, gr1_lift.cc,
     gr1_service.c, pipeline_source.c, gr1_oxidd.c; src/tools/tlsfsolve/ and
     src/tools/tlsfcertcheck/ main sources.
   - Add the new private gr1_check_internal.h and any new file that now holds verdict-path code
     that the moves split out (check the #41 diff for gr1/oxidd/lift code that changed file).
   - Replace the dropped include/tlsf/oxidd_common.h with wherever its content now lives.
   - Nothing on the verdict path may leave the scan, and every listed path must exist.
   - The MONITOR path (scripts/gr1_monitor_game.py) is unchanged; confirm it.
2. **The wrap redirect.** Add `/subprojects/yyjson.wrap` to .gitignore. Meson regenerates this
   redirect when it resolves the nested wrap.
3. **Any other Acacia reference** to moved tlsf-tools paths, in tests, scripts, CI or current docs.
   Leave dated sprint records alone.

Validation:
- Debug builds with `-Dacacia_native_arms=true` and `=false`: `meson test --suite unit` for each.
- The native CLI tests: check-native-gr1-cli.py, check-native-param-lift-cli.py,
  check-native-gr1-same-thread.py, check-portfolio-deadline.py and check-native-release-no-hooks.py.
- Full pytest (python3 -s).
- Configure and build a release native binary (LTO, -j 1).
- Smoke in 3G scopes: `--arms real:gr1:oxidd` and `--arms real:param-lift:oxidd` on
  tlsf-corpus/arbiter_with_buffer_pb_8_pe_.tlsf (expect REALIZABLE), and the default arms on
  lilydemo01.tlsf (expect UNREALIZABLE).
- #41 changed expansion to reject empty or out-of-range bus bounds, which could change behaviour on
  real inputs. Run the release native binary's reduction-only path, or `tlsf2ltl` from the new
  subproject build, over all 1,524 tlsf-corpus inputs. Serially, in a 3G scope, 5 s per input.
  Compare with the previous tlsf-tools a453adb, whose tlsf2ltl you can build from
  /home/gperez/GIT-repos/tlsf-tools at that commit in a build_scratch worktree. List every input
  whose expansion now errors or whose LTL output differs.
VERDICT at end, with the files changed.
