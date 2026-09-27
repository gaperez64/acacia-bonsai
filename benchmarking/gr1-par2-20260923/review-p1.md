# Review brief — P1 (tlsfcertcheck builds only the BDDs it needs)

Reviewer; do not edit tracked files. Workspace /home/gperez/GIT-repos/tlsf-tools (branch
gr1-par2-checker, uncommitted). Patches build-p1-patches/step{1,2,3}.patch with notes step*.note apply
in sequence onto main 6b2507f. Brief: /home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/brief-p1.md;
plan: .../plan.md §2, §4, §5A S4. Report to build-p1-review/REPORT.md in the tlsf-tools repo.
Build with one job only (`meson compile -j 1`, `meson test --num-processes 1`); scratch under
build-p1-review/, never /tmp; PKG_CONFIG_PATH=/usr/local/lib/pkgconfig. You may reuse the existing
build-p1* dirs read-only or configure your own under build-p1-review/.
This is a proof checker: soundness dominates. Look hardest for anything that could make the
optimized checker ACCEPT a certificate/policy the old one rejects.
1. Selected roots: every root the chosen --method's proof obligations need is built; nothing a
   premise needs is skipped; move_* cones still structurally validated; refcounts correct for
   duplicate / complemented roots; allocation failure never yields VERIFIED.
2. Mode specialization: all-zero and every one-hot mode are checked separately (never merged);
   counter bits truly become constants before compilation; environment certificates' modes likewise;
   closed-loop and the closed-loop leg of --method both still use the full unspecialized policy.
3. Caches: no cache crosses mode, policy, game, substitution, or manager lifetime; lower-rank images
   computed per level are the same function the old per-entry code computed; bounded memory.
4. Differential: build main 6b2507f's tlsfcertcheck (or use
   /home/gperez/GIT-repos/acacia-bonsai/subprojects/tlsf-tools/build-L0/tlsfcertcheck, read-only) and
   the new one; on every certificate fixture in test/ plus your own mutations (at least: flip one
   all-zero-mode policy output; corrupt one counter-next; drop one rank layer; swap one fairness
   predicate; make a move_* cone malformed) compare exit status and stdout. Report any disagreement.
5. Hidden oracle flags (--test-unspecialized-policy, --test-rebuild-successor): not reachable in a
   way that changes default behaviour; default stdout byte-identical to main.
6. Test count 283 vs 285 at L0 build: explain the difference precisely.
7. New src/oxidd_host.c and meson.build changes: justified, minimal?
End with `## VERDICT` ACCEPT / ACCEPT-WITH-NITS / REJECT, per step, and numbered findings with severity.
