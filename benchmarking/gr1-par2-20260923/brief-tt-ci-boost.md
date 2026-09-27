# Brief — tlsf-tools #39 CI: build-native-gr1 lacks Boost.JSON

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api at c1bebaf (clean except the
submodule's applied OxiDD patch and the owner's untracked git-derived-versioning.md; do not touch
either). NEVER stage or commit. A timed benchmark AND a release build are running on this machine:
do NOT compile or run tests; this is an edit-only task validated by hosted CI.

CI run 36133589590: every job passes except build-native-gr1, which fails at configure with
`meson.build:346:25: ERROR: Dependency "boost" not found (tried system)`. meson.build requires
`dependency('boost', modules: ['json'])` when native_gr1 is enabled, and the job's apt list
(.github/workflows/ci.yml, job build-native-gr1, step "Install toolchain") lacks it.

1. Add the Ubuntu 24.04 package providing Boost.JSON headers and library (libboost-json-dev) to
   that apt list. Check every other workflow job and workflow file that configures with
   -Dnative_gr1=enabled or builds the native API, and add it there too if missing.
2. Check README/docs build instructions for native_gr1: if they list dependencies (Spot, OxiDD),
   add Boost.JSON the same way.
3. Parse the workflow YAML with python3 -c 'import yaml' to confirm it is valid.
Report the diff and VERDICT.
