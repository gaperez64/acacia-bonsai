# Brief — fixes for the yyjson migration (tlsf-tools build-cleanup/F-REVIEW.md, ACCEPT WITH FIXES)

Workspace /home/gperez/GIT-repos/tlsf-tools, native-api at ed8df90 plus the uncommitted F state.
NEVER stage or commit. A timed benchmark is running: one job, serial, never /tmp, no network.
Update F-yyjson.patch (or add G-yyjson-fixes.patch) and F-REPORT.md.

1. **Escaped key spellings (decision: keep decoded semantics).** The checker now matches decoded
   keys and string values, so `"format"` is accepted where the old byte-matching reader
   rejected it. The owner's side has decided that decoded-JSON semantics are the intended
   contract, because every reader in the chain decodes: the checker, Acacia's proof binding and
   the Python oracle. The old lexical rejection was an artefact of the hand-written reader, and a
   mismatch between readers is the real risk.
   - Do not restore the lexical rule.
   - Add checker tests: an escaped spelling of a required key or fixed value is accepted exactly
     like the plain one; a document with both `"format"` and `"format"` (or both spellings
     of `source_sha256`) is rejected as a duplicate.
   - State the contract in the checker's header or doc comment: sidecars are compared as decoded
     JSON; duplicate keys are rejected after decoding; extra fields are allowed.
   - Update the report's strictness section accordingly.
2. **Leak on allocation failure.** Free the buffer returned by `yyjson_mut_write` when
   `std::string` construction throws, using RAII in src/yyjson_cpp.hh. Check every other
   yyjson-owned pointer in the facade and the C code for the same pattern.
3. **Packaging.** yyjson is an implementation detail of libtlsf, so the generated `tlsf.pc` should
   list it under `Requires.private`, not `Requires`. Fix that, and verify a staged install and an
   external link with `pkg-config --static --libs tlsf` for both `default_library=static` and
   `shared` (configure plus the minimal targets only, -j 1). Correct the report's wording.
4. **Report: the Acacia follow-up list must also name `src/native_param_lift_arm.hh`**, which
   uses Boost.JSON for evidence parsing and for mutation and serialisation in its test hooks.

Validation: the checker and native API tests, the full serial native suite (one job), and the
exact CI clang-format command. VERDICT at end.
