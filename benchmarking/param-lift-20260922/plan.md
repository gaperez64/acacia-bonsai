# Parametric witness lifting, done the Maderbacher–Bloem way

## Context

The sprint's W5/W7 "generalize small solutions" pipeline certified exactly one family
(`arbiter`, n=10). It failed on `round_robin_arbiter` (W5: `no_recognized_candidate`) and on
`round_robin_arbiter_unreal2` (target timeout), and it never touched the other 33 parametric
families that still have unsolved SYNTCOMP26 instances. The precedent is Maderbacher & Bloem,
*Parameterized Infinite-State Reactive Synthesis* (arXiv 2508.00613): solve GR(1) instances for a
few parameter values with a symbolic fixpoint solver, **generalize the guards and the
winning-region invariant** by anti-unification + SyGuS over a small grammar, synthesize **ranking
functions** for the justice goals, and verify with **one consistency check** (invariant
inductive, ranks decrease), looping on a counterexample parameter (CEGIS). Goal: rebuild our
lifting on that recipe and cover as many of the 36 unsolved parametric families as it honestly can.

## What we got wrong (evidence-backed)

1. **Wrong seed representation.** We generalized Acacia's antichain-derived AIGs — one arbitrary,
   optimized strategy with arbitrary tie-breaking. M&B generalize *symbolic* guards produced by a
   structured fixpoint solver. The handoff itself warned "do not assume arbitrary optimized AIG
   latches reveal a semantic counter."
2. **Recognition, not generalization.** W5 matched one hard-coded shape (pending flags + cyclic
   index) using behavioral probes. On `round_robin_arbiter` it demanded a grant *after a request
   drops* — but that family's assumptions (`r ∧ ¬g → X r`) forbid dropping, so the environment is
   the memory and no pending flag exists. The probes ran on assumption-violating inputs.
   No anti-unification, no grammar hole-filling was ever implemented.
3. **No invariant, no ranking.** We kept only the controller and threw away the solver's
   certificate. M&B's verification *is* the generalized invariant + ranking.
4. **Verification by re-translating the whole spec at N.** `verify_conjuncts.py`/Spot build an
   automaton for the complete formula at the target: n independent fairness assumptions force a
   3ⁿ product (measured). M&B check local, one-step conditions whose size doesn't grow that way.
5. **Fixed seeds, no CEGIS.** {2,3} + sanity 4, no refinement on failure.
6. **Ignored existing GR(1) machinery.** tlsf-tools already has GR(k) recognition (`src/gr.c`),
   a `Gr1Parts` decomposition with response/weak-until monitor latches
   (`src/compose_internal.h`, `aig_gr1_parts`), an AIGER GR(1) game builder
   (`build_aig_gr1_game`) and a Piterman–Pnueli–Sa'ar solver on OxiDD (`src/gr1_oxidd.c`) that
   already computes the winning region Z and μY rank layers internally — unused by us.
7. **Parameter semantics differ from the paper.** M&B's parameter is an integer over a *fixed*
   variable set; SYNTCOMP's n grows the interface (`r[n]`, `g[n]`). Anti-unification across n needs
   index alignment (signal-origin map `g_i ← g[i]`, index-aligned monitor latches) — this bridge is
   our genuine research contribution, not something the paper provides.

Measured today: `tlsfcompose --route-stats` routes `round_robin_arbiter` to the exact GR(1) path,
but `arbiter`, `rru2`, `load_balancer`, `prioritized_arbiter`, `lift`, `amba_decomposed_arbiter`,
`arbiter_with_cancel` are `nested_unsupported`. Direct GR(1) solving of `round_robin_arbiter`:
n=6 0.5 s, n=8 20 s, n=10 >60 s — seeds are cheap, direct solving doesn't scale, a certificate
check at N is the piece that pays off.

## Decisions (confirmed)

- **Verification bar:** tier 1 — certify each requested target N via the generalized
  certificate checked at N. All-n proof (tier 2) is a stretch goal after tier 1 works.
- **Code home:** GR(1) extensions go into **tlsf-tools** (`subprojects/tlsf-tools`, own git,
  currently `b42d5ef`), on a new branch there; Acacia's subproject pin is bumped only with explicit
  approval. Generalizer, checker driver and campaign live in Acacia's research layer.
- **Scope order:** REAL families first (M0–M4, M6); UNREAL (M5) immediately after.
- Precedent assumed to be Maderbacher & Bloem (arXiv 2508.00613), already cited in the handoff's
  source map [S12].

Execution order: M0 → M1 → M2 → M3 → M4 → M6 (REAL campaign) → M5 → M6 again (UNREAL) → M7.
Workflow as in the sprint: codex writes code with an independent codex review; I drive git,
experiments and independent re-verification of every VERIFIED claim; heavy runs cgroup-wrapped
and serialized.

## Approach

M&B's four stages, adapted to interface-parameterized TLSF, on top of tlsf-tools' GR(1) stack.

### M0 — Census (no new code beyond a driver)
Run `tlsfcompose --split --route-stats` over all instances of the 36 families
(list from `opening/family-frontier.tsv` ⋈ exact-param provenance) and time the direct GR(1)
route per n. Output `families/gr1-census.tsv`: route class (gr1 / nested_unsupported / other),
liveness class, REAL/UNREAL, parameter dimension, direct-solve scaling. Drives prioritization.

Also in M0: capture one W7 evidence run on `round_robin_arbiter` seeds and dump the all-high
cycle and the failed predicate, to confirm the diagnosis in item 2 above before building on it.

