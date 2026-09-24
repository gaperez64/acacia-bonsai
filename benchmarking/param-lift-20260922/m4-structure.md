# M4 prerequisite: what actually has to be generalized, measured

Before building the index-aware generalizer, two questions decide what it can possibly do, and
neither was measured during the previous sprint. They are asked separately here because they have
different answers, and conflating them is part of why W5 failed.

1. **Specification side** — across `n`, is the *objective* presented in an index-aligned way, so a
   predicate learned about index `i` at a small seed can be re-instantiated at index `i` at the
   target? Data: `m4-spec-arity.tsv`.
2. **Solution side** — is the *winning region* a conjunction of fixed-arity local predicates, so
   that one template learned at a seed determines it at every `N`? Data:
   `m4-invariant-separability.tsv`.

Both were measured on the exact-semantics per-conjunct monitor games from M1
(`gr1_monitor_game.py --semantics exact`), whose `--provenance-out` already carries, per monitor,
the index tuple, the `i0`/`i1`-placeholder template, the arity kind, and the support buses.

## 1. Specification side: the reduction is index-aligned, and the provenance says how

Each monitor is one conjunct of the lowered TLSF, so the game's state variables partition by
conjunct, and each conjunct carries its own index tuple. That partition is the index-alignment
bridge the plan called our own contribution, and it is already emitted — it does not have to be
recovered from an optimized AIG, which is what W5 was trying to do.

Monitor counts per family and `n` (`m4-spec-arity.tsv`), classified by index arity:

| family | local (arity 1) | pairwise (arity 2) | bus-wide | per-index role classes |
|---|---|---|---|---|
| `arbiter` | 8,12,16,20 (n=2..5) | 0 | 1 | 1 |
| `round_robin_arbiter` | 8,12,16,20 | 0 | 1 | 1 |
| `prioritized_arbiter` | 4,6,8,10 | 0 | 2 | 1 + a scalar client `m` |
| `arbiter_with_cancel` | 12,18,24 (n=2..4) | 0 | 1 | 1 |
| `amba_decomposed_arbiter` | 7,10,13,16 | 0 | 4 | 2 (index 0 special) |
| `load_balancer` | 5,7,9,11 | 1,2,3,4 | 3 | 2 (index 0 special) |
| `round_robin_arbiter_unreal2` | 8,12,16,20 | 0,3,6,10 | 1 (2 at n=2) | 1 |
| `lift` | 2,5,6,8 | 0 | 14,22,29,36 | — |
| `simple_arbiter_enc`, `prioritized_arbiter_enc` | 0 | 0 | 8..15 | — |

Four things follow, each of which bears directly on a W5 failure:

- **The replicated families are cleanly aligned.** `arbiter` and `round_robin_arbiter` add exactly
  four local monitors per client, every index carries the identical template set, and there is a
  single bus-wide conjunct. Generalizing across `n` here is re-instantiating one template set.
- **`round_robin_arbiter_unreal2` has Θ(n²) pairwise conjuncts** — 0, 3, 6, 10 at n=2..5, i.e.
  exactly `C(n,2)` instances of `G(!r_i0 | X!r_i1 | F(g_i0 & g_i1))`. **At n=2 there are none of
  them** (the single pair is absorbed into a bus-wide conjunct: bus count is 2 at n=2 and 1 from
  n=3). So n=2 is not a small instance of this family, it is a structurally different game. The
  previous sprint seeded this family at n=2. Seeds must be drawn at or above the `n` from which the
  template set stops changing, which is n=3 here.
- **`*_enc` families have zero local monitors** — every conjunct is bus-wide, because `n` is a
  bit-width, not a client count. There is no index to align. These are correctly out of scope, as
  the plan predicted.
- **The bus-wide conjuncts must be recognized, not anti-unified.** Four families share the mutual
  exclusion conjunct, and it is emitted as a balanced tree whose *shape changes with `n`*:
  `G((!g_0 & !g_1 & (!g_2 | !g_3)) | (!g_2 & !g_3 & (!g_0 | !g_1)))` at n=4. Syntactic
  anti-unification across `n` cannot recover this. The generalizer needs a schema library keyed on
  (arity kind, role, bus) — `AtMostOne(bus)`, `AtLeastOne(bus)`, `NoneOf(bus)` and the `U`/`W`
  variants — matched semantically against the bus, with the provenance supplying the bus membership.

