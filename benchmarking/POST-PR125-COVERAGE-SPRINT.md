# Post-PR-125 coverage sprint

Six ranked work packages against the 434 instances of the official SYNTCOMP 2026
LTL-realizability selection that PR #125's portfolio cannot decide at 17 s.

## Decision

| package | correct? | target gains | full-2026 gains | regressions | memory effect | decision |
|---|---:|---:|---:|---:|---:|---|
| P1 forced contradiction | | | | | | *in progress* |
| P2 semantic dominance D1 | | | | | | *not started* |
| P2 semantic dominance D2 | | | | | | *not started* |
| P3 K schedule | | | | | | *not started* |
| P4 OTFUR lazy | | | | | | *not started* |
| P4 OTFUR memory | | | | | | *not started* |
| P4 OTFUR covering | | | | | | *not started* |
| P5 four-slot portfolio | | | | | | *not started* |
| P6 wide6 portfolio | | | | | | *not started* |

## Frozen baseline

```
PR #125    head 521b9400569489be2393b9a114f9e22940463e12
           base 2178290bac7d0eb77350593c69a5fa4e723f0cbf
submodules posets              139e14336b7a1f0bc064022e587ea4e1b9a81427
           tlsf-tools          b42d5ef4a680252e04820ac7f073f5d786a43f7c
           syntcomp-benchmarks 4105caf1f1e5fd3b76657879bfce8021d130cbde  (v2026)
```

Reference configurations, never silently changed:

```
B = best_decomp_rank_bucketed_semantic_mona
S = best_decomp_rank_bucketed_semantic_mona_local
F = best_decomp_rank_bucketed_semantic_mona_forward
```

Coverage established by PR #125 over `tests/suites/benchmarks/syntcomp26/all.list`
(1,524 logical instances, 17 s cap):

| configuration | decided |
|---|---:|
| B | 1056 |
| S | 1065 |
| F | 1053 |
| B ∪ S | 1065 |
| B ∪ S ∪ F | **1090** |

The TLSF corpus is materialized once and reused by every gate:

```sh
python3 benchmarking/syntcomp-corpus.py init
python3 benchmarking/syntcomp-corpus.py materialize --out /tmp/acacia-syntcomp26-tlsf
export ACACIA_TLSF_CORPUS=/tmp/acacia-syntcomp26-tlsf
```

Verified for this sprint: **1,586 files**, the required count.

## P1 — forced-output contradiction checker

### Why this package is worth doing

The two target families hold 52 of the 1,524 instances. Their measured status at a
17 s cap, from `benchmarking/_coverage26/{B,S,F26}-runs.tsv`:

| configuration | UNREALIZABLE | TIMEOUT |
|---|---:|---:|
| B | 2 | 22 |
| S | 1 | 22 |
| F | 30 | 22 |
| union | 30 | **22** |

The same 22 instances defeat all three solvers, and 20 of them still time out at
60 s. They are `full_arbiter_unreal1` at the larger delays (`n=2, u=18..26`;
`n=3, u=8..15`; `n=4, u=5..8`; `n=6, u=8`) and `full_arbiter_unreal2` at `n=5,6,7`.
Every one is adjudicated `unrealizable`.

P1 decides at the formula level, before translation, so its cost does not grow with
the delay `u` that defeats the game solvers. Its ceiling on this sprint's primary
metric is therefore **+22, taking the union from 1,090 to 1,112** — the largest
single gain available in the six packages, and the cheapest to obtain.

### Twelve of those 22 have no known answer

The SYNTCOMP `//STATUS` annotations across the 52 family members are 40
`unrealizable` and 22 `unknown`, and the split is not incidental: of the 22
instances that defeat B, S and F, **10 are annotated `unrealizable` and 12 are
annotated `unknown`**. No tool in the competition ever decided those twelve.

`run-syntcomp26-coverage.py:190` maps an `unknown` annotation to no expectation, so
a verdict there cannot register as a conflict — but it equally means there is no
oracle to check it against. That raises rather than lowers the soundness burden, and
it is the reason the restricted fragment of §3.3 must not be widened to make more
instances match. The validation surface for P1 is:

| cohort | count | check available |
|---|---:|---|
| hard, annotated `unrealizable` | 10 | direct comparison against the annotation |
| already decided by F at 17 s | 95 | independent solver agreement |
| hard, annotated `unknown` | 12 | none — rests on the fragment being sound |

The 95 come from `full_arbiter_unreal` (30) and `simple_arbiter_unreal` (65).

### A family the handoff does not mention

