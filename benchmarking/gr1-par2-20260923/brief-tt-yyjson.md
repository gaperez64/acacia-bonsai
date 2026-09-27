# Brief — replace Boost.JSON and our hand-written JSON code with yyjson (tlsf-tools #39)

Owner decision (2026-09-25): use yyjson (MIT, C) instead of Boost.JSON, and drop our own small JSON
parsers and writers in favour of it. One JSON library serves both the C and the C++ parts.

Workspace /home/gperez/GIT-repos/tlsf-tools, branch native-api at ed8df90. Leave the submodule's
applied OxiDD patch and the owner's untracked git-derived-versioning.md alone. NEVER stage or
commit. No network. The yyjson 0.12.0 WrapDB wrap is installed at subprojects/yyjson.wrap, and its
source and patch are downloaded (subprojects/packagecache, subprojects/yyjson-0.12.0). A TIMED
BENCHMARK is running: one job for every build, bounded serial tests, never /tmp;
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. Write build-cleanup/F-yyjson.patch and
build-cleanup/F-REPORT.md.

## Scope
1. Remove Boost entirely: from src/gr1_reduction.cc, src/gr1_lift.cc, test/native_lift_api.cpp,
   meson.build (dependency, partial dependencies, pkg-config requirements), CI apt lists, and docs.
2. Replace every hand-written JSON reader and writer in src/ with yyjson (reading and mutable
   writing). Known sites:
   - gr1_check.c: the `json_*` readers and writers;
   - gr1_oxidd.c: the `json_string` / `json_predicate` writers;
   - main_tlsfsolve.c, provenance.c, pipeline.c, main_tlsf2tlsf.c.

   Find every other site with `git grep` for escaping helpers and fprintf calls that emit JSON
   keys. Tests in C/C++ that hand-parse JSON move to yyjson too. Python tests keep using `json`.
3. Meson: `dependency('yyjson', version: '>=0.12.0', fallback: ['yyjson', ...])`, with the
   pinned wrap as the fallback so CI and offline builds agree. Add it to every target that now
   uses it (the core library if provenance.c/pipeline.c use it, not only native_gr1). Handle the
   installed static libtlsf correctly: pkg-config Requires.private, or bundling when the fallback
   is used. Ignore subprojects/packagecache and extracted subproject directories in .gitignore,
   keeping *.wrap tracked. Update the CI jobs: no Boost; yyjson comes from the wrap.

## Invariants (report each explicitly)
- **Hashes and bytes.** Outputs that are hashed or compared byte-for-byte must stay identical
  unless there is a stated reason:
  - certificate, policy, provenance and game metadata, lifting evidence;
  - CLI JSON outputs checked by the oracle test against 8b158d7;
  - fixtures with recorded SHA-256.

  First try yyjson writer flags and key insertion order that reproduce today's bytes. Where exact
  reproduction is impossible (e.g. separators or float formatting), list each changed output, show
  one before/after example, and say which tests or goldens had to change and why. Never update a
  golden or a recorded hash silently.
- **The checker stays at least as strict as today.**
  - It requires the same keys and types, and rejects non-finite numbers, trailing garbage, and
    truncated or oversized inputs.
  - It keeps the existing size and allocation caps (json_limited / json_allocation_failed paths).
    Use a bounded allocator where the old code bounded memory.
  - Duplicate keys: reject them in certificate, policy and provenance inputs. yyjson does not do
    this by itself, so check it while iterating. A duplicate `source_sha256` must not let the
    checker and the solver read different values.
- **Library behaviour.** The library still never prints to stdout or stderr, results stay
  transactional, and every allocation-failure path is handled.

## Validation
- Full serial meson test in the oxidd+native_gr1 build and in a plain `-Doxidd=disabled` build.
- ASan/UBSan on the native API selection, and the lift and reduction differentials.
- The oracle test against 8b158d7.
- The exact CI clang-format command.
- `grep -rn boost` returns nothing outside history.

Also list what Acacia must change. Its native arms use Boost.JSON (src/native_proof_binding.hh,
src/native_gr1_arm.hh) and consume tlsf-tools as a Meson subproject: does the nested yyjson wrap
resolve, or must Acacia add or promote its own wrap? End with a VERDICT.
