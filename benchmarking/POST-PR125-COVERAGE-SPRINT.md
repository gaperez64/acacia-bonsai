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
| P5 four-slot portfolio | yes | +62 full corpus, -28.6% time | **976 -> 1038** | 18 realizable | none | **forward for unreal, split for real** |
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


## P5 — isolated arm census (P5A)

Twelve arms of §7.3 run in isolation over the 180-instance deterministic
stratified panel, one process per instance, staged caps 1/5/17 under an 8 GiB
cgroup. **Zero verdict conflicts across all twelve arms.** Screened on the panel
rather than the full selection first: measured throughput put the full
1,524-instance census at ~5 h per arm, ~2.5 days for twelve, and the panel gives
the marginal-contribution signal for about 1/30th of that.

### Per-arm decisive answers, 180 panel instances

| arm | decisive | R | U |
|---|---:|---:|---:|
| `B-real-small` | 45 | 45 | 0 |
| `B-real-any` | 44 | 44 | 0 |
| `B-unreal-formula-small` | 68 | 0 | 68 |
| `B-unreal-automaton-small` | 66 | 0 | 66 |
| `S-real-small` | 46 | 46 | 0 |
| `S-real-any` | 45 | 45 | 0 |
| `S-unreal-formula-small` | 67 | 0 | 67 |
| `S-unreal-automaton-small` | 67 | 0 | 67 |
| `F-real-small` | 46 | 46 | 0 |
| `F-real-any` | 46 | 46 | 0 |
| `F-unreal-formula-small` | **81** | 0 | 81 |
| `F-unreal-automaton-small` | 79 | 0 | 79 |

Every arm is polarity-pure, which confirms the CLI isolation does what §7.3 needs:
one child, one polarity, no contamination. It also means no single arm is ever a
portfolio, which is why the selector requires one of each.

### Four arms reach the twelve-arm ceiling

| subset size | best decisive union |
|---|---:|
| 2 arms | 127 |
| 3 arms | 134 |
| **4 arms** | **137** |
| all 12 arms (oracle) | **137** |

A four-slot portfolio captures *everything* twelve arms can. Best 4-arm set:
`F-real-small + F-unreal-formula-small + S-real-small + S-unreal-automaton-small`.

Redundancy is high: across all twelve arms only **2** answers are unique to a
single arm, both in `F-unreal-formula-small`. Arm choice is therefore mostly about
efficiency, not reachability — with that one exception.

### The forward backend, not the arm shape, is where the coverage is

Comparing like-for-like arm *shapes* as oracle unions on the same 180 instances:

| configuration, default 3-child shape | union |
|---|---:|
| B — the shipping default | **120** |
| S | 120 |
| **F** | **134** |
| best 4-arm, mixed S and F | 137 |

| | |
|---|---:|
| union of all 8 B and S arms | 122 |
| union of all 4 F arms | 134 |
| **F adds over all B/S** | **15** |
| B/S adds over all F | 3 |

Switching the backend to forward, keeping the existing three-child shape and
adding no process slots, captures **+14 of the +17** available. The remaining +3
needs the mixed S/F set, which the current architecture cannot express in one
process because the backend is a compile-time choice — that is exactly the runtime
`game_backend` field of P5B, and it buys 3 panel instances.

This inverts PR #125's picture, where B and F were near-equal at 1056 and 1053 of
1524. The most likely cause is P4: O1 cut forward actions per controller node by
67–89% and O2 added a further 16–29%, both landed since that measurement. P4's
work appears to have shifted the portfolio balance toward forward.

### Caveats, stated before any decision rests on this

These are **isolated oracle unions on a 180-instance panel**, not race results on
the full selection. §7.5 is explicit that the isolated union is an upper bound. One
independent check exists: G3 measured the actual raced default portfolio at 118/180
on this same panel against the 120 oracle computed here for that same arm set, so
the race penalty looks small — but that is one data point, not a measured race of
the proposed profile.

### Forward is an unrealizability engine, not a general-purpose backend

Splitting the census by polarity is what makes the result actionable:

| polarity | B | S | **F** | F adds over all B/S | B/S adds over F |
|---|---:|---:|---:|---:|---:|
| real | 45 | 46 | 46 | 3 | **3** |
| unreal | 75 | 74 | **88** | **12** | **0** |

