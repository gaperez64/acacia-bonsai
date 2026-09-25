# Brief TD — tlsf-tools PR #39 CI repair (patch D, on top of cleanup A–C)

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api, with cleanup patches A–C already
applied in the working tree (see build-cleanup/ and brief-tt-cleanup.md). NEVER stage or commit by any
path. TIMED BENCHMARKS are running on this machine: one job for every build (meson compile -j 1,
cargo -j 1), bounded serial test runs, never /tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. No
network. Write the result as build-cleanup/D-ci.patch and leave the tree in the final state.

CI run 36118725259 on 85e3a31 failed in every job except `oxidd`. Three root causes:

1. `meson.build` (vcs_tag for version.h): `ERROR: vcs_tag got unknown keyword arguments "install",
   "install_dir"`. Those kwargs need Meson >= 1.7; the Ubuntu 24.04 runners install apt meson 1.3.x
   (build-test, build-clang, coverage, simd-matrix, perf-regression). Fix: declare
   `meson_version: '>= 1.7.0'` in project() and install Meson from pip in every job that runs meson
   (pin one version, e.g. `pip install 'meson==1.12.1'`, the same way the format job pins
   clang-format), dropping `meson` from the apt lists so the pip one is not shadowed. Only if version.h
   does not actually need to be installed (check what includes it among installed headers), prefer
   dropping the install kwargs instead and say which you chose and why.
2. `build-oxidd`, `build-native-gr1`: `OxiDD artifacts are missing` because meson now also requires
   `external/oxidd/build/oxidd-patch-info.txt`, but the `oxidd` job uploads only
   `target/release/liboxidd_ffi_c.a` and `build/include`. After patch C decide from the final state:
   upload every file that meson's readiness check requires, and make the `oxidd` job's cache key
   include everything that determines the artifact (submodule SHA, rust version, the hash of
   scripts/build_oxidd.sh and of patches/ if a patch is still applied) so a cache built without the
   patch can never be restored for a patched build. The restore-keys fallback must not reintroduce
   that (drop it or make it equally specific).
3. `format`: clang-format 22.1.8 violations at src/main_tlsfsolve.c:334,342 and
   include/tlsf/gr1_lift.h:58-59; also the format step globs `src/native/*.c`, which patch B removes.
   Make the glob cover every C/C++ source and header that exists after A–C (including .cc/.cpp files if
   the repo's .clang-format applies to them) and run the exact CI command locally until clean.

Also (owner standing rule: keep GitHub Actions at the latest major): bump actions/checkout@v7,
actions/cache@v6, actions/upload-artifact@v7, actions/download-artifact@v8, actions/setup-python@v7 in
every workflow under .github/workflows/. Check each bumped action's inputs used here still exist
(from memory of their changelogs; note any you are unsure of).

Validation: `meson setup` with the local Meson (1.11.2; no network for pip) for the plain, research, oxidd and native_gr1
configurations; full meson test serially for the oxidd+native_gr1 build; the exact CI clang-format
command; `python3 -c 'import yaml,sys; [yaml.safe_load(open(f)) for f in sys.argv[1:]]' .github/workflows/*.yml`.
Report per root cause what changed. VERDICT at end.

## Addendum (after cleanup A–C)

- I have fetched the crates for upstream OxiDD `be2f69b`'s exact `Cargo.lock` into the cargo
  registry cache (`cargo fetch --locked`). Before patch D, rebuild OxiDD with the exact upstream
  lockfile, offline and locked with `-j 1`, through `scripts/build_oxidd.sh`, without the
  validation-only substitutions from patch C. Then rerun, on that build:
  - `test/oxidd_manager_lifetime.c` in manager and checker modes, 10 repetitions each;
  - the Rust `repeated_manager_lifetimes_on_one_thread` test;
  - the full serial `meson test`.

  If `scripts/build_oxidd.sh` does not pass `--locked`, make it do so, because CI must build
  exactly the committed lockfile.
- Move the scratch directory `external/oxidd/build-oxidd-review/` out of the submodule into
  `build-cleanup/`, so the submodule shows only the patch.
