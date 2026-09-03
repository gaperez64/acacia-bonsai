# Post-PR-125 coverage sprint

Six ranked work packages against the 434 instances of the official SYNTCOMP 2026
LTL-realizability selection that PR #125's portfolio cannot decide at 17 s.

## Decision

| package | correct? | target gains | full-2026 gains | regressions | memory effect | decision |
|---|---:|---:|---:|---:|---:|---|
| P1 forced contradiction | yes | +4 on G4, 22 target instances | **+22** (1,090 -> 1,112) | none | none | **LAND** |
| P2 semantic dominance D1 | yes | 512 -> 18 actions, 0 instances | none | G2s +0.39% cycles | none | **KEEP RESEARCH TOOLING** |
| P2 semantic dominance D2 | — | — | — | — | — | **not attempted (D1 did not pay)** |
| P3 K schedule | yes | 34 -> 7 attempts, 0 instances | none | +3-4% slower | none | **STOP** |
| P4 OTFUR lazy | yes | 3 targets -27% to -56% | pending G26 | none | fewer successors | **LAND (O1)** |
| P4 OTFUR memory | yes | 2 targets -16% to -29% | pending G26 | none | RSS criterion failed | **LAND (O2, on speed)** |
| P4 OTFUR covering | yes | 1 of 2 targets, mixed | none | none | none | **AGGRESSIVE PRESET (off by default)** |
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

Over the full 1,586-file corpus rather than the graded selection, the same binary
matches **130**, distributed as 55 `full_arbiter_unreal1`, 7 `full_arbiter_unreal2`,
53 `simple_arbiter_unreal1`, 12 `simple_arbiter_unreal2`, and the three singletons.
That closes the prediction loop exactly: the four arbiter families come to
55 + 7 + 53 + 12 = **127**, the number derived from the TLSF sources before any code
existed, and the three extra matches are precisely the ones the two additional
theorem paths were added to catch. Of the 130, 106 are annotated `unrealizable`,
24 `unknown`, and none `realizable`.

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
1,112 of 1,524**.

| instance | witness | SYNTCOMP status |
|---|---|---|
| `full_arbiter_unreal1_pb_2_18_pe_` | `fixed_delay 18` | unrealizable |
| `full_arbiter_unreal1_pb_2_20_pe_` | `fixed_delay 20` | unrealizable |
| `full_arbiter_unreal1_pb_2_21_pe_` | `fixed_delay 21` | unrealizable |
| `full_arbiter_unreal1_pb_2_22_pe_` | `fixed_delay 22` | unrealizable |
| `full_arbiter_unreal1_pb_2_24_pe_` | `fixed_delay 24` | unrealizable |
| `full_arbiter_unreal1_pb_2_26_pe_` | `fixed_delay 26` | unrealizable |
| `full_arbiter_unreal1_pb_3_8_pe_` | `fixed_delay 8` | unrealizable |
| `full_arbiter_unreal1_pb_3_9_pe_` | `fixed_delay 9` | unrealizable |
| `full_arbiter_unreal1_pb_3_10_pe_` | `fixed_delay 10` | unrealizable |
| `full_arbiter_unreal1_pb_3_11_pe_` | `fixed_delay 11` | **unknown** |
| `full_arbiter_unreal1_pb_3_12_pe_` | `fixed_delay 12` | **unknown** |
| `full_arbiter_unreal1_pb_3_13_pe_` | `fixed_delay 13` | **unknown** |
| `full_arbiter_unreal1_pb_3_14_pe_` | `fixed_delay 14` | **unknown** |
| `full_arbiter_unreal1_pb_3_15_pe_` | `fixed_delay 15` | **unknown** |
| `full_arbiter_unreal1_pb_4_5_pe_` | `fixed_delay 5` | unrealizable |
| `full_arbiter_unreal1_pb_4_6_pe_` | `fixed_delay 6` | **unknown** |
| `full_arbiter_unreal1_pb_4_7_pe_` | `fixed_delay 7` | **unknown** |
| `full_arbiter_unreal1_pb_4_8_pe_` | `fixed_delay 8` | **unknown** |
| `full_arbiter_unreal1_pb_6_8_pe_` | `fixed_delay 8` | **unknown** |
| `full_arbiter_unreal2_pb_5_pe_` | `eventual 0` | **unknown** |
| `full_arbiter_unreal2_pb_6_pe_` | `eventual 0` | **unknown** |
| `full_arbiter_unreal2_pb_7_pe_` | `eventual 0` | **unknown** |