On the real side forward has no edge — it trades three for three. On the unreal
side it **subsumes every answer all four B and S unreal arms produce** and adds
twelve more, with none going the other way.

So the best three-arm set is `S-real-small + F-unreal-formula + F-unreal-automaton`
at **134/180**, against **120** for the shipping default, in the *same three-slot
budget*. That configuration cannot be built today: `ACACIA_FORWARD_SAFETY_SOLVER`
is a compile-time define, so one binary means one backend for every child. The best
single-binary approximation is forced to spend its real worker on a backend that
brings nothing there.

### Remaining work, reordered by what the census showed

1. **Finish the full-corpus race.** Reading criterion: absence of large regressions
   matters more than the size of the margin, because forward runs on naive data
   structures and the headroom there exceeds the current margin either way.
2. **Split the backend choice by polarity** (supersedes P5B). Runtime, per-arm
   backend so one process can run backward-with-local-certificates for its real
   worker and forward for both unreal workers, reproducing the 134/180 set.
3. **Data structures for the forward solver.**
   `grep -c 'posets::' src/solver/forward_reachable_safety.hh` returns **0** —
   forward is entirely hand-rolled flat vectors while backward picks
   `rank_bucketed_vector_backed` from sixteen tuned posets implementations. The
   target is the invalidation scan over the visited environment-node set that O3
   measured at 148,504,205 checks for 6,671 invalidations (22,261x), **not** the
   177-entry generator list O5 mistakenly indexed.
4. **Arm-choice flexibility** — backend, polarity, translation preference and
   unreal transform independently selectable per worker.
5. **Three-way comparison, then four new top configurations.** Deferred to last so
   it measures finished work: v1 `5ffd8f99`, ltlsynt 2.15.1.dev, best from main
   head, and sprint-best, on the August panels and SyFCo caches reused verbatim.

### P6 as originally specified is not justified by this data

§8.1 gates P6 on P5 showing that forward real and forward unreal arms both carry
marginal coverage **and** that a four-arm replacement or routing profile cannot
retain enough of it. A four-arm profile retains **all** of it — 137, the full
twelve-arm oracle ceiling. The condition fails, so the six-wide race is not
justified and is not built.


## P5 — full-corpus backend race (result)

Both binaries run their own default portfolio over all 1,524 instances, staged caps
1/5/17, 8 GiB cgroup, sequential, same tree.

| | decisive | conflicts |
|---|---:|---:|
| B (backward, shipping default) | **976** | 0 |
| F (forward) | **1038** | 0 |
| net | **+62** | |

**Zero verdict disagreements.** The fresh B run returned exactly 976, matching the
figure recorded during the P1 gate campaign on an earlier tree — confirming rather
than assuming that P2–P4 left the backward path untouched, since none of them
changes backward-solver code.

### The panel understated the effect by 4x

The 180-instance screen extrapolated to about +14. The full selection gives **+62**.
Screening was still the right call — it cost 2.5 h instead of 2.5 days and correctly
identified the direction and the polarity split — but its *magnitude* was not
predictive, which is worth remembering before quoting a panel number as a forecast.

### Time, not just coverage

On the 958 instances both decide:

| | F | B | |
|---|---:|---:|---|
| total | **348.6 s** | 488.1 s | **-28.6%** |
| median | 0.024 s | 0.036 s | -33% |
| faster on | **679** | 279 | 71% |
| sub-1 s | 894 | 873 | +21 |

Forward is not buying coverage with time; it is cheaper on identical work.

Where the budget goes, on F: cap 1 gives 909 answers for 66.6 s (88% of answers, 8%
of time); cap 17 gives 56 answers for 525.0 s (5% of answers, 68% of time). The
staged design is doing real work, and the expensive tail is what made the full
12-arm census a 2.5-day proposition.

### The regression set decides the portfolio shape

| | F-only | B-only |
|---|---:|---:|
| UNREALIZABLE | **59** | **0** |
| REALIZABLE | 21 | 18 |
| total | **80** | **18** |

**No unrealizable answer exists that backward gets and forward does not**, across
all 1,524. The panel's subsumption claim holds at scale — forward's unreal
dominance is real, not a 180-instance artifact.

