# Brief P1 — tlsfcertcheck: build only the needed BDDs (tlsf-tools)

Workspace: /home/gperez/GIT-repos/tlsf-tools, branch gr1-par2-checker (already checked out; starts at
main 6b2507f). git commit/checkout are blocked for you; I commit. Leave the untracked
git-derived-versioning.md alone. Read first: the plan
/home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/plan.md §2 (invariants), §4 (P1,
all of it), §5A S4, §6.3, §9.1, §10.1 last paragraph; this repo's README/CONTRIBUTING/docs on build
and tests; include/tlsf/oxidd_common.h and src/oxidd_common.c (oxidd_build_roots and its tests).
Machine rules: build with one job only (`meson compile -j 1`, `meson test --num-processes 1`); scratch
and build dirs under this repo as build-p1*/ (never /tmp); PKG_CONFIG_PATH must include
/usr/local/lib/pkgconfig. Configure with the same options as
/home/gperez/GIT-repos/acacia-bonsai/subprojects/tlsf-tools/build-oxidd (see its meson-info).

Deliver three separately reviewable steps, in this order, each leaving the full test suite green.
After each step, write a patch file build-p1-patches/step<N>.patch (`git diff` output relative to the
previous step, include new files via `git add -N` first — that is allowed) and a one-paragraph note,
so I can commit them as three commits.

1. P1.1 selected roots. Replace compile_aig's whole-AIG map (main_tlsfcertcheck.c ~958-1054 and the
   setup_bdds callers) with the existing oxidd_build_roots (adapt it narrowly if needed; do not write
   another gate scheduler). Root lists are derived from the chosen --method; fixed-policy certificate
   checking must not evaluate move_* cones (they stay structurally validated). Keep full structural
   validation of every supplied AIG. Tests from §4.1 (equivalence old vs new roots on small arbitrary
   AIGs over the same manager/mapping, duplicate roots, complemented aliases, shared gates, constants,
   dead gates, invalid refs, allocation-failure injection at each publication point using the
   existing host-allocation-failure boundary, malformed unused cone still rejected, large valid unused
   move cone not evaluated — prove it with a counter/diagnostic, not timing).
2. P1.2 mode specialization before compilation. For certificate checking (system and environment
   certificates), per supported mode (all-zero AND every one-hot, never merged) build the policy input
   map with counter bits as constants and compile only the needed control/counter-next roots under it;
   run the existing per-mode conditions; release mode-local roots before the next mode. Closed-loop
   method (and the closed-loop leg of --method both) keeps its existing full construction. Tests from
   §4.2: mutants changing only all-zero policy behaviour, only a counter update, one fairness mode, one
   rank layer — the optimized checker rejects exactly where the old one does (keep the old path
   available behind a hidden/test flag or compare against the build of main 6b2507f as oracle); a
   large selector whose constant-cofactored cones are small is built from constants (diagnostic count).
3. P1.3 per-mode reuse. One immutable substitution per specialized mode; goal-successor ∧ invariant
   once per mode; each fairness successor once per mode on first use; lower-rank image once per
   level; scope-local, bounded; nothing crosses policy/order/game/manager lifetime.
   Tests: status equality with step 2 on all existing certificate fixtures plus mutation set;
   a diagnostic count showing successor() substitution construction count dropped.

Also add (small, can be in step 1) a checker --stats/diagnostic output (stderr or a JSON file behind an
option, off by default) reporting: AIG gates visited, requested roots, per-phase setup/proof time,
peak live nodes sample if the OxiDD API gives it, final status. Default stdout must remain
byte-identical.

Do NOT change the certificate format, proof obligations, or the solver. Keep the independent checker
independent of tlsfsolve's algorithm. Run clang-format with the pinned config on changed C files.
Final message: a VERDICT section — per step: what changed, test counts, and anything suspicious.
