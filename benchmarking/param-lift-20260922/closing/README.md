# Closing report: parametric witness lifting, done the Maderbacher–Bloem way

Plan: `../plan.md`. `../decisions.md` is the running log with every measurement and judgment call in
the order it happened; this file is the synthesis against the plan's own verification criteria.
Read `../decisions.md` for anything summarized here.

## The question

Why did the previous sprint's parametric generalization fail, and can it be made to work across the
SYNTCOMP26 parametric families? The precedent is Maderbacher & Bloem, *Parameterized Infinite-State
Reactive Synthesis* (arXiv 2508.00613): solve small instances, generalize guards **and the winning
region**, synthesize ranking functions, discharge one consistency check, loop on counterexamples.

## Result

**26 previously-unsolved SYNTCOMP26 instances are closed, every one of them inside the 120 s budget
that defined it as unsolved**, at a total cold cost of 396 s with every stage charged
(`../m6-campaign.tsv`). 22 are REAL and lifted from seeds; 4 are UNREAL and certified directly.

| | instances |
|---|---|
| `arbiter` | n = 6, 7, 8, 9, 10 |
| `prioritized_arbiter` | n = 7, 8, 9, 10, 12 |
| `arbiter_with_cancel` | n = 6, 7, 8, 9 |
| `arbiter_with_buffer` | n = 6, 7, 8 |
| `load_balancer` | n = 8, 9 |
| `arbiter_on_inpchange` | n = 5, 6 |
| `amba_decomposed_lock` | n = 15 |
| `round_robin_arbiter_unreal2` (UNREAL) | n = 5, 6, 7 |
| `load_balancer_unreal2` (UNREAL) | n = 6 |

On those 42 previously-unsolved REAL instances the existing Acacia baselines B and S solve **none**
— which is what made them unsolved — `ltlsynt` solves 18, and this path solves 22, with eight
instances only this path solves and four only `ltlsynt` solves. The PAR-2 mean gap of 16.7 s sits
just above the repo's stated 12.1 s noise floor, so **coverage and the disjoint sets are the
evidence, not the aggregate**. On the UNREAL side both cover four instances but the sets are
disjoint and the 3.3 s difference is inside the noise floor; reported as no evidence either way.

## What the previous attempt got wrong

Diagnosed by measurement, not by reading code (`../m4-structure.md`):

1. **It generalized the wrong object.** Not one exported policy guard is local: every guard reads
   every client, at every `n`, in every family measured. The export is one Skolemization with
   arbitrary tie-breaking. The *most-permissive move relation*, which the certificate exports before
   Skolemization, has the same separability arity as the winning region. Generalize the relation and
   re-Skolemize at the target.
2. **It recognized one shape instead of generalizing.** Replaced by per-family templates at a
   measured arity, with semantic bus schemas (`AtMostOne`, `AtLeastOne`, `NoneOf` and `X` variants)
   matched against the bus rather than anti-unified — the emitted form is a balanced tree whose
   shape changes with `n`, so syntactic anti-unification cannot recover it.
3. **It seeded from a degenerate regime.** `round_robin_arbiter_unreal2` has `C(n,2)` pairwise
   conjuncts that do not exist at n=2, so n=2 is a structurally different game — and that is where
   the previous sprint seeded it. `stable_from` is now measured per family and seeds below it are
   refused by name.
4. **It verified by re-translating the whole spec at the target.** Replaced by one-step certificate
   conditions that do not grow that way.

## Milestones

| | outcome |
|---|---|
| M0 census | 36 parametric families, 29 DBA-reducible, 172 unsolved instances |
| M1a | two soundness bugs in the existing GR(1) path, found before building on it |
| M1 | per-conjunct deterministic-Büchi reduction, exact and strict semantics |
| M2 | certificate export: region, goals, rank layers, most-permissive move relation |
| M3 | `tlsfcertcheck` — decides a policy at a target size without solving |
| M4 | index-aware generalizer; 28 targets verified warm, 21 inside budget |
| M5 | environment certificate for UNREAL; `rru2` n=7 closed |
| M6 | cold campaign, both sides, every stage charged |
| M7 | all-n proof attempted, does not close — see below |

## Plan verification criteria, checked

- [x] **M1 rewrites equivalence-checked; M2 agrees with a reference; default `tlsfsolve` output
      byte-identical.** 284/284 suite; export verified against an explicit reference on 240 seeded
      games; default stdout byte-identical confirmed independently at each landing.
- [x] **M3 mutation tests; differential for REAL and UNREAL.** No corruption ever produces a false
      VERIFIED. REAL: 44/44 seeds across four methods. UNREAL: 58/58 agreement with the explicit SCC
      oracle on 120 seeded games, and no environment certificate emitted for the 62 realizable ones.
- [x] **M4 tests: seeds pass/target fails, ambiguous alignment declines, cap and determinism.** 10
      tests including byte-identical determinism across two runs.