The **real** side is genuinely complementary: F gets 21 B misses, B gets 18 F
misses, neither subsumes. Seven of the 18 are `collector_v2`, and `collector` is the
handoff's own named forward negative control (§6.2); P4's profiling measured
`collector_v1` at 2.0 actions per controller node and node-capped, so forward
failing there is expected behaviour rather than a surprise.

So the portfolio composition is settled by data rather than by the panel's guess:

```
panel suggested (3):  S-real + F-unreal-formula + F-unreal-automaton
                      ...misses the 18 backward-only realizable instances

corpus says    (4):  B-real + F-real + F-unreal-formula + F-unreal-automaton
                      ...both real backends, forward-only unreal
```

Four arms, so the existing four-slot budget suffices and §8.1's gate for P6 still
fails. What the 18 change is *which* four — and they raise the value of the
split-backend work, since that portfolio cannot be expressed by any single binary
today.

---

## Stage 1b — three defects found by review, none of which had a test

Two read-only codex reviews ran while the full-corpus race was executing. Neither
built anything, so the race is uncontaminated. They confirmed P1's soundness ("I
found no false-UNREAL path", checked against the top-level implication guard,
`G(A & B)` distribution, the empty-invariant and vacuous-trigger cases) and found
three defects, all now fixed.

### The B/S/F builds are what they claim to be — verified, not assumed

Before trusting any of the recorded numbers, the three reference binaries were read
back out of their own generated headers:

| build | `LOCAL_CERTIFICATE` | `FORWARD_SAFETY_SOLVER` | `ENABLE_EQUIVARIANT_SOLVER` |
|---|---:|---:|---:|
| `build_p5_B` | 0 | 0 | 1 |
| `build_p5_S` | **1** | 0 | 1 |
| `build_p5_F` | 0 | **1** | **0** |

This matters for one reason worth stating plainly: **F raced with the equivariant
solver switched off, B raced with it on.** F still won by 62. The forward margin is
therefore *understated* by whatever the equivariant pre-pass contributes to B, not
inflated by it.

### 1. The two configuration frontends disagreed on a solver-behaviour option

`meson.options` defaulted `acacia_local_certificate` to **true**;
`config/acacia-options.json` and `src/configuration.hh` both said **false**. That
option is the one thing separating the S reference configuration from B, so a plain
`meson setup build` produced S while calling itself B.

The reason it survived: `scripts/acacia-config.py` passes every option explicitly,
so the divergence is invisible from exactly the path used for measurement. The
recorded campaigns are unaffected — verified by re-generating `build_p5_B`'s
`acacia_build_config.hh` after the fix and confirming it is byte-identical.

An audit of all 27 shared scalar options found exactly one other divergence, and
that one is deliberate: `acacia_enable_tlsf_frontend` is false in meson (the default
build stays free of the flex/bison dependency; `.github/workflows/main.yml` covers
the frontend in a dedicated job) and true in the JSON (every `acacia-config.py`
binary drives the harness through `-T`). It differs in what the binary *can do*, and
a binary without it rejects `-T` outright rather than answering differently.

`tests/check-config-frontends.py` now enforces agreement on every shared scalar
option and admits a divergence only with a written reason. Reintroducing the
`local_certificate` default fails it.

### 2. The arm selector could have ranked a soundness bug as coverage

`select-portfolio-arms.py` kept the faster of two answers **without comparing
verdicts**. An arm returning REALIZABLE where another returned UNREALIZABLE would
have grown the union by an instance the subset decides *inconsistently*. On a
two-arm census built to disagree, the old code reported "2 decided" and exited 0; it
now names the instance and exits 1. The scan runs once over the whole census before
ranking, so it reports every conflict rather than aborting on whichever subset
happens to contain the offending pair first.

Re-running the real twelve-arm census after the fix is unchanged — 137 is still the
four-arm ceiling — so nothing previously reported rested on a masked conflict.

### 3. "Forward backend" did not name the backend that runs

`-Dacacia_forward_safety_solver=true` is not an exclusive selection:
`solve_with_downset` tries the equivariant solver first
(`src/solver/solve_game_impl.hh:164-178`) and the two meson options are independent.
The F preset sidesteps this by disabling the equivariant solver at compile time —
which is why the measured forward numbers are clean — but the option name still
promises something it does not deliver.