## 2. Solution side: separability arity of the winning region

`m4-invariant-separability.tsv`. For each seed, group the state variables by owning client (shared
= scalar and bus-wide monitors' variables), and ask for the smallest `k` such that `inv` equals the
conjunction of its projections onto every `k`-subset of clients. Projections only ever weaken, so
the conjunction always contains `inv`; equality is the whole question. `min_k` is the template
arity a conjunctive generalizer would have to use, and the interesting property is whether it stays
constant as `n` grows.

**Fixed arity — one template determines every `N`:**

| family | min-k | confirmed at |
|---|---|---|
| `prioritized_arbiter` | 1 | n=3,4,5,6 |
| `collector_v1` | 1 | n=3 |
| `arbiter_with_buffer` | 1 | n=2,3,4 |
| `simple_arbiter_with_hints` | 1 | n=2,4,6 |
| `amba_decomposed_lock` | 1 | n=2,3,4 |
| `arbiter` | 2 | n=3,4,5,6,7 |
| `load_balancer` | 2 | n=2,3,4,5 |
| `arbiter_with_cancel` | 2 | n=2,3,4 |
| `abcg_arbiter` | 2 | n=2,3 |
| `arbiter_on_inpchange` | 2 | n=2,3,4 |

(The second round, `m4-invariant-separability-round2.tsv`, added the last five. `collector_v3` came
out at `min_k = 3` with only n=3 measurable — its n=5 seed fails to solve — so it is recorded as
unconfirmed rather than counted either way. The `*_unreal2` families are UNREALIZABLE by
construction and need M5's dual certificate, not this measurement.)

**Arity grows with `n` — no fixed-arity conjunctive schema:**

| family | min-k | over-approximation at k=1 |
|---|---|---|
| `round_robin_arbiter` | n (3,4,5) | 1.043 → 1.017 → 1.007 |
| `lift` | n (2,3) | 1.134 → 1.042 |
| `amba_decomposed_arbiter` | n (2,3,4) | 1.0003 → 1.0002 → 1.0001 |

For `round_robin_arbiter` the local approximation is within 4.3% of the winning region at n=3 and
0.7% at n=5 — it gets relatively closer as `n` grows but is never exact at any fixed arity. This is
the solution-side counterpart of the specification-side finding: the family's rotation is a global
object, so no conjunction of bounded-arity local predicates captures its winning region.

`amba_decomposed_arbiter` is the near-miss: unequal at every `k < n`, but by so few states that the
ratio is 1.0000 to four decimals. A strengthening search would likely succeed there.

### The honest limit of this measurement

`min_k` measures whether the winning region is *exactly* a conjunction of `k`-local projections. M4
does not need the exact winning region — it needs *some* adequate inductive invariant, which may be
a strict **subset**. Projections only produce supersets, so a family with `min_k = n` may still
admit a fixed-arity invariant that is stronger than `inv`. `min_k = n` is therefore evidence that
the obvious local schema fails, not proof that no schema exists. Finding a strengthening is what
Maderbacher & Bloem do with SyGuS, which needs an SMT solver with a synthesis frontend; neither z3
nor cvc5 is installed here (plan M7).

An adequacy experiment was attempted — substitute the projection conjunction for `inv` in the
certificate and re-check — and it is **not** informative as constructed: since projections
over-approximate, the extra states have no rank by construction and the check fails for that reason
alone rather than because the candidate is not inductive. Reported here so it is not repeated.

## What this sets up for M4

- **Generalize `(inv, ranks, move_j)`, then re-Skolemize at `N`.** The exported strategy is global
  in every family measured; the relation it was Skolemized from is not (section 2b).
- Generalize per-family against a declared template arity, taken from `min_k`, rather than one
  hard-coded shape. Ten families are measured to have a constant arity — five at 1 and five at 2 —
  and they are where a conjunctive generalizer is known to be able to succeed, so they are M4's
  targets. They hold **42 of the 147** unsolved instances in the reducible set (28.6%).
- Draw seeds at or above the family's stable `n`, never below (`round_robin_arbiter_unreal2` n=2,
  and n=1 for every replicated family, are degenerate).
- Treat bus-wide conjuncts through a semantic schema library, not syntactic anti-unification.
- `round_robin_arbiter`, `lift` and `amba_decomposed_arbiter` need an invariant *strengthening*
  search, which is a different technique from the conjunctive generalizer and should be scoped
  separately rather than folded into M4's first cut.

## Reproduction

```
python3.13 scripts/gr1_monitor_game.py <tlsf> --semantics exact \
    --output G.aag --provenance-out G.prov.json     # PYTHONPATH=/usr/local/lib64/python3.13/site-packages
tlsfsolve --policy P.aag --certificate C.aag --certificate-json C.aag.json G.aag
```

then the two analysis drivers kept with this sprint's scratch work (`ksep.py` for separability,
reading `C.aag` + `C.aag.json` + `G.prov.json`; the arity table is a direct read of the provenance).
All runs were cgroup-wrapped at `MemoryMax=4G`/`6G` and serialized.