`simple_arbiter_unreal1` (53 instances) and `simple_arbiter_unreal2` (12) are also
assumption-free, and their `MAIN` block is the `full_arbiter_unreal` template minus
the "no spurious grants" invariants — mutual exclusion plus the same two-grant
clause. They match the same fragment.

They add **no coverage**: F already decides all 65 at 17 s. Their value is as the
largest independent-agreement cohort available for the correctness gate, and as a
speed result, since P1 answers them before translation.

More broadly, 796 of the 1,586 materialized files carry no `ASSUMPTIONS`, `ASSUME`
or `REQUIRE` section. That is the population the sound fragment can even consider;
the remaining 790 must be declined on sight.

### Prediction for the scan, recorded before the matcher exists

Of the 796 assumption-free files, 208 carry a `mutual_exclusion` invariant, but only
**127** also carry the two-grant clause `g[i] && g[j]` that contradicts it:

| family | files | contradiction clause |
|---|---:|---|
| `full_arbiter_unreal1` | 55 | yes |
| `simple_arbiter_unreal1` | 53 | yes |
| `simple_arbiter_unreal2` | 12 | yes |
| `full_arbiter_unreal2` | 7 | yes |
| `rw_arbiter`, `arbiter_with_cancel`, `arbiter_with_buffer`, `arbiter_on_inpchange`, `arbiter`, `abcg_arbiter`, `robot_grid`, `simple_arbiter_with_hints`, `generalized_buffer` | 81 | **no** |

So the matcher should report **127 matches over the 1,586 materialized files and
nothing else**. Restricted to the TLSF files the graded 1,524-instance selection
actually uses, the prediction is **117**: 46 `full_arbiter_unreal1`, 6
`full_arbiter_unreal2`, 53 `simple_arbiter_unreal1`, 12 `simple_arbiter_unreal2`.
Of those 117, **95 are already decided** by an existing configuration and are
therefore agreement checks, and **22 are new coverage**.

Those 81 non-matching arbiters are the sharpest false-match test available: they
carry the same global mutual-exclusion invariant over the same output partition and
differ only in having no clause that contradicts it. A matcher that fires on them
has a bug in the `chi & beta` unsatisfiability test rather than in conjunct
extraction. A count above 127 is a false match to investigate; a count below 127 is
a missing normalization.

### What the effective formula actually looks like

`full_arbiter_unreal1_pb_2_2_pe_` as the native TLSF frontend emits it, **before**
`spot::realizability_simplifier` (367 bytes). The post-simplifier text the checker
actually receives is shorter, and is recorded below once the fixture dump exists:

```
G ( (g_0 && G !X r_0 -> F !g_0)
 && (g_0 && X (!X r_0 && !g_0) -> X (X r_0 R !g_0))
 && ((g_1 && G !X r_1 -> F !g_1) && (g_1 && X (!X r_1 && !g_1) -> X (X r_1 R !g_1)))
 && (!g_0 && true || true && !g_1)
 && ((X r_0 && X X r_1 -> X X (g_0 && g_1)) && true) )
&& (X r_0 R !g_0 && G (X r_0 -> F g_0) && (X r_1 R !g_1 && G (X r_1 -> F g_1)))
```

Five properties of this text decide the implementation:

1. **The top level is a conjunction, with no outer implication.** These families
   carry no `ASSUMPTIONS` section, so the sound fragment applies. This is exactly why
   `round_robin_arbiter_unreal1` and `load_balancer_unreal1` are excluded: both *do*
   have an `ASSUMPTIONS` section, so their clauses sit under `A -> G` and are not
   consequences of the whole formula. They are negative fixtures, not targets.
2. **The entire `INVARIANTS` block arrives as one `G(...)` over a conjunction.** The
   rewrite `G(A & B) → G A, G B` is therefore load-bearing, not a convenience:
   without it nothing matches.
3. **The mutual-exclusion invariant is an OR**, `(!g_0 && true || true && !g_1)`.
   The "never descend under arbitrary OR" restriction governs *conjunct extraction*;
   it does not forbid an invariant whose `chi` is itself a disjunction. `chi` is
   accepted here because it is temporal-operator-free with support inside the output
   partition, and it goes to a BDD as-is.
4. **Inputs are shifted by one `X`** — `X r_0`, not `r_0` — because the family is
   `SEMANTICS: Moore, TARGET: Mealy`. The trigger is `X r_0 && X X r_1`, so the
   bounded-input-pattern translator over `(input AP, time offset)` pairs is required
   for the very first target, not just for exotic cases.
5. **Trivial `true` conjuncts survive simplification** and must be tolerated.

The match is then `chi = !g_0 || !g_1`, `beta = g_0 && g_1`, `chi & beta = bddfalse`,
trigger satisfiable, `d = 2` — `UNREALIZABLE`.