This one is not a patch, it is a design constraint on Stage 2: a runtime backend
enum has to name the backend that will actually run, so the equivariant pre-pass has
to become explicitly conditional rather than implicitly first. Carried there.

### What the local certificates turned out to be worth

Incidental, but it settles Stage 2's scope. Of the twelve subsets that reach the
137/180 ceiling, the four fastest all avoid the S arms entirely:

```
137   140.14s   B-real-any + F-real-small + F-unreal-automaton-small + F-unreal-formula-small
137   141.41s   B-real-small + F-real-any + F-unreal-automaton-small + F-unreal-formula-small
137   143.33s   B-real-any + F-real-any + F-unreal-automaton-small + F-unreal-formula-small
137   145.50s   B-real-any + B-unreal-automaton-small + F-real-small + F-unreal-formula-small
137   145.71s   F-real-small + F-unreal-automaton-small + F-unreal-formula-small + S-real-any   <- first with S
```

Local certificates reach the same ceiling and cost more time to get there. So the
runtime backend enum needs **two** values, not three: `backward` and `forward`. The
four `ACACIA_LOCAL_CERTIFICATE` sites in `k_bounded_safety_aut.hh` stay compile-time
and stay out of Stage 2 — which also keeps the change well clear of the backward
solver's hot path.

Note that the top subset is exactly the shape the full-corpus race independently
arrived at: **backward for real, forward for unreal, plus a forward real arm.** Two
different measurements, one at 180 instances and one at 1,524, agreeing on the
portfolio.

---

## Stage 2 — runtime game backend

### Why per-polarity flags are not enough, established before spending a campaign

The full-corpus race decomposes by polarity for free, and nobody needed to run
anything to see it: a portfolio's REALIZABLE can only have come from a real arm, and
its UNREALIZABLE only from an unreal one. So the race summaries already contain the
per-polarity arm results.

```
B = 976 decisive = 496 real + 480 unreal
F = 1038 decisive = 499 real + 539 unreal

3-arm mixed  (B-real + F-unreal x2)          = 1035     <- three WORSE than F
4-arm target (B-real + F-real + F-unreal x2) = 1056     <- exactly the B u F ceiling
```

The shipping default portfolio is **three** arms — `ACACIA_TRANSLATION_PREFS` gives
one real preference and `DEFAULT_UNREAL_X` gives both unreal transforms — and the
best three-arm portfolio a per-polarity flag can build *loses three instances against
plain forward*. It trades forward's 21 real-only answers for backward's 18. Running
that campaign would have cost hours to measure a number already derivable, and the
number is negative.

The 4-arm target needs **two real arms with different backends**, which no
per-polarity flag can express. Worse, one census subset that reaches the 137/180
ceiling is `B-real-any + F-real-any` — same translation preference, different backend
— which `-r` cannot express either, since `append_strategy`
(`src/arg_parser.hh:177-183`) rejects duplicates within a `-r` list.

So Stage 2 splits: the backend plumbing, which is landed and verified, and an
explicit per-arm portfolio specification, which is what makes it measurable.

### Behaviour preservation, checked rather than argued

The claim that existing builds are unaffected is worth more than a code-reading. Both
reference configurations were rebuilt from the Stage 2 source with **byte-identical
meson options** (verified: zero option differences against the recorded builds) and
run through the frozen 40-instance gate:

| configuration | recorded reference | rebuilt from Stage 2 source |
|---|---|---|
| B | 35 solved, 5 lost, PAR-2 217.557s | 35 solved, **the same 5**, PAR-2 218.111s |
| F | 37 solved, 3 lost, PAR-2 162.794s | 37 solved, **the same 3**, PAR-2 164.980s |

Identical instance sets, PAR-2 within 0.3% and 1.4%. The gate reports `GATE FAIL` for
both — before and after the change — because its baseline is `build_best_decomp_mona`,
a third configuration; those losses are the known backward-baseline mismatch.

A first attempt to gate this used the build codex had made, which carried only three
`-D` flags and therefore differed from F in **six** options — `ios_precomputer`
standard rather than `semantic_mona`, `vector_downset` plain rather than
`rank_bucketed`, and four more. It lost two instances more than F, and the difference
had nothing to do with the change under test. A build with forward switched on is not
the forward configuration.

### The equivariant pre-pass is now bound to the backward backend

