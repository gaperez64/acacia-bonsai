# Brief P5-tt — relational region checking in tlsfcertcheck (tlsf-tools), new versioned method

Workspace /home/gperez/GIT-repos/tlsf-tools, branch gr1-par2-checker at bd14c61 (I commit). Read the
plan (/home/gperez/GIT-repos/acacia-bonsai/benchmarking/gr1-par2-20260923/plan.md) §2, §4.1 (root
masks are method-specific), §8 (THE SPEC — read every line), §9.1, §10.1 last paragraph; the
existing certificate format/docs for GR(1) certificates (winning region, goals, rank layers,
most-permissive move relation `move_*`), main_tlsfcertcheck.c's certificate method and its tests.
Goal: `--method region` (new versioned result) that checks a REAL GR(1) certificate WITHOUT a
policy: for each scheduler mode j (all-zero and each one-hot, never merged), reconstruct from the
ACTUAL GAME and the validated rank certificate a joint predicate Good_j(s,u,c) = safety ∧
successor ∈ same invariant ∧ the correct rank-progress/counter rule (exact existing fairness/
justice timing and least-rank rule), and check: reset ∈ W; all proof predicates have valid support
and rank shape/coverage; ∀ s∈W, ∀ u: ∃ c. Good_j(s,u,c). The existential is over ONE joint c
(never separate existence checks per conjunct). Prefer per-disjoint-rank-layer checks to one giant
mux where the same joint condition is established exactly. The certificate's `move_*` functions are
NOT trusted premises (the checker derives Good_j from the game) — you may use them only as an
optional hint that is re-verified, or not at all; say which.
Deliverables:
1. docs/gr1-region-method.md: precise statement of what is checked and a written sufficiency
   argument (finite deterministic game: existence of such choices + checked ranking/counter
   discipline ⇒ a winning strategy exists) with the exact timing conventions. Name any assumption.
2. Implementation reusing the selected-root builder (method-specific root list), mode
   specialization, substitution reuse and resource conventions from P1; no call into the solver's
   algorithm; `--method region` result string distinct (e.g. REGION_VERIFIED) and versioned; UNREAL
   never produced by this method; closed-loop/certificate methods unchanged; default behaviour
   byte-identical.
3. Tests: agreement with the explicit tiny-game solver on exhaustively enumerated small games
   (REAL games → REGION_VERIFIED with valid certificates; UNREAL games → never verified); mutation
   tests: non-total region (a state/input with no good c), incompatible-choice certificate (safe c
   and progressing c exist but no single c is both), rank shape, fairness timing, reset ∉ W,
   support violations, per-mode mutations incl. all-zero vs one-hot-0 differences. Certificates
   produced by tlsfsolve's existing export must REGION_VERIFY on all REAL fixtures.
4. --stats for the new method (roots, per-mode/per-layer timings).
Build one job only under build-p5/ (meson setup -Dbuildtype=release -Doxidd=enabled
-Dresearch_tools=true, PKG_CONFIG_PATH=/usr/local/lib/pkgconfig); full suite `meson test
--num-processes 1`; clang-format; never /tmp. MACHINE MAY BE RUNNING TIMED MEASUREMENTS: before
compiling or running the test suite, wait until
/home/gperez/GIT-repos/acacia-bonsai/build_scratch/seq4/progress.txt is absent or has a line
starting "DONE" (poll every 60 s). Leave uncommitted. VERDICT at the end.