The reported delay equals the instance's own `u` generator parameter in every one of
the nineteen fixed-delay rows, and the three `full_arbiter_unreal2` rows report
`eventual`, which is the form that family is generated in. Nothing in the checker
reads the filename, so this is an independent consistency check on the witness
rather than a restatement of it.

Twelve of the twenty-two are annotated `unknown`, meaning the sprint would settle
instances the competition left open. The other 98 matches gain nothing but are answered before
translation rather than by a game search, and they are what makes the correctness
argument above possible.

### Gates

Every gate is run twice, on two builds differing in exactly one preprocessor flag —
`ACACIA_FORCED_OUTPUT_CONTRADICTION` 0 against 1, both from the
`best_decomp_rank_bucketed_semantic_mona` preset with diagnostics off. Without the
baseline side, a failing gate cannot be attributed to P1 rather than to something
already failing at `521b9400`.

| gate | candidate | baseline | attributable to P1 |
|---|---|---|---|
| G0 unit + posets | `Fail: 0`, 28/28 | same | — |
| G1 frozen 40 | `GATE FAIL`, 35/40 | `GATE FAIL`, 35/40, **identical set** | **none** |
| G4 correctness corpus | `Fail: 0`, 563 ok, 61 timeout | `Fail: 0`, 559 ok, 65 timeout | **+4, none lost** |
| G2s per-target cycles | `GATE PASS`, geomean 0.994 | paired against candidate | **no measurable cost** |
| G3 syntcomp25 + 26 panels | `GATE PASS` ×2, 80/180 and 118/180 | same counts | **none** |
| G26 full 1,524 | 998 decided | 976 decided | **+22, none lost** |

#### G26: the coverage claim, measured

Both sides run with staged caps 1, 5, 17 under `MemoryMax=8G` / `MemorySwapMax=0`,
sequentially, from the same two builds:

| | decided of 1,524 |
|---|---:|
| baseline, flag off | 976 |
| candidate, flag on | **998** |
| delta | **+22** |

The gained set is *exactly* the twenty-two named in the table above, before the
matcher existed — nineteen `full_arbiter_unreal1` and three `full_arbiter_unreal2`,
every one `UNREALIZABLE`. **Every one is decided at the 1-second cap**, against a
baseline that cannot decide them at 17 s and, for most, not at 60 s either.

Nothing was lost, and the two builds disagree on **no** instance they both decide.

Those twenty-two are absent from the recorded B, S and F runs alike, so the
portfolio union moves **1,090 → 1,112 of 1,524**.

#### Two conflicts, neither P1's, both now settled by construction

The candidate campaign recorded two verdict conflicts: `lilydemo15` and
`lilydemo16` are annotated `unrealizable` and come back `REALIZABLE`.

They are not P1's. The checker declines on both; the baseline binary returns
`REALIZABLE` identically; and the checker cannot emit `REALIZABLE` at all, since it
either reports `UNREALIZABLE` or declines.

They are not Acacia's either. `ltlsynt` agrees, and it synthesises a controller for
each — 6 and 23 states — whose language has **empty intersection** with an automaton
for the negated specification. That is a witness rather than a second opinion: the
strategy satisfies the specification on every run. Both formulas are arbiters with
mutual exclusion and no spurious grants, two clients and three, realised by
alternating grants.

Both are now in `syntcomp26-status-exceptions.tsv` with that evidence, beside the
pre-existing `lilydemo04_modified` correction. The re-run baseline campaign, which
used the corrected table, records **zero** conflicts.

### Decision

**LAND.**