Caveat on model counts: `bdd_satcount` spans BuDDy's whole declared variable count, where a
few-state difference disappears into double precision and every ratio reads 1.000. The ratios above
use `bdd_satcountset` over the state-variable cube. The equality verdicts are BDD identity and were
never affected.

## 2b. Generalize the move relation, not the strategy

The policy `tlsfsolve --policy` exports is one Skolemization of the winning move relation, with
arbitrary tie-breaking. Measuring the support of each guard `controllable_g_i` against the client
partition (the `polsep.py` driver):

| seed | guards | own-client-local | mean foreign clients read |
|---|---|---|---|
| `arbiter` n=3,4,5 | 3,4,5 | 0 | 2.00, 3.00, 4.00 |
| `round_robin_arbiter` n=3,4 | 3,4 | 0 | 2.00, 3.00 |
| `prioritized_arbiter` n=3,4 | 4,5 | 0 | 2.25, 3.20 |
| `load_balancer` n=3,4 | 3,4 | 0 | 2.00, 3.00 |
| `arbiter_with_cancel` n=3 | 3 | 0 | 2.00 |

**Not one guard is local.** Every guard reads every other client, at every `n`, in every family —
including the families whose winning region is per-client separable. Mutual exclusion alone forces
a grant to look at all the others once a total order has been imposed to break ties.

This is W5's mistake in a new guise: generalizing the synthesized strategy means generalizing
arbitrary tie-breaking. Running the same separability measurement on `move_j`, the most-permissive
move relation that M2 already exports **before** Skolemization, gives the same arity as the
invariant:

| family | `min_k` of `inv` | `min_k` of `move_0` |
|---|---|---|
| `prioritized_arbiter` | 1 | 1 (n=3,4) |
| `arbiter` | 2 | 2 (n=3,4) |
| `load_balancer` | 2 | 2 (n=3) |
| `arbiter_with_cancel` | 2 | 2 (n=3) |
| `round_robin_arbiter` | n | n (3,4) |

So M4 should generalize `(inv, ranks, move_j)` — all three fixed-arity for the same five families —
and **re-Skolemize at the target `N`** with a canonical, `n`-independent tie-break (lowest index
first), rather than trying to generalize a strategy that was never local to begin with. The
Skolemization is cheap and local to the target; the relation is what carries across `n`.

## 3. The two-parameter families, and a concrete encoding defect they expose

Four reducible families take two parameters and were skipped by the single-parameter census
(`m4-alignment-2param.tsv` holds the per-axis results). Two of them matter a lot:
`round_robin_arbiter_unreal1` has **21** unsolved instances and `load_balancer_unreal1` has **7** —
28 between them, the largest single block in the reducible set.

Holding one parameter fixed and varying the other shows the two axes are completely different:

| family | axis | verdict | monitors |
|---|---|---|---|
| `round_robin_arbiter_unreal1` | `u` (n=3 fixed) | replicated, roles=1, local=0.94 | 16, 16, 16, 16 — **constant** |
| `load_balancer_unreal1` | `n` (u=2 fixed) | roles=2 constant | 11, 16, 22, 29 |
| `load_balancer_unreal1` | `u` (n=2 fixed) | roles=2 constant | 11, 11, 11, 11 — **constant** |
| `chomp` | `M` / `N` | replicated, roles=1 | 12, 14 |
| `robot_grid` | `xN` | weak, local=0.18 | bus-dominated |

