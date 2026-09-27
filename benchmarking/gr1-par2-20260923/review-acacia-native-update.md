# Review brief — Acacia native arms on tlsf-tools bf3e7e7 with yyjson (uncommitted)

Reviewer; do not edit tracked files; write build_scratch/review-acacia-native-update/REPORT.md.
Scope: the uncommitted diff in /home/gperez/GIT-repos/acacia-bonsai: src/native_*.hh,
tests/meson.build, tests/native_json_binding_test.cc, the guard test, and the tlsf-tools pointer
85e3a31 → bf3e7e7. Brief: benchmarking/gr1-par2-20260923/brief-acacia-native-update.md. A timed
benchmark is running: static review first. You may rerun single focused tests (one job, serial,
never /tmp) in the build_scratch build directories the implementer left.

Check hardest:
1. **Proof binding is not weakened.** Compare old and new native_proof_binding.hh,
   native_gr1_arm.hh and native_param_lift_arm.hh line by line. Every field and hash checked before
   (source, game, certificate and policy SHA-256; side, status, semantics, format, method,
   policy_sha256) is still checked, with the same comparison: length-aware, exact equality, and
   the same failure diagnostics and exit codes. List any check that was dropped, loosened or
   reordered in a way that changes behaviour.
2. **The yyjson reader.**
   - Duplicate keys are rejected after decoding at every depth.
   - Documents are freed on all paths, with no use after free: check the pointers into
     `yyjson_doc` kept past the free.
   - Embedded NULs in strings are handled.
   - Allocation failure maps to a clean UNKNOWN, never a verdict.
   - The debug-only test hooks that rewrite evidence are compiled out of release builds, and
     check-native-release-no-hooks still proves it.
3. **API migration to bf3e7e7.** Struct fields are initialised completely now that `struct_size`
   and `abi_version` are gone: no uninitialised new fields and no zeroing of fields that need
   non-zero defaults. Also check limits, deadlines and cancellation wiring unchanged, and the
   same entry points semantically.
4. **The guard.** It now names the flat tlsf-tools files and asserts that each exists. Is the set
   complete? Every tlsf-tools source on the native verdict path must be scanned: reduction,
   lifting, checker, service, pipeline source, gr1_oxidd and the CLIs listed before. Nothing may
   be dropped relative to the old glob plus explicit list.
5. **Build.** yyjson comes only through tlsf_dep or `dependency('yyjson')`, with no second copy.
   Check release/LTO and non-native configurations. Is the new unit test registered in the right
   suites?
6. Anything beyond the brief.
End with VERDICT: ACCEPT / ACCEPT WITH FIXES (list) / REJECT.