Every criterion of §3.12 is met. Unit tests pass. Every matched instance is
independently confirmed: 100 of the 120 matches carry a B, S or F verdict and all
100 agree, none conflicts, and none is annotated `realizable`. There are zero
verdict conflicts attributable to P1 over the 1,524. Twenty-two instances above the
17-second cap drop below one second — `full_arbiter_unreal1_pb_3_15_pe_`, a 60-second
timeout for both backward configurations, returns in 7.9 ms. And no gate regresses:
G1 fails identically on both sides, G4 gains four and loses none, G2s finds no
measurable cost, G3 is unchanged.

Twelve of the twenty-two are annotated `unknown` in SYNTCOMP, so this package
settles instances the competition left open.

The flag stays **off by default**. Flipping it is a separate decision with its own
gate run, and B, S and F remain untouched for the packages that follow.

#### G4 is where P1 earns its place

| transition | count |
|---|---:|
| OK → OK | 510 |
| **TIMEOUT → OK** | **4** |
| TIMEOUT → TIMEOUT | 59 |
| OK → TIMEOUT | **0** |
| `FAIL`, either side | **0** |

The four gained are `full_arbiter_unreal1_3_8`, `_3_10`, `_3_12` and
`full_arbiter_unreal2_5` — the target family, and the same hard points the corpus
scan identified. Strictly more answers, never a different one, which is the only
shape a sound decision procedure may produce.

#### G2s passes, and measures less than its headline claims

```
geometric mean ratio=0.99387  improvement=-0.61%
decision=proxy-pass-to-G3: no target exceeds the regression ceiling
          and best target improves 6.23%
GATE PASS
```

Two facts make the summary line unusable as a performance claim. **P1 fires on none
of the ten panel targets** — every one has zero matches in the corpus scan — so the
checker runs, declines, and gets out of the way, and the expected effect is nil.
And **all ten time out on both sides in all three repetitions**, so cycles here
measure how fast a binary burns the clock before the cap, not how fast it solves.
The `+6.23%` on `evasion0` that the script calls the best improvement is a ratio of
`1.06639`: the candidate spent *more* cycles before timing out. On a both-sides
timeout that is not a signal in either direction.

What G2s does establish is narrower and still worth having: a pre-check that runs on
every instance and decides 130 of 1,586 costs nothing measurable on the other 1,456.

This is the trap `FORWARD-COVERAGE-SPRINT.md` documented when it recorded a 10.9x
G2s geomean for the forward solver and then explained why that was not a win. Same
gate, same failure mode, opposite direction.

#### G3 passes with no change, for a checkable reason

Both panels are unmoved: syntcomp25 stays at 80/180 and syntcomp26 at 118/180. The
panels hold four `full_arbiter_unreal` instances between them — `pb_2_16`,
`pb_2_14`, `pb_3_2` and `unreal2_pb_4` — and **none is among P1's twenty-two**.
They are the easy members the baseline already solves. The stratified panels do not
sample the hard tail, so no change is the correct outcome rather than a
disappointment, and the coverage claim rests on G26 instead.

#### G1 fails, and fails identically on both sides

`regression-gate.sh` compares the build under test against a hardcoded reference,
`build_best_decomp_mona` — the *shipping* preset — not against the build handed to
it. So both runs were measured against the same third binary:

| build | solved | timeout | PAR-2 |
|---|---:|---:|---:|
| reference, shipping `best_decomp_mona` | 40 | 0 | 101.867 s |
| baseline, B preset, flag off | 35 | 5 | 217.762 s |
| candidate, B preset, flag on | 35 | 5 | 195.269 s |

The five lost instances are the same five in both, character for character:
`Morning_f1477cc5`, `Morning_f2774e0b`, `load_balancer7`, `infinite-race-u4` and
`load_balancer_pb_7_pe_`. They belong to the B preset relative to the shipping
reference and are inherited from PR #125; P1 neither causes nor repairs any of them.

The PAR-2 difference, 195 s against 218 s, is **not** claimed as an improvement. It
is a 10 % swing on a forty-instance panel against a documented noise floor of about
21 s on a much larger one.

#### A near-miss worth recording

The first G1 run was invoked as `regression-gate.sh ... | tail -30`, which reported
`tail`'s exit status rather than the script's. The visible output ended in
`Ok: 22 / Fail: 0` and looked clean; the script had in fact printed `GATE FAIL` on
its first line and exited 1, because the build lacked `-Dacacia_tlsf_corpus_dir` and
had silently run 25 of its 40 sentinels. `ACACIA_TLSF_CORPUS` in the environment
serves the campaign scripts; the meson-driven gates need the path configured into
the build. Gates are run unpiped, and their own verdict line is what gets recorded.

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