`n` is the familiar interface width. **`u` is an X-depth**: the pairwise conjunct is
`G(!r_i0 | X(!r_i1 | X^(u-1)(g_i0 & g_i1)))`. It adds no monitors at all — it only deepens one.

That makes `u` a much easier kind of parameter than `n`, and it is where a real defect shows up.
Latch totals for `round_robin_arbiter_unreal1` at n=3, by `u`:

| u | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| total latches | 41 | 47 | 59 | 83 | 131 | 227 | 419 |
| largest monitor, states | | | | | 33 | 65 | 129 |

The largest monitor has `2^(u-1)+1` states, and the provenance records
`"latch_encoding": "one-hot"` — one latch per automaton state. The `2^u` state count is inherent
(overlapping obligations mean the monitor must remember, for each of the last `u` steps, whether the
trigger fired — a `u`-bit shift register). **What is not inherent is spending `2^u` latches on `u`
bits of state.** A binary state encoding gives `u` latches for the same automaton: at u=7 that is
roughly 40 latches instead of 419, i.e. a BDD over ~40 variables instead of ~419.

So for this block of 28 unsolved instances the obstruction on the `u` axis is our own monitor
encoding, not the problem. This is a concrete, bounded change to M1's `gr1_monitor_game.py`
(binary-encode monitor states, or recognize the shift-register template and emit the register
directly) and it should be measured before any generalization work is aimed at these families.

One caveat before acting on it: one-hot keeps each monitor's transition relation a simple
disjunction, which can keep BDDs small for irregular automata, so binary encoding is not
automatically better everywhere. It is unambiguously better for these shift-register monitors. The
change should be measured on the existing seeds, not assumed.


## 4. One hypothesis tested and refuted

`min_k = n` is suggestive: `AtMostOne` over a bus *is* a conjunction of pairwise constraints, so it
would have surfaced as `min_k = 2`. An n-ary **disjunction** would not — it is exactly the shape no
fixed-arity conjunctive template can express, while a quantifier schema `exists j. q(j)` expresses it
easily. That suggested `round_robin_arbiter` might be generalizable after all, just not
conjunctively.

Tested directly (`disj.py`): take `P`, the conjunction of the `(n-1)`-subset projections, which
strictly contains `inv`, and let `R = P and not inv`. If `R` were itself a conjunction of per-client
predicates `AND_i b_i`, then `inv = P and (OR_i not b_i)` — a pairwise part plus one n-ary
disjunction of local predicates.

| seed | `R` nonempty | `R` per-client separable | `inv == P and (OR_i not b_i)` |
|---|---|---|---|
| `round_robin_arbiter` n=3 | yes | **no** | no |
| `round_robin_arbiter` n=4 | yes | **no** | no |
| `round_robin_arbiter` n=5 | yes | **no** | no |

Refuted. The residue is not per-client separable either, so `round_robin_arbiter`'s winning region
is not a bounded-arity conjunction plus a simple existential over local predicates. That family
needs a genuine strengthening search, and this closes off the cheap route to it.

## 5. What the other seven families are blocked by, and it is not parameterization

Seven of the 36 parametric families are not DBA-reducible and hold the other 25 of the 172 unsolved
instances. They share one cause, visible in `m0-census.tsv`'s `non_recurrence_templates`: a conjunct
on the guarantee side that is itself an implication between recurrence properties.

| family | unsolved | blocking conjunct |
|---|---|---|
| `ltl2dba_C2_unreal` | 6 | `GFg <-> (GFr_i & GFr_i)` |
| `ltl2dba_theta` | 5 | `GFacc <-> !(GFp_i -> G(q -> Fr))` |
| `ltl2dba_R` | 4 | `GFp_i <-> GFacc` |
| `lift_gr1`, `lift_gr1+` | 4 + 4 | one monolithic `G(...)` the splitter does not decompose |
| `collector_v2` | 1 | `GFallfinished <-> (GFfinished_i & ...)` |
| `generalized_buffer` | 1 | a per-index assume-guarantee pair inside one conjunct |