- [x] **`arbiter` n=10 still VERIFIED.** Yes, and n=6–9 with it.
- [x] **`rru2` n=7 (was timeout).** UNREALIZABLE in 24 s, environment certificate checks in 18 s.
- [ ] **`round_robin_arbiter` n=10 (was `no_recognized_candidate`).** **Superseded by measurement,
      not achieved.** That family's winning region has separability arity `n` at every `n`, so no
      bounded-arity conjunctive template reaches it at any size. The generalizer declines it by
      name. The cheap escape — that `min_k = n` might indicate an n-ary disjunction expressible by a
      quantifier schema — was tested and refuted (`../m4-structure.md` §4). It needs an invariant
      strengthening search, which is different work.
- [x] **M6 campaign with coverage deltas vs B/S/ltlsynt.** Above.

## Expected coverage versus measured

The plan predicted ~20 reachable families and named `*_enc`, `chomp`/`robot_grid` and `ltl2dba_*` as
likely out of reach. Measured: **10 families** carry the closures. The plan was optimistic, and the
reasons are specific rather than general difficulty:

- `*_enc` out of reach as predicted — `n` is a bit-width, zero local monitors, no index to align.
- `ltl2dba_*` and five others out of reach for a reason the plan did not anticipate: their guarantee
  side contains an implication between recurrence properties, which is **Streett of index > 1** and
  which the single top-level GR(1) split cannot absorb. That is 25 instances blocked by *acceptance
  condition*, not by parameterization — a different failure, and worth keeping separate.
- `round_robin_arbiter`, `lift`, `amba_decomposed_arbiter`: separability arity `n`; 17 instances.
- The `*_unreal1` families, 28 instances and the largest single block, are held back by the monitor
  encoding: `u` is an X-depth and the one-hot encoding spends `2^u` latches on a `u`-bit shift
  register. Briefed, not done.

## M7: a negative result with an address

All three arity-1 families return `UNKNOWN` with zero decisive obligations proved. What *did* work:
every bus summary discharged — 14/14, 8/8, 14/14 — by asserting the negation and getting `unsat`,
over an encoding that partitions each bus into the obligation's own client, one generic other, and
an integer count for the anonymous remainder, with summaries derived from that partition rather than
assumed. That was the place a fake proof would have crept in.

It does not close for a structural reason the tool names: the generalizer retains no parametric AST
for the invariant and rank templates, the transition and justice predicates are compiled from a
concrete target AAG, and the policy is Skolemized only after target-size move relations exist. Every
term the prover can read is an observation at one `n`, and treating a concrete BDD as an all-n lemma
would be unsound. **Tier 2 needs the generalizer to keep parametric terms through instantiation, not
a better SMT encoding.**

## Corrections made to this sprint's own evidence

Both were caught by checking rather than by review, and both are recorded rather than quietly fixed:

- **Four of M4's five original demonstration targets were instances that already solved at 120 s**,
  and one was not a benchmark instance at all. The measured result at that point was one closed
  instance, not five. Every target since is drawn from the unsolved set.
- **`min_k` in `../m4-invariant-separability.tsv` measures less than it appears to.** Its projection
  keeps every shared variable, so for a shared-heavy family it discards almost nothing and `min_k=1`
  becomes close to vacuous — `simple_arbiter_with_hints` at 64% shared reports `min_k=1` and the
  generalizer correctly declines it. The table now carries `shared_pct` and `dropped_by_k1` so the
  number cannot be over-read.

## Reproducibility caveat on the submodule pin

The committed `subprojects/tlsf-tools` pin is `f213093`, the merge of PR #33. The M5 environment
certificate work is **not** in that pin — it lives on `param-lift-m5` (PR #35) and was merged
upstream only after these numbers were taken. So the REAL half of the campaign reproduces at the
committed pin, but the four UNREAL closures do not: they need the M5 binaries.

Bump the pin again once #35 merges, and re-run `m6-campaign.tsv` against it before quoting the
UNREAL rows as reproducible from a clean checkout. The pin was deliberately left where it is rather
than moved to an unmerged branch commit.

## What a future session should pick up

1. **The monitor encoding** (`followup-monitor-encoding.md`; 28 instances). Binary-encode
   monitor states; `u` is a shift register, not an arbitrary automaton.
2. **Invariant strengthening** for the `min_k = n` families (17 instances). z3 5.1.0 is installed at
   `build_scratch/smt`. The conjunctive and the cheap existential routes are both closed by
   measurement, so this needs a real search.
3. **Parametric terms through instantiation**, which is the single change that would move M7 from
   `UNKNOWN` toward `PROVED`.
4. **`tlsf-tools` issue #34** — the game-profile resolver rejects models combining bad state
   properties with justice properties, which AIGER 1.9 permits. Our reduction works around it by
   emitting the safety condition as a plain output.
5. **The closed-loop route's monolithic transition relation**, which is why a target-size result has
   one sound proof and no independent second opinion.
