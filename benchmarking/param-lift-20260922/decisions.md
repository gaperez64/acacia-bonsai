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
