# Parametric witness lifting (Maderbacher–Bloem style) — running decisions log

Follow-on to `../witness-lifting-20260918/` (closed; see its `closing/README.md`). `plan.md` is
the approved plan, copied verbatim. Branches: Acacia `sprint/param-lift-20260922`, tlsf-tools
`param-lift-gr1` (from the pinned `b42d5ef`; Acacia's subproject pin is not bumped without
explicit approval).

## M0 — census (complete)

**Target set.** `m0-instances.tsv`: every exact-parametric instance of the 36 families that still
have an instance unsolved by both B and S at 120 s in the W1 campaign — 413 instances, 172
unsolved, all present in `tlsf-corpus/`.

**Current routing.** `m0-route-stats.tsv`, produced with tlsf-tools' own
`scripts/collect_route_stats.py` over `tlsfcompose --split --route-stats` (no new driver). Today
only `round_robin_arbiter` and `round_robin_arbiter_unreal1` route to the exact GR(1) path (25
unsolved instances); `amba_decomposed_encode`/`_lock` route to the exact safety path; every other
family is rejected — almost always `nested_unsupported` ("liveness temporal operators are not
OxiDD-eligible"), `collector_v1` for weak-until release, `chomp` for until.

**Conjunct classes — the decisive result.** `m0-census.tsv` (`m0-census.py`): lowering each
family's smallest instance and classifying every top-level conjunct of `A → G` by its
Manna–Pnueli class shows **29 of 36 families have every conjunct, on both sides, at most
recurrence class**, i.e. each conjunct has a deterministic Büchi monitor, so the objective is
*exactly* a GR(1) condition over the game × one small monitor per conjunct. Those 29 families hold
**147 of the 172 unsolved instances** — the ceiling for this approach. The 7 that are not reducible
contain reactivity-class conjuncts: `collector_v2`, `generalized_buffer`, `lift_gr1`, `lift_gr1+`
(a nested `A → G` with `FG` inside the guarantee), and the three `ltl2dba_*` (`GF a ↔ …`).

**Design refinement for M1 (within the approved plan).** The plan allowed "an exact per-conjunct
rewrite or small deterministic monitor". Given the census, a generic per-conjunct deterministic
Büchi monitor construction is exact for all 29 families, where pattern-by-pattern C rewrites in
`aig_gr1_parts` would each cover a handful. Each conjunct mentions one or two clients' signals, so
each monitor is constant-size and the total is linear in n — never a whole-formula automaton.
tlsf-tools already carries Spot as an optional dependency with Spot-Python scripts
(`scripts/verify_aiger_ltl.py`), so M1 is a Spot-Python game builder in tlsf-tools/scripts emitting
the AbsSynthe GR(1) format that its existing `tlsfsolve` (Piterman–Pnueli–Sa'ar on OxiDD) solves.
M2 remains a C change to `src/gr1_oxidd.c` to export the certificate.

**Direct GR(1) scaling, for reference.** `tlsfcompose --split --aiger` on `round_robin_arbiter`:
n=6 0.51 s, n=8 20.1 s, n=10 >60 s (timeout). Small seeds are cheap; direct solving at the targets
is not — the certificate check at N is where lifting has to pay off.

**W5's failure, confirmed on the real seeds.** Re-ran W7 on `round_robin_arbiter` (still
`UNKNOWN schema_proposal no_recognized_candidate`) and ran W5's own `observe_seed` on the fresh
seeds. The n=2 seed has **one reachable state** — a stateless controller; under all-high requests
it grants client 0 forever, legal only because that input violates the fairness assumption
`GF¬(r₀ ∧ g₀)`: the environment, not the controller, enforces the rotation. It never outputs
all-zero (`saw_zero_output=false`), which the recognizer required. The n=3 seed even violates
mutual exclusion on some BFS-reachable states — reachable only through assumption-violating inputs,
where plain `A → G` lets the controller do anything. W5 was mostly observing behavior on traces
the specification does not constrain; the generalizable object is the controller restricted to the
winning region under the assumptions, which is what Maderbacher–Bloem generalize.

## M1a — two soundness bugs in tlsf-tools' GR(1) path (inserted before M1)

Found while preparing M1, before building anything on the solver. M1's first launch was stopped
before it wrote any code (both trees verified clean) so the solver could be fixed first.

**Bug 1: `tlsfsolve` silently drops liveness.** `src/main_tlsfsolve.c` reads games with
`aig_read_aag`, which deliberately skips the AIGER 1.9 bad/constraint/justice/fairness records
("a synthesized controller only needs the circuit"). For any game read from a file,
`aig_num_justice`/`aig_num_fairness` are 0, `is_gr1` is false, and the game is solved as a safety
game with all liveness ignored — contrary to the README. Reproducer `m1a-repro/fair1.aag`: one
fairness `GF la` (latch copying input `a`) and justice `{false}`; under GR(1) semantics it is
UNREALIZABLE, but `tlsfsolve` exits 0 with a strategy.

**Bug 2: the GR(1) fixpoint is unsound with two or more fairness assumptions.** `src/gr1_oxidd.c`
uses `νX. cpre((Z∩goal_j) ∪ Y ∪ (X ∩ ⋃ᵢ ¬fairᵢ))` ("env breaks at least one"). Violating fairness
means `FG ¬fairᵢ` for some *fixed* i; with the union, a play can violate some assumption at every
step while satisfying every assumption infinitely often, so the winning region is
over-approximated and a non-winning strategy can be emitted. The standard fixpoint takes a
disjunction over separate inner νX, one per fairness assumption (Bloem–Jobstmann–Piterman–Pnueli–
Sa'ar, JCSS 2012). Reproducer `m1a-repro/fair2.aag` (`GF la`, `GF ¬la`, justice `{false}`;
correct answer UNREALIZABLE) — observable only once bug 1 is fixed.

**Impact.** Acacia's shipping solver never calls tlsf-tools' GR(1)/safety solvers, `tlsfcompose`
or `tlsfsolve`, so no SYNTCOMP verdict of Acacia's is affected. Affected: `tlsfsolve` on any file
game with liveness, and `tlsfcompose`'s in-process GR(1) route whenever a game has ≥2 fairness
records that cannot all hold in the same step. Measured before the fix
(`m1a-baseline-before-fix.tsv`, 20 s cap): on the two corpus families that use the route,
`round_robin_arbiter` and `round_robin_arbiter_unreal1`, every completed verdict matches the known
answer — their fairness assumptions (`¬(rᵢ ∧ gᵢ)`) are simultaneously satisfiable, so the union
happens not to matter there. It would matter for M1's per-conjunct monitors, whose accepting
states need not coincide. Also noted: the route already reaches `round_robin_arbiter` n=6/7/8
(0.3/2.6/15.5 s), which Acacia could not solve at 120 s; those strategies are unverified and came
from the flawed fixpoint, so no claim is made about them until M1a's re-run verifies them.

Fix delegated (codex, `param-lift-m1a`): a game-reading mode that keeps the records (rejecting any
it cannot represent), the standard per-fairness disjunction with matching strategy extraction, a
committed differential test against an independent brute-force explicit-state GR(1) solver on
random small games (with strategies checked independently), and before/after on the corpus
families.

### M1a results and independent review

Fix (uncommitted in tlsf-tools at the time of writing): a strict game reader for `tlsfsolve` that
retains justice/fairness/bad records (rejecting invariant constraints and unsupported resets), and
the standard per-fairness disjunction in `src/gr1_oxidd.c` with strategy extraction over
`X[k,i]`. Validated: `fair1.aag`/`fair2.aag` now UNREALIZABLE; committed differential test
(`test/test_gr1_differential.py`, 320 games, fixed seed) — old binary wrong on 115, union formula
alone wrong on 7, fixed solver wrong on 0, all 165 emitted strategies pass an independent SCC
check. I re-ran it on three fresh seeds the implementer never saw (1,800 games): 0 wrong, 956
strategies checked, 0 failures (the union formula would have been wrong on 35). 267/267 tlsf-tools
tests pass. Corpus (`m1a-after-fix.tsv`): no verdict changed; completed instances are 1.5–3×
slower with the correct fixpoint (`round_robin_arbiter` n=7 8.1 s vs 2.6 s; n=8 now exceeds 20 s);
n=10 and `rru1` (4,8) now fail fast on OxiDD's node capacity. Spot's AIGER reader asserts fewer
than 32 latches, so `verify_aiger_ltl.py` cannot check the n=6/7 controllers (33/38 latches) —
recorded as unverified; M3 needs its own AIGER-to-BDD path.

Independent review (codex, fresh session): the two original fixes are correct for state-based
acceptance — 80/80 agreement with **ltlsynt** (an unrelated solver, via an LTL encoding of each
game) on fresh random games, 43 REAL / 37 UNREAL, which closes the gap that the brute-force
reference was written in the same session as the fix. It found three further issues:

1. **High — input-dependent acceptance is unsound.** The fixpoint substitutes only latches, but
   AIGER allows justice/fairness literals over inputs. Counterexample `m1a-repro/input_fairness.aag`
   (fairness `GF(q ↔ u)`, justice `GF false`): UNREALIZABLE per ltlsynt, but `tlsfsolve` exits 0
   (I reproduced it). `tlsfcompose`'s in-process games are not affected — its builder samples each
   fairness assumption into a latch and uses pending latches for justice — so this hole was opened
   only by letting `tlsfsolve` accept arbitrary file games. Fix in progress: sample every
   input-dependent acceptance literal into a fresh latch at the solver entry (`GF ℓ ≡ GF Xℓ`), with
   the differential test extended to transition-level acceptance and a second ltlsynt cross-check.
2. Medium — the capacity-error path leaks the local `Y` reference (4,987 live nodes after cleanup
   at n=10; `tlsfcompose` shares a manager across clusters).
3. Low — `oxidd_bdd_ref` is taken before `bddvec_push`, leaking on realloc failure.

The review also measured that the n=10 capacity failure is the corrected algorithm's genuine
footprint, not the leak (1.31M live nodes at failure; disabling `X[k,i]` storage saves only 26).

### M1a follow-up — committed on tlsf-tools `param-lift-gr1` as `3a9ec71`

The review's three findings were fixed: input-dependent justice/fairness literals are sampled into
reset-0 latches at solver entry (exact, since `GF ℓ ≡ GF Xℓ`; state-based literals untouched, and
`tlsfcompose`'s artifacts on `round_robin_arbiter` n=2..5 are byte-identical before/after), the
capacity-error path releases `Y`, and references are taken only after a successful push.
`input_fairness.aag` is now UNREALIZABLE and registered as a test with the other two.

Numbers on the committed differential test (`test/test_gr1_differential.py`, 320 games, fixed
seed, 67 with input-dependent acceptance), measured by me with the archived original binary:
**original pinned binary wrong on 116**, of which 22 are input-acceptance games; the union
formula alone accounts for 6; **fixed solver wrong on 0**; 164 emitted strategies all pass the
independent SCC check. (The commit message says 115 — that figure is from the first M1a version of
the game set, before input-dependent games were added; 116 is the figure for the committed test.)

External cross-check, run by me with the reviewer's own generator and LTL encoding (independent
of the implementer): **ltlsynt agreed with the fixed `tlsfsolve` on 200 of 200 fresh games**
(seed 0x20260922; 100 with input-dependent acceptance, 61 REAL / 39 UNREAL; 100 state-based,
53 REAL / 47 UNREAL). tlsf-tools suite 268/268; the one CI clang-format issue (pinned 18.1.8, a
line join in `src/aiger.c`) was fixed before committing. Corpus table re-run
(`m1a-after-fix.tsv`): zero verdict changes; strategies for `round_robin_arbiter` n=2–5 verified
against the original LTL; n=6/7 remain unverified (Spot's AIGER reader refuses ≥32 latches, and it
also asserts on semantically duplicate AND nodes, which the implementer worked around with
strashed throwaway copies).

**Pin hazard.** tlsf-tools is a git submodule of Acacia pinned at `b42d5ef`. Its working tree is
now on `param-lift-gr1`, so Acacia's `git status` shows the gitlink as modified and any Acacia
rebuild compiles tlsf-tools from the branch. The gitlink is deliberately not staged (bumping the
pin needs explicit approval), and any Acacia measurement build must check out `b42d5ef` in the
submodule first.
