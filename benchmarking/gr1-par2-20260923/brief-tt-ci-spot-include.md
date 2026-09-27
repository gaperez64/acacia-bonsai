# Brief — tlsf-tools #39: libtlsf compiles without Spot/Boost include flags

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api at 6340ba5 (leave the submodule's
applied OxiDD patch and the owner's untracked git-derived-versioning.md alone). NEVER stage or
commit. A timed benchmark and another side task are running: CONFIGURE ONLY (meson setup /
introspection), no compiling, no tests; never /tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig.

CI run 36135034203, job build-native-gr1 (Spot 2.16 installed in a custom prefix, found by
pkg-config): `../src/gr1_reduction.cc:12:10: fatal error: spot/misc/optionmap.hh: No such file or
directory`. The compile line for libtlsf has no Spot include path: `tlsf_lib = static_library(...)`
uses `dependencies: lib_compile_deps` (OxiDD include dirs only), while spot_dep and Boost.JSON are
only in `lib_deps`, which reaches consumers through tlsf_dep. Locally it works only because Spot
lives in /usr/local/include.

1. Fix meson.build so that every libtlsf source is compiled with the compile arguments and include
   directories of every dependency it includes (Spot and Boost.JSON when native_gr1; OxiDD as
   now), e.g. with `partial_dependency(compile_args: true, includes: true)` added to
   lib_compile_deps. Keep link arguments flowing through tlsf_dep as now; do not change what gets
   linked. Check whether any other target compiles library sources directly (e.g. executables
   listing src/gr1_lift.cc or src/gr1_service.c) with a dependency set that lacks Spot/Boost flags,
   and fix those the same way.
2. Prove it without compiling: configure a fresh build dir (in the repo, e.g. build-spot-flags)
   with `-Dnative_gr1=enabled -Doxidd=enabled -Dresearch_tools=true -Dcpp_std=c++20`, then show
   from compile_commands.json that src_gr1_reduction.cc and every other Spot-including source
   now carries Spot's `pkg-config --cflags libspot` flags. For comparison, show the same entry at
   HEAD without the flags (configure HEAD in a second dir via `git stash`-free means, e.g. a
   `git worktree` in the repo's build_scratch-like location, not /tmp). Remove both build dirs and
   any worktree afterwards.
3. Also make the CI job fail fast on this class of bug: nothing extra is needed if the fix is
   right, but say whether any other native-API target would still depend on /usr/local.
Report the diff and a VERDICT.