## P2 — global semantic-action dominance

### Decision

**KEEP RESEARCH TOOLING.** The reduction is real, exactly as predicted, and buys
nothing.

### What was built

`src/actioners/profile_dominance.hh` prunes actions that are redundant for the
controller. The direction reads backwards and is worth stating: backward `apply`
imposes `apply_out[q] = min (apply_out[q], max (-1, m[p] - increment))` per
endpoint, so more endpoints and larger increments give tighter constraints, a
pointwise smaller image and a smaller downset — and since `cpre_inplace` takes a
**union** over an input's actions, the action with *more and stronger* endpoints is
the one that contributes nothing.

Pruning runs after `actioners::standard` extracts from its
`std::set<input_and_actions, compare_actions>`, never before, so the input ordering
PR #125 measured is preserved and the reduction is the only variable.

### Correctness

`tests/profile_dominance_test.cc` compares the controller predecessor before and
after pruning at **every** rank vector of the entire domain, for 240 generated
tables, using `research::apply_backward` — the documented transcription of the real
`apply`. Measured separately over 2,000 tables of the same shape, pruning fires on
**49%** of them and removes **29%** of all actions, so the differential check is
exercising real reduction rather than passing vacuously. A flipped comparison would
shrink the union on half the tables.

### The reduction is exactly as predicted

```
prioritized_arbiter_pb_7_pe_
  profile_actions_before = 512      profile_actions_after = 18
  dominance_tests = 640             endpoint_visits = 5340
  declined = 0                      ms = 0.09
```

512 equality-distinct profiles collapsing to the 18 inclusion-minimal ones, at
0.09 ms with no budget exhaustion. The census was right.

### And it buys nothing