`full_arbiter_unreal2` is the eventual variant: the same shape with `F (g_0 && g_1)`
in place of `X X (g_0 && g_1)`. Both response kinds of §3.2 are needed.

### A crash the unit test was hiding

The first working implementation passed all 28 unit tests and would have segfaulted
on every instance in production.

`bounded_input_pattern` and the output translator call `bdd_extvarnum` and
`bdd_ithvar` directly, and those require BuDDy to have been initialized. At the
call site the checker is designed for — `try_syntactic_bypass` — it has not been:
`solver_invoker.cc` does not create `spot::make_bdd_dict ()` until well after the
bypass returns. A standalone program that parses a formula and calls `try_direct`
with no dictionary in existence dies with SIGSEGV; adding one `make_bdd_dict ()`
before the call makes the same program print `unrealizable=1` and exit 0.

The unit test did not catch it because its `main` opened with
`auto dictionary = spot::make_bdd_dict ();`, initializing BuDDy process-wide and
reproducing a precondition the solver never establishes. The test was green because
it was testing a situation that does not occur.

The fix makes both public entry points hold a `spot::bdd_dict_ptr` for the duration
of the call, and **removes** the dictionary from the test, so the test now exercises
the same uninitialized state as the real call site and fails if the guard is ever
removed.

This is worth recording because it is a general hazard for this sprint rather than a
one-off: P1, and later any BDD work in P2's dominance helper, runs earlier in the
pipeline than the solver's own BDD setup. A green unit suite is not evidence that
code works where it is actually called.

### The prediction was wrong, and the reason is instructive

The 117 estimate was derived from the TLSF sources. It does not survive contact with
the effective formulas, because `spot::realizability_simplifier` runs first and
changes the *shape* of half the match set.

`simple_arbiter_unreal1_pb_2_1_pe_` reduces to

```
G((!g_0 | !g_1) & X(g_0 & g_1)) & GFg_0 & GFg_1
```

with **no implication anywhere**. Its request APs are single-polarity, so the
simplifier substituted them true and collapsed `(r_0 & X r_1) -> X(g_0 & g_1)` into
an unconditional `X(g_0 & g_1)`. `simple_arbiter_unreal2_pb_4_pe_` collapses the
same way into `G(chi & F(g_0 & g_1) & F(g_0 & g_2) & ...)`. A matcher that requires
`G(alpha -> ...)` declines both, and the n=16 member declines in 2.6 ms.

The families that keep their implications are exactly the ones that need them:
`full_arbiter_unreal1_pb_3_15_pe_` still carries 12, because its requests appear at
both polarities in the no-spurious-grant clauses, so the simplifier cannot force
them. **All 22 coverage-gaining instances are in that group and match today.** What
the collapse costs is the 65 `simple_arbiter_unreal` instances, which F already
decides — no coverage, but the bulk of the independent-agreement cohort.

The extension is to treat a missing implication as a vacuously true trigger, which
is sound in both forms:

- `G(chi) & G(X^d beta)` forces `beta` at every position from `d` on, and `chi` at
  every position, so `chi & beta` unsatisfiable is a contradiction;
- `G(chi) & G(F beta)` forces `beta` at some position, where `chi` also holds.

It cannot create a false match on the 81 innocent arbiters: with an unconditional
obligation, `beta` is drawn from the same conjuncts that make up `chi_all`, so
`chi_all & beta` reduces to `chi_all`, which is satisfiable unless the specification
is itself contradictory — and a contradictory specification is unrealizable anyway.

The order of work is therefore: scan with the implication-only matcher and record
what it really matches, extend, then scan again and account for every new match.

### Probing the soundness boundary

The handoff's restriction list (§3.3) says what not to descend under. Whether the
implementation honours it was checked directly, with cases chosen so that a matcher
one level too permissive would answer `UNREALIZABLE` on each. All ten behave:

| case | required | result |
|---|---|---|
| response under `G`, trigger input-only | match | match |
| `beta` propositionally false, no invariant at all | match | match |
| response only under a disjunction | decline | decline |
| response under an inner implication | decline | decline |
| invariant only under a disjunction | decline | decline |
| response negated | decline | decline |
| invariant under `F G` rather than `G` | decline | decline |
| trigger mentions an output | decline | decline |
| realizable lookalike, `chi & beta` satisfiable | decline | decline |
| response inside `U` | decline | decline |

Two are worth singling out. The `beta`-false case matches with **no invariant
present**: `G(alpha -> false)` is `G(!alpha)`, and a forceable `alpha` refutes it, so
the empty-invariant path is sound rather than an oversight. And the realizable
lookalike, `G(!g0|!g1) & G(r0 -> XX g0)`, is the shape the checker must never fire
on — a single grant violates nothing — which is what the `chi & beta` test rejects.