`spot.mp_class` puts `GF a <-> GF b` — and even a bare `GF a -> GF b` — in the **reactivity** class,
while `GF a` and `G(a -> F b)` are recurrence. That is the whole obstruction. GR(1) has exactly one
assumption/guarantee split at the top, `(AND_i GF A_i) -> (AND_j GF G_j)`, and the PPS fixpoint
solves that one Streett-like condition. An implication *nested inside* a conjunct is a second Streett
pair, and a conjunction of `k` such pairs is a Streett condition of index `k`, which the tri-nested
fixpoint does not solve.

So these seven are out of reach of this route because of their **acceptance condition**, not because
of anything about their parameter. No amount of index alignment or invariant generalization reaches
them; they would need a Streett/Rabin solver. Worth stating explicitly, because "parametric family
we cannot lift" and "objective the GR(1) fixpoint cannot express" are different failures and only
the first is this sprint's subject.

## Coverage accounting

Of the **172** unsolved instances across the 36 parametric families:

| | instances | share |
|---|---|---|
| M4 in scope — measured constant separability arity (10 families) | 42 | 24% |
| blocked by the one-hot monitor encoding (`*_unreal1`, §3) | 28 | 16% |
| acceptance condition needs Streett/Rabin (7 families, §5) | 25 | 15% |
| needs an invariant strengthening search (`min_k = n`, 3 families) | 17 | 10% |
| needs M5's dual certificate (`*_unreal2`) | 6 | 3% |
| reducible but not yet classified | 54 | 31% |

## 6. Correction: what `min_k` does and does not measure

`m4-invariant-separability.tsv` now carries `vars_per_client`, `shared_vars`, `shared_pct` and
`dropped_by_k1`, because the `min_k` column on its own reads as more than it supports.

The projection used to compute `min_k` keeps **every shared variable** — those belonging to scalar
and bus-wide monitors — in every conjunct, and projects away only other clients' local variables. So
the strength of the test depends entirely on how much of the state is local:

| seed | vars/client | shared | shared % | a k=1 projection drops | min_k | generalizer |
|---|---|---|---|---|---|---|
| `arbiter` n=4 | 11 | 2 | 4% | 33 of 46 | 2 | succeeds at k=2 |
| `round_robin_arbiter` n=4 | 10 | 2 | 5% | 30 of 42 | n | declines, correctly |
| `prioritized_arbiter` n=3 | 4 | 8 | 40% | 8 of 20 | 1 | succeeds at k=1 |
| `simple_arbiter_with_hints` n=4 | 2 | 14 | **64%** | **6 of 22** | 1 | **declines** |

For `arbiter` a k=1 projection discards 33 of 46 variables, so "the conjunction still equals `inv`"
is a strong statement, and it correctly fails there. For `simple_arbiter_with_hints` it discards 6 of
22 and keeps the other 16 in every conjunct, so recovering `inv` is close to automatic: `min_k = 1`
there means *almost nothing was projected away*, not that the invariant is per-client local.

The generalizer needs the stronger property, because at the target `N` the shared and bus-wide
monitors differ too and must themselves be generalized rather than carried along. When it tries,
`inv` for that family reconstructs at no bounded arity, and it declines — which is the correct
answer. There is no contradiction between the two results; `min_k` is simply the weaker test.

So: **`min_k` is informative when `shared_pct` is low and close to vacuous when it is high.** It was
never a sufficient condition for the generalizer to succeed, and §2's table should be read with the
`shared_pct` column beside it. The families where it did predict correctly all have low shared
fractions (`arbiter`, `arbiter_with_cancel`, `arbiter_on_inpchange`, `abcg_arbiter`,
`round_robin_arbiter`, all at 2-6%); the one case where a low `min_k` did not predict success is
`simple_arbiter_with_hints` at 60-71%.

Note the correlation is not a law: `amba_decomposed_lock` sits at 40-57% shared with `min_k = 1` and
the generalizer does succeed on it, closing n=15 and n=16. `shared_pct` says how much weight the
measurement can bear, not what the answer will be.