### M1 — Extend the GR(1)-with-monitors reduction (tlsf-tools)
In `src/compose_analysis.c` (`aig_gr1_parts` family) and `src/compose_games.c`
(`build_aig_gr1_game`), accept the nested shapes these families use, each via an exact,
*per-conjunct* rewrite or small deterministic monitor (never a whole-formula automaton):
- persistence antecedents: `G((a ∧ G¬r) → F¬g)` ≡ `GF(¬g ∨ r)` (justice);
- response with bounded X in the request: `G((r₀ ∧ X r₁) → F(g₀ ∧ g₁))` via a delay latch;
- W with bounded X (`G((g ∧ X(¬g ∧ ¬r)) → X(¬g W r))`) and W-release safety (collector).
Monitor latches are emitted per index, so they are index-aligned across n by construction.
Each rewrite gets a Spot equivalence test on the conjunct (tlsf-tools `test/`).

### M2 — Export the certificate from the GR(1) solver (tlsf-tools)
`src/gr1_oxidd.c` / `include/tlsf/gr1_oxidd.h` / `tlsfsolve`: optionally emit Z (winning
region) and the rank layers Y^j_k as extra AIGER outputs (`inv`, `rank_j_k`), plus the dual
(environment) region and counter-strategy on UNREAL. Default behavior unchanged.

### M3 — Target certificate checker (tier 1 verifier)
New research module (Python, Spot/BuDDy, or C on OxiDD if BDD sizes demand):
instantiate candidate (controller, Inv, rank) at N over the M1 game and check M&B's
Φ_Inv and Φ_Inv,(l,r) (init ⊆ Inv; Inv closed under controller × all assumption-respecting env
moves; non-deadlock; rank decreases toward each justice goal under the fairness-selecting `l`).
Soundness bridge: the M1 reduction is exact per conjunct (M1 tests), and for n where it is
feasible the controller is also re-checked against the original lowered LTL with the existing
`verify_aiger_ltl.py` / `verify_conjuncts.py`. Returns the handoff's
`VERIFIED | REFUTED | UNKNOWN | INVALID`; only VERIFIED is decisive.

### M4 — Index-aware generalizer (replaces W5's recognizer)
New module (e.g. `families/proposals/generalize_gr1.py`), reusing `propose_schema.py`'s
`ProposerLimits`/`CandidateSchema`/cap-32/determinism patterns:
1. Seeds: M1+M2 at the smallest legal n values (sub-second).
2. Index canonicalization: per-output guards, Z and rank layers as BDDs over index-aligned
   variables; relabel relative to the output's index (use tlsf-tools signal-origin provenance;
   Acacia's `symmetry::analyze_indexed_aps` for rotation/permutation classes).
3. Generalize (M&B GeneralizeExpr): anti-unify canonical ISOP covers across i and n; fill holes by
   enumerative search over {c, n, n−c, i, (i±1) mod n, cyclic distance, ∀j≠i / ∃j≠i of a local
   predicate, pointer}; bounded `ite` case splits over parameter regions. Generalize the
   *most permissive* guards consistent with Inv, not one strategy's tie-breaking.
4. Ranking: generalize rank layers into R ::= S | S+R, S ::= V | abs(T−T) | cyclic-dist.
5. CEGIS: check at next small n, then at N; on REFUTED add that n to the seeds, re-generalize,
   bounded rounds. Refuted candidate ⇒ UNKNOWN, never the opposite verdict.

### M5 — UNREAL families (after the REAL campaign)
Dual certificate from M2 (environment region + counter-strategy + environment rank), generalized
the same way; plus small-support padding (handoff §8.3) for pairwise obstructions (`rru2`,
`prioritized_arbiter_unreal2`, `load_balancer_unreal2`), checked at N by M3's dual conditions.

### M6 — Integrate and measure
Swap M4/M3 into `acacia-witness-lift.py` (keep its shared deadline, cold workspace,
`PipelineResult` VERIFIED-only invariant, evidence sidecar). Cold 120 s campaign over the 172
unsolved instances; report per family: census class, lifted?, verified at target?, failing stage.
Update `closing/`-style report and `families/target-checks.tsv`.

### M7 — Stretch: all-n proof (tier 2)
M&B's single QF-LIA query needs a fixed-variable encoding; interface-growing families need
quantified index reasoning or a cutoff argument (Jacobs/Khalimov/Bloem token-ring line; to be
checked for applicability). Requires installing z3/cvc5 (none present). Only after tier 1 works.

## Expected coverage (to be confirmed by M0)
Primary: arbiter-like and scheduler families — `arbiter*`, `round_robin_arbiter`,
`prioritized_arbiter`, `simple_arbiter_with_hints`, `rw_arbiter`, `abcg_arbiter`,
`load_balancer*`, `amba_decomposed_*`, `generalized_buffer`, `lift*`, `collector_v*` (~20
families). Likely out of reach and reported as such: `*_enc` families (n changes bit-width, no
index alignment), `chomp`/`robot_grid` (two-parameter games), `ltl2dba_*` (n is formula size).

## Verification
- tlsf-tools unit tests: each M1 rewrite equivalence-checked with Spot; M2 export agrees with
  Spot-computed winning region/strategy on small n; default `tlsfsolve` output byte-identical.
- M3 mutation tests: corrupt Inv / rank / controller / drop an AP ⇒ REFUTED or INVALID; differential
  vs `verify_aiger_ltl.py` on small n for both REAL and UNREAL.
- M4 tests: handoff §11 cases (seeds pass/target fails, n-dependent switch, ambiguous alignment
  declines, cap and determinism).
- End-to-end regressions: `arbiter` n=10 still VERIFIED; `round_robin_arbiter` n=10 (was
  `no_recognized_candidate`); `rru2` n=7 (was timeout); then the M6 campaign with coverage deltas
  vs B/S/ltlsynt.