These are now part of the unit suite rather than a one-off script.

### Scan 1: the implication-only matcher, over all 1,524

`benchmarking/forced-contradiction-scan.py` against
`acacia-forced-contradiction-scan`, one process per file, about a minute:

```
scanned 1524 files
  DECLINE  1471
  MATCH      53
matches by family:
  full_arbiter_unreal1         46
  full_arbiter_unreal2          6
  jarvis_gideon_a02758ea        1
```

No crashes, errors, timeouts or empty verdicts. Both target families match in full,
46 and 6, exactly the counts predicted from the sources. The 65
`simple_arbiter_unreal` instances decline, for the simplifier-collapse reason above.

**No match contradicts an annotation.** The 53 are 41 annotated `unrealizable` and
12 annotated `unknown`; none is annotated `realizable`. The 12 are precisely the
hard `full_arbiter_unreal` instances no competition tool decided, and among them
`full_arbiter_unreal1_pb_3_15_pe_` is answered with `MATCH fixed_delay 15`, the
delay agreeing with its own `u = 15` parameter. That instance times out for B and S
at 1 s, 5 s, 17 s **and** 60 s, and for F at 17 s, which is the only cap F was run
at. The checker settles it before translation.

### The unpredicted match was real, and found a second theorem

`jarvis_gideon_a02758ea` was not in the prediction. It is annotated `unrealizable`,
so the answer is right, but the *reason* reported was not. Reconstructing the
invariant half of the checker over that instance shows it collects **26** output-only
global invariants whose conjunction is already `bddfalse` — the specification demands
`G u0window0f1dopen1b` and `G u0window0f1dclose1b` among others. With `chi_all`
unsatisfiable, the first response encountered matches for a reason unrelated to that
response, and the witness names it anyway.

The answer is sound: `G chi` with `chi` propositionally unsatisfiable makes the whole
specification unsatisfiable, hence unrealizable. But it is a *different* theorem from
the one the checker was written for, and it deserves to be reported as itself. It is
also strictly more general, since it does not need a response-shaped conjunct to be
present at all — the current code can only reach it by accident, when some response
happens to exist.

Both findings are folded in: contradictory invariants become their own witness kind,
checked before any response is considered, and an absent implication is treated as a
vacuously true trigger.

### Scan 2: after both generalizations

```
scanned 1524 files
  DECLINE 1404
  MATCH    120
matches by family:
  full_arbiter_unreal1         46      simple_arbiter_unreal1       53
  full_arbiter_unreal2          6      simple_arbiter_unreal2       12
  jarvis_gideon_a02758ea        1      lilydemo01                    1
                                       lilydemo02                    1
witness kinds:  fixed_delay 101   eventual 18   contradictory_invariants 1
```

Nothing that matched in scan 1 stopped matching. Every newly matched instance is
accounted for: 65 are the `simple_arbiter_unreal` families recovered by the
vacuous-trigger reading, and the two `lilydemo` instances reduce to
`G(... & X(0))` — an obligation whose `beta` is propositionally false, caught by the
path the adversarial probe had already covered in the abstract. Both are annotated
`unrealizable`.

### Correctness of the 120

| check | result |
|---|---|
| matches annotated `realizable` | **0** |
| matches annotated `unrealizable` | 106 |
| matches annotated `unknown` | 14 |
| matches carrying a B/S/F verdict at any cap | 100 |
| of those, agreeing `UNREALIZABLE` | **100** |
| of those, conflicting | **0** |

A hundred independent solver agreements and no contradiction anywhere in the
official selection is the strongest evidence available short of the gates
themselves.

### Coverage

| | count |
|---|---:|
| matched by the checker | 120 |
| already decided by B, S or F at 17 s | 98 |
| **new coverage** | **22** |

The 22 are 19 `full_arbiter_unreal1` and 3 `full_arbiter_unreal2` — exactly the set
identified before the code existed. They take the portfolio union from **1,090 to
1,112 of 1,524**. The other 98 matches gain nothing but are answered before
translation rather than by a game search, and they are what makes the correctness
argument above possible.

### Gates

*Pending — filled in as each gate completes.*

| gate | result | nature of failure |
|---|---|---|
| G0 unit/posets | | |
| G1 frozen 40 | | |
| G2s per-target cycles | | |
| G3 syntcomp25 + syntcomp26 panels | | |
| G4 correctness corpus | | |
| G26-full | | |
| scan, 1,586 TLSF files | | |

### Decision

*Pending.*