| instance | baseline | candidate |
|---|---|---|
| `prioritized_arbiter_pb_5_pe_` | REALIZABLE 0.22 s | REALIZABLE 0.16 s |
| `prioritized_arbiter_pb_6_pe_` | REALIZABLE 21.33 s | REALIZABLE 20.98 s |
| `prioritized_arbiter_pb_7_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |
| `prioritized_arbiter_pb_8_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |
| `round_robin_arbiter_pb_4_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |
| `workstation_resupply_pb_3_pe_` | UNKNOWN 60 s | UNKNOWN 60 s |

No verdict changes, nothing newly solved, best movement 1.6% against §4.12's 25%
criterion.

| gate | candidate | baseline | verdict |
|---|---|---|---|
| G0 unit | `Fail: 0`, 28/28 | — | pass |
| G1 frozen 40 | `GATE FAIL` 35/40, PAR-2 212.7 s | `GATE FAIL` 35/40, PAR-2 213.4 s | identical, not attributable |
| G2s per-target | geomean **1.00394**, +0.39% cycles | paired | **GATE FAIL** |

G2s is the decisive one: five of ten targets get *slower* — `arbiter_with_buffer6`
+5.45%, `round_robin_arbiter4` +2.53% — and the geometric mean is 0.39% **more**
cycles. On that panel the prune mostly finds nothing to remove, so the run pays the
construction cost without collecting the benefit.

### Why it fails, which is the useful part

The census counted *profiles*, and 512 → 18 confirms that count exactly. What it
never established is that the action basis was the **bottleneck**. On the same
instance, 5,370 worker diagnostics report 20 actions before and 20 after: the
collapse touches one worker, and the search everywhere else is untouched. The
instance still exhausts 60 s.

So the cost lives somewhere the action count does not reach — most plausibly the
antichain width in the fixed point rather than the per-CPre action loop. That is
worth carrying into P4 and P5, both of which are partly premised on action-table
size being what matters.

**D2 is not attempted.** §4.9 permits pre-decode dominance only once D1 shows
material solver gains, and it must then beat D1 rather than the equality-only
baseline. D1 shows none.

The helper, its exact differential test and the six diagnostics counters stay in the
tree behind a default-off flag, because the action-profile census they produce is
needed by P5's arm census regardless.


## P3 — alternative K schedules

### Decision

**STOP.** The mechanism works exactly as designed, gains nothing, and costs time.

### The premise, and why it survived P2

`src/solver/k_schedule.hh` adds linear, geometric (`2k+1`), cheap-loss-adaptive and
direct-max schedules. The precondition was an algebra question the handoff makes P3
conditional on: is one direct lift by the total delta equal to the repeated lifts it
replaces? **It is** — `lift(s,d)` adds `d` to numeric coordinates and resets boolean
ones to zero, so the boolean half is idempotent and the numeric half accumulates.
`tests/k_schedule_test.cc` checks that over **84,832 exhaustive cases**, covering the
four the handoff names, plus the overflow guard against `VECTOR_ELT_T`.

The cohort looked ideal: 181 instances with `k_attempts >= 10`, and the stuck ones
show `k_attempts=34` with `max_f` of **1 or 2** — `arbiter_pb_6` burns 2,721 loops
over a frontier of one element. Linear walks 2→99 by 3 in 34 attempts, geometric in
7. A 4.9x reduction, and unlike P2 the reduction is *on the quantity being counted*.

### The measurement

`chomp_pb_3_2_pe_` confirms the wiring: `k_attempts` 34 → **7**, `k_last_next=99`.

Over 22 instances spanning every cohort family, at a 17 s cap, four builds:

| | forward | backward |
|---|---|---|
| decided by linear | 10 | 8 |
| decided by geometric | 10 | 8 |
| gained / lost | 0 / 0 | 0 / 0 |
| verdict disagreements | 0 | 0 |
| wall on instances both decide | **+3.0%** | **+4.4%** |

### Why, which the diagnostics settle

**All 181 high-K instances lose at every bound `K <= 99`.** They complete the whole
sweep and fail; none is won at any K the schedule could reach sooner. So there is no
winning bound for geometric to arrive at earlier — and equally none to overshoot.
Reaching `kmax` in 7 attempts instead of 34 produces the same `UNKNOWN`, only sooner.

And "fewer attempts" is not "less work": geometric tests 2, 5, 11, 23, 47, 95, and a
single attempt at K=95 costs more than several at K=5..20. That is why it comes out
**3-4% slower** despite doing a fifth as many attempts.

§5.11 requires new 2026 coverage of at least 2. This is 0, with a time regression.

The schedules stay in the tree behind `acacia_k_schedule=linear`, since §5.11's
fallback is to retain several as portfolio arms for P5 rather than averaging them
into one heuristic — and the exhaustive algebra proof is worth keeping regardless.


## P4 — improve OTFUR

### O1, lazy controller expansion: **LAND**

P2 and P3 each reduced exactly what they promised and moved nothing, because the
reduced quantity was not binding. So O1's premise was measured before it was built,
from counters the forward solver already keeps — actions applied per controller node
expanded:

| instance | actions/node | successors/node |
|---|---:|---:|
| `robot_grid_3x3` | 12.4 | 1.2 |
| `robot_grid_6_6` | 15.3 | 4.7 |
| `lift_gr1_3` | 17.5 | 3.3 |
| `AllLights` | **807.6** | **151** |
| `prioritized_arbiter6` | 129.9 | 78 |
| `collector_v1_7` | 2.0 | 2.0 |

Twelve to eight hundred actions applied per node, up to 151 successors materialised,
one selected. `advance_controller` now stops at the first safe, untried,
non-subsumed successor and computes no further action until that choice is proved
losing.

**Actions actually applied:** `robot_grid` 173,500 → 19,491 (−88.8%), `lift_gr1`
480,482 → 90,137 (−81.2%), `AllLights` 233,397 → 76,074 (−67.4%).

**And it converts to time**, which is what P2 and P3 failed to do:

| instance | eager | lazy | change |
|---|---:|---:|---:|
| `robot_grid_3x3` | 1.85 s | 0.82 s | **−56%** |
| `AllLights` | 7.31 s | 4.96 s | **−32%** |
| `lift_gr1_3` | 5.53 s | 4.03 s | **−27%** |
| `robot_grid_6_6`, `collector_v1` 7 and 11, `prioritized_arbiter6` | 25.0 s | 25.0 s | flat |

Zero verdict changes. §6.8 requires one target improving ≥25%; three do.

### Correctness

The existing 5,000-game fixed-seed harness still matches on F3/F1/F0, and lazy and
eager are now compared directly on **5,015 games** — same verdict, same losing
antichain, same strategy ranks. The eager path stays behind
`acacia_forward_eager_minimal_successors`, defaulting off, and F3 Pareto
minimisation stays on that side: `minimal_successors` equals `distinct_successors`
on `AllLights` (43,718), `collector_v1` (527,618) and `prioritized_arbiter6`
(1,972,517), so it removes nothing there.

### What O1 cannot do, and one prediction that was wrong

O1 reduces actions per controller node, not the number of nodes. The four flat
instances are resource-capped, and the profile predicted three of them correctly:
`robot_grid_6_6` creates 439 controller nodes per environment node and hits the
400,000 `max_ctrl_nodes` cap at K=2; `collector_v1` runs at 2.0 actions per node and
is immune twice over.

`prioritized_arbiter6` was predicted to improve and did not. It has 129.9
actions/node and only 27,484 controller nodes, so it is not node-capped — but with
3,281,024 raw actions it hits `max_edges = 2,000,000` instead. The prediction was
right about the mechanism and wrong about which cap binds, which is worth recording
because O2's byte accounting is aimed at exactly these caps.


## P4 — O3 batching reverted, O5 mis-scoped: what remains open

O3's batching was reverted (see the O3 commit) after measurement showed maximum
batch sizes of 2, 5, 2, 5 and 1 — the losing queue drains too often for insertions
to accumulate. The counters it added were kept, and they measured something
striking: **148,504,205 visited-node checks for 6,671 invalidations** on
`prioritized_arbiter` (22,261x), and 628,895 for 676 on `lift_gr1` (930x).

O5 was scoped against that finding, but against the wrong structure. The 22,261x
ratio is the cost of scanning the **visited environment-node set** during
invalidation — for each newly proved losing generator, checking every visited node
to see if the generator now subsumes it. `minimal_losing_antichain`'s own generator
list is a different, much smaller structure: it peaked at **177** entries on
`lift_gr1` in the O3 diagnostics. A linear scan over 177 elements is already cheap,
indexed or not, which is exactly what the measurement showed.

### O5 as attempted: correct, no material benefit, discarded

Sorting `minimal_losing_antichain`'s generators by rank and binary-searching the
feasible prefix preserved every correctness counter exactly — `queries`, `hits`,
`insertions`, `removals`, `invalidation_scans` and `nodes_invalidated` all
unchanged across before/after — but `prefilter_skips` did not increase (2,455 both
times). The reason: the existing per-element filter was already an O(1) integer
comparison per skip, so sorting only changes how cheaply you skip infeasible
elements, not how many expensive partial-order comparisons are avoided. Per the
acceptance criterion set before the work started — stop rather than add buckets on
top of a mechanism that is not working — the attempt stopped there and the diff was
discarded rather than committed.

### What is still open, and why it is not being pursued now

The structure that would actually address the 22,261x finding is an index over the
**visited environment-node set**, so that a newly proved losing generator can find
which visited nodes it subsumes without scanning all of them — closer to §6.7's
sketch of a `minimal_upset_antichain<State>` contributed to Posets, with
`subsumes`/`insert`/`size` as its first interface. That is a materially larger
change: it touches how environment nodes are stored and indexed, not a single
generator list, and §6.7 itself frames it as optional and lower priority, gated on
O3/O4 measurements rather than assumed. Those measurements now exist. Whether to
build it is a scope decision the plan reserves for re-approval before P5, not one to
make by continuing past it.

### P4 overall

| substage | decision |
|---|---|
| O1 lazy expansion | **LAND** |
| O2 interner + byte accounting | **LAND**, on the speed criterion, not the memory one |
| O3 batched invalidation | **STOP**, reverted; counters kept |
| O4 conditional covering | **AGGRESSIVE PRESET**, off by default |
| O5 antichain indexing (as scoped) | **STOP**, discarded; real target identified but not built |