Deliberate, and the reason the enum exists. A binary built with both options on ran
the equivariant solver before it ever reached the forward branch, so
`-Dacacia_forward_safety_solver=true` did not name the backend that ran. The forward
reference sidestepped this by compiling the equivariant solver out — which is why the
recorded forward numbers are numbers *without* the pre-pass. Binding it to backward
reproduces both measured configurations exactly instead of inventing a third
combination for which no measurement exists.

### One defect found in review, carried to the follow-up

Requesting a forward arm on a binary built without the forward solver warned and fell
back to backward. Codex's own proof of the fallback shows the problem:

```
[real=small,backend=forward] Warning: forward game backend requested but not compiled in; falling back to backward
REALIZABLE
```

A run labelled `backend=forward` that ran backward — a **mislabelled** data point, in
the one piece of work whose entire purpose is per-arm attribution. A failed run is
recoverable; a mislabelled one quietly corrupts a census. It becomes a parse-time
error. (`utils::vout` also writes to `std::cout`, the channel carrying the verdict
that `benchlib.py`'s `parse_acacia_result` substring-matches, but that is the smaller
objection: the message happens not to contain "realizable".)

### The four-arm portfolio, measured: 1052

One binary, one process, four arms
(`--arms real:small:backward,real:small:forward,unreal:formula:forward,unreal:automaton:forward`),
same protocol as the race: all 1,524, staged caps 1/5/17, 8 GiB cgroup, no swap.

| | decisive | real | unreal | conflicts |
|---|---:|---:|---:|---:|
| B (backward, 3 arms) | 976 | 496 | 480 | 0 |
| F (forward, 3 arms) | 1038 | 499 | 539 | 0 |
| **mix4 (4 arms)** | **1052** | **517** | **535** | **0** |

**+14 over forward, +76 over backward.** The prediction was 1056, and the gap is
fully accounted for.

### The recovery is exact

The second real arm was added to recover the 18 realizable instances backward
decides and forward misses. It recovered **all 18, and nothing else**:

```
predicted recoverable (B-only):   18
mix4 gained over F:               18
  of which are exactly B-only:    18
  gained but not B-only:           0
  B-only still missed:             0
polarity of the 18:               18 realizable, 0 unrealizable
```

`collector_v2` accounts for 7 of them and `chain-simple-*-real` for 4 — the
families P4's profiling identified as forward's node-capped weak spot. The
mechanism predicted from the polarity decomposition is the mechanism observed,
instance for instance.

### The four losses are the cap boundary, not the portfolio

Four instances forward decides that the four-arm race does not:

```
arbiter_pb_5_pe_.ltl        REALIZABLE     F took 16.787s   at the 17s cap
finding_nemo_pb_2_pe_.ltl   REALIZABLE     F took 15.375s   at the 17s cap
g-real-real.ltl             REALIZABLE     F took 15.805s   at the 17s cap
patrolling-alarm26.ltl      UNREALIZABLE   F took 15.579s   at the 17s cap
```

All four were already inside 90% of the cap, and a fourth arm's contention pushed
them over. The same pattern appeared at the 1 s cap during the run — the five
instances lost there had taken forward 0.879 s to 0.991 s against a 1.0 s cap —
which makes this a boundary effect rather than a coverage loss. Under a larger cap
the portfolio's coverage is the full 1056.

The unreal arms are a built-in control for this: they are *identical code with
identical flags* in F and in mix4, so their delta (539 → 535) measures contention
and nothing else. It did not grow with the cap — three at 1 s, four at 17 s — so
contention is a small fixed set of boundary instances, not a tax that scales.

### It is also faster

On the 1,034 instances both decide, mix4 takes **711.2 s against forward's 736.1 s,
−3.4%**, despite running four children instead of three. It is slower on more
instances than it is faster on (662 versus 372), so the saving is concentrated in
the expensive ones: the backward real arm answers some hard realizable instances
far faster than forward does, which is the same complementarity that motivated the
portfolio.

### Decision: **LAND**

`B-real + F-real + F-unreal-formula + F-unreal-automaton` decides 1052 of 1,524
against the shipping default's 976 — **+76** — with zero verdict conflicts, in less
total time, within the existing four-slot budget. §8.1's gate for P6 still fails,
and now for a stronger reason: four arms reach the ceiling that six were speculated
to be needed for.
