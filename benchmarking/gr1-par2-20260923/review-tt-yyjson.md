# Review brief — tlsf-tools yyjson migration (patch F, uncommitted on native-api at ed8df90)

Reviewer; do not edit tracked files; write /home/gperez/GIT-repos/tlsf-tools/build-cleanup/F-REVIEW.md.
Brief: benchmarking/gr1-par2-20260923/brief-tt-yyjson.md in the Acacia repo; implementation report:
build-cleanup/F-REPORT.md and the F-* logs there. A timed benchmark is running: prefer static review;
you may rerun single focused tests with one job, never /tmp, no network.

Check hardest:
1. **Checker strictness is not reduced.** Compare the old hand-written readers (git show
   ed8df90:src/gr1_check.c) with the new yyjson path. Check every required key, its type, its
   location, and its value constraint. Check duplicate keys at every depth, depth and size limits,
   and the allocation budget. Can any certificate or policy that the old checker rejected now be
   accepted? List each difference.
2. **Byte-level changes.** Are the report's claims supported by its evidence files? In particular,
   are the native reduction and lift outputs identical, and which sidecars changed whitespace only?
   Is `test/native_oracle.py`'s switch to parsed-JSON comparison limited to the listed outputs, or
   does it also weaken exact comparisons elsewhere? Could anything that hashes a sidecar (proof
   binding in tlsf-tools or Acacia, recorded fixtures) now mismatch across producer and consumer?
3. **The C++ facade over yyjson** in gr1_reduction.cc and gr1_lift.cc. Check document lifetimes
   (no use after `yyjson_doc_free`), handling of allocation failure (no silent partial output),
   number formatting (integers exact, non-finite rejected), and duplicate-key rejection on every
   input the library parses.
4. **The build.** The dependency and fallback wrap, the install patch (`diff_files`),
   `Requires.private`, .gitignore, and the CI changes. Is anything platform-specific (e.g. the
   shared vs static yyjson from the fallback)? Does the library still never print?
5. Anything in the diff beyond the brief.
End with VERDICT: ACCEPT / ACCEPT WITH FIXES (list) / REJECT.
