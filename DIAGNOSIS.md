# Where & why ltlsynt beats acacia-bonsai — diagnostic findings

Branch: `optimize-vs-ltlsynt` (off `spot-fastpath-no-tlsf-tools`).
Data: SYNTCOMP-2024 `0s-20s`, 1011 shared instances, logs
`../acacia-bonsai/_bm-logs-top4-on-2024_20s/`. Acacia reference config
`best_decomp_mona` (plain `vector_backed` antichain + mona ios-precompute + decompose).
Loss set produced by `benchmarking/loss-set.py` → `loss-set-2024_20s.csv`.

## Headline numbers

| category | total | real | unreal |
|---|---|---|---|
| both solved | 647 | 379 | 268 |
| **ltlsynt-only (loss set)** | **192** | 108 | 80 |
| acacia-only | 25 | 21 | 3 |
| **acacia >2× slower (both solve)** | **64** | 28 | 36 |
| neither | 83 | — | — |

- Coverage: acacia 736 vs **ltlsynt 903** solved. PAR-2: acacia ~10127 vs **ltlsynt 4138** (~2.4×).

## Finding 1 — The posets data structures are NOT the lever (user hypothesis refuted)

Cross-backend coverage over the SAME suite (from the logs, no re-run):

- solved: vector **736** · kdtree 730 · sharingtrie 726 · base 724 · skiplist 711.
- **Union of ALL acacia backends (vector∪kdtree∪skiplist∪cst∪sharingtrie) = 741**, i.e. only
  **+5 over vector alone**.
- Of the 192 loss instances, a per-instance oracle over every backend rescues **5**; **187
  remain lost**. (Rescues: sharingtrie 4, kdtree/base a couple, mostly `simple_arbiter_with_hints6`,
  `finding_nemo_1`, `ltl2dba_Q7`, `round_robin_arbiter5`.)

⇒ Optimizing/replacing the antichain data structure can recover **≤5 / 192**. The gap is
algorithmic / pipeline, not data-structure. (Tuning the structures is still worth a little,
but it is not where ltlsynt's advantage lives.)

## The two bottlenecks (from an instrumented, verbose build — supersedes earlier guesses)

Built `build_exp` (best_decomp_mona flags, system Spot 2.15.1, `-DNO_VERBOSE` removed) and
read timestamped `-v1` stage markers. The concluding child exits via `exit()` so its verbose
flushes. Result: the antichain data structure is almost never the cost. Losses split into two
disjoint bottlenecks.

### Finding 2 — Translation blow-up (dominates the unrealizable / long-X losses)

Case `prioritized_arbiter_unreal12_16` (UNREALIZABLE; ltlsynt 0.00s / 18 MB, acacia 10.3s /
403 MB). Timestamped `-U -u automaton -v1`:
```
+ 0.00s  Formula: ... ((r_0 & Xr_1) -> XXXXXXXXXXXXXXXX(g_0 & g_1)) ...   <- 16 nested X
+10.28s  Spot NBA fast path classification: unsupported                  <- 10.28s in create_automaton()!
+10.30s  Make actions... ; Loop# 1..3, f of size 1                       <- fixpoint is trivial
+10.31s  UNREALIZABLE
```
- The **entire cost is `create_automaton()`**; the antichain fixpoint is ~0.01s (`f` size 1,
  3 loops). Earlier guess ("antichain on the dualized game") was **wrong**.
- The `-u both` default also forks the **formula** strategy, which adds `X` to the *formula*
  before translation → an even harder translation (`-U -u formula` > 35s TIMEOUT vs
  `-U -u automaton` 10.5s) on THIS instance. See the correction below though: `-u both` is
  still correct to keep (Finding 2b).
- This cluster owns 36 of 64 ">2× slower" (762×, 496×, 380×, 354×, 281×, 260×, 226×, all
  `*_unreal*`/counter specs, ltlsynt ~0.02s) and 80 of 192 timeouts.

#### Finding 2a — root cause is NOT `Small` vs `Any`; it's a missing pre-translation simplification

Chasing "why does ltlsynt avoid this translation cost" (user follow-up) overturned the
`Small`-minimization explanation:
- Timing `ltl2tgba` on the exact (non-negated) formula with acacia's settings (`-B --small
  --sbacc`) = **10.9s**; with ltlsynt's own `lar`-algo translator settings (`--generic
  --deterministic`, from `synthesis.cc:2540-2541`, no `Small`, no `SBAcc`) = **11.2s — just as
  slow.** So the output-preference knob is not what saves ltlsynt.
- `ltlsynt --realizability --verbose` on the same instance shows the actual mechanism:
  ```
  the following signals can be temporarily removed:
    r_0 := 1
    r_1 := 1
  new formula: GF!r_m -> (G(r_m -> X((!g_0&!g_1) U g_m)) & G(...& X^16(g_0&g_1)) & GFg_0 & GFg_1)
  translating formula done in 0.00092139 seconds     <- automaton has 1 state!
  ```
  This is **`spot::realizability_simplifier`** (`spot/tl/apcollect.{hh,cc}`, `SPOT_API` public
  class, `apcollect.cc:489`) — a formula-level, **realizability-preserving** simplification
  that detects input APs whose value can be forced to a constant without changing the
  verdict (options `polarity`, `global_equiv[_output_only|_moore]`), and rewrites the formula
  before any translation. `ltlsynt --bypass=no` (disabling the *other* fast path,
  `try_create_direct_strategy`) is still instant — the simplifier alone does the job, run
  unconditionally in `ltlsynt.cc:648` before `ltl_to_game`.
- **Directly confirmed on acacia's own construction**: feeding the *reduced* formula
  (`r_0,r_1` substituted) into `ltl2tgba -B --small --sbacc` (acacia's exact
  `create_automaton.hh` settings, unchanged) gives **2 states / 3 edges / 0.012s**, vs.
  **30 states / 233 edges / 10.6s** on the original — an **~880× speedup**, with zero change
  to acacia's translator preference.
- ⇒ **This is the real, high-confidence lever for the long-X/unreal cluster** (bigger and
  cleaner than the `Small`→`Any` tweak, and philosophically "acacia engine": acacia already
  links libspot for parsing/translation, and `realizability_simplifier` is a public, reusable
  Spot library API for formula preprocessing — not "delegate solving to ltlsynt").
- Caveat for implementation: `realizability_simplifier` returns a `mapping_t` of substitutions.
  For a pure realizability decision (acacia's `-r`/`-U -u *` paths) the mapping can be
  discarded. For full synthesis (`-s`, AIGER output) the forced APs must be reintroduced via
  the class's own `patch_mealy`/`patch_game` methods (`apcollect.hh:185-189`) so the emitted
  controller still declares/handles the removed input signals.

#### Finding 2b — `-u both` reconsidered: keep it (see backlog item 2)

### Finding 3 — Antichain-size explosion in the fixpoint (dominates realizable arbiter family)

Case `arbiter8` (8 in/8 out, realizable, times out). Timestamped `-r -v1`:
```
+0.17s  translation done (negated formula -> ~35-state automaton: translation is NOT the cost)
+0.53s  Loop# 2, f of size 256
+10.8s  Loop# 4, f of size 6561          <- antichain size explodes
+45s    UNKNOWN (timeout)
```
- Here translation is fast; the **K-bounded safety fixpoint's antichain `f` blows up**
  (1 → 256 → 6561 → …). This is why swapping antichain *backends* barely helps (Finding 1):
  the problem is the antichain **size** (inherent to the K-encoding for n-client arbiters),
  not the container's speed. (Correction: an earlier CLI `ltl2tgba --small` on the
  *non-negated* formula gave 5888 states/24.7s, but acacia negates on the real path and gets
  ~35 states in 0.17s — the CLI test used the wrong polarity.)
- Same family: arbiter6/7, abcg_arbiter3/4, arbiter_with_buffer/cancel, full_arbiter_enc*.
- Huge-alphabet specs (`amba_decomposed_lock150` 301 in, `lock200` 401 in) and many-output
  specs are a further sub-case to confirm (io-action enumeration / cpre union width).

#### Finding 3a — K-schedule tuning is RULED OUT for this cluster (tested, not assumed)

Isolated the true per-K cost on `arbiter6`/`arbiter7` by forcing `-M k -K k` (single fixed K,
no increment) for k=2..8, 30s cap each, real-only:
```
arbiter6:  K=2 0.7s(TO)  K=3 2.1s(TO)  K=4 6.1s(TO)  K=5..8 all 30s TIMEOUT (never converges)
arbiter7:  K=2 12.7s(TO) K=3..8 all 30s TIMEOUT
```
K=4 is cheap but insufficient (fixpoint converges but excludes init); **K=5 alone is already
intractable** (>30s just to reach *a* fixpoint, isolated from any increment overshoot).
⇒ No `DEFAULT_KMIN`/`DEFAULT_KINC` schedule can help — the minimal *sufficient* K for n≥6
independent-but-coupled clients is itself too expensive to compute. This is a genuine
algorithmic scaling wall in the K-bounded safety-game encoding for this spec family, not a
parameter-tuning problem. (Multi-K schedule trace for context: arbiter6 grinds K=5 for ~33s
down to a converged-but-insufficient `f=729`, forcing K=8, which then explodes
729→1359→2439→3879 in 4 loops and never converges in 150s.)

#### Finding 3b — the exploding antichain has heavy combinatorial/symmetric structure (evidence for a symbolic/BDD downset)

Dumped the actual antichain contents (not just size) via `-v -v` (level-2 verbosity prints the
full vector list after each `cpre` step, `k_bounded_safety_aut.hh:195`). On `arbiter5`'s
converged 743-vector antichain and `arbiter6`'s 2439-vector blowup, the vectors are **not**
high-entropy — they share almost all coordinates:
```
arbiter5 (743 vectors, 23 dims):
  { -1 4 4 4 4 4 3 3 3 3 3 -1 -1 -1 -1 -1 -1 0 0 0 0 0 -1 }
  { -1 4 4 4 4 3 3 3 3 3 4 -1 -1 -1 -1 -1 -1 0 0 0 0 -1 -1 }
  { -1 4 4 4 4 -1 3 3 3 3 4 -1 -1 -1 -1 -1 0 0 0 0 0 0 -1 }
  ...
arbiter6 (2439 vectors, 27 dims): identical pattern, scaled up
  { -1 4 4 4 4 4 4 3 3 3 3 3 3 -1 -1 -1 -1 -1 -1 -1 0 0 0 0 0 0 -1 }
  { -1 4 4 4 4 4 3 3 3 3 3 3 4 -1 -1 -1 -1 -1 -1 -1 0 0 0 0 0 -1 -1 }
  ...
```
Position 0 is always `-1`; a fixed block of positions is always `-1`; a fixed block is always
`0`; and the entire combinatorial blowup lives in a comparatively small run of positions taking
values in `{-1, 3, 4}` — literally "which subset of the (per-client) slots holds which value",
scaling combinatorially with the number of clients. Every element of the antichain differs from
its neighbors in only a handful of coordinates.

⇒ This is exactly the precondition for a symbolic (BDD) downset representation to pay off:
the blowup is *representational* (many near-duplicate/permutation-symmetric vectors an explicit
list must enumerate one-by-one) rather than *information-theoretically* large. A BDD over
bit-blasted counters, with a variable ordering that interleaves per-client bits, should be able
to share the common prefix/suffix and the combinatorial middle section in a canonical,
polynomial-size structure — something no explicit-antichain backend (`vector`/`kdtree`/
`skiplist`/`cst`) can do by construction, and consistent with Finding 1 (backend swaps don't
help: the problem is representation *class*, not implementation quality within that class).
Promotes backlog item 4 (data-structure tuning) from "low ceiling" to "worth a targeted
prototype, scoped to this cluster" — see updated backlog below.

#### Finding 3c — SPIKE RESULT: the BDD-compactness hypothesis (3b) is REFUTED (measured)

Built a standalone BuDDy spike (`/tmp/bddspike.cc`): encode the downward closure of the actual
dumped arbiter antichains as a BDD (per-coord counter bit-blasted, code = value+1), report
`bdd_nodecount` under coord-major ordering with dynamic sifting (near-optimal order). Ran on the
converged/last antichain for arbiter n = 3,4,5,6:

```
 n  antichain_vectors  sifted_BDD_nodes  nodes/vecs   vec_ratio  node_ratio
 3        45                211            4.69          -           -
 4       189                645            3.41         4.20        3.06
 5       743               2111            2.84         3.93        3.27
 6      2439               6257            2.57         3.28        2.96
```

- **Both grow exponentially at the same base (~3×/client).** `log(nodes)` is linear in `n` with
  a constant step (~1.1) — the exponential signature; a fit gives nodes ≈ 3.1ⁿ, vectors ≈ 3.3ⁿ.
- The BDD is a **constant ~2.5× *more* nodes** than the explicit antichain, and `nodes/vecs`
  (4.69→3.41→2.84→2.57) is converging to a constant (~2), **not** heading below 1.
- ⇒ A BDD gives **no sub-exponential representation** of this winning region. The "shared
  prefix/suffix + combinatorial middle" structure (3b) does **not** linearize under any variable
  ordering (coord-major and bit-plane both sift to ~the same size); the middle section is
  genuinely ~3ⁿ bits of information (which client holds which of ~3 counter values, under the
  mutual-exclusion + fairness constraints). **A BDD solver would push the OOM wall out by only a
  constant factor, not break the exponential.** Finding 3b's optimism was wrong once measured.

**Important caveat — a TIME win may still exist even without a size win.** The arbiter timeouts
are driven by the antichain's **quadratic** meet-closure ops (`union_with`/`intersect_with` ~
O(f²·d) on f in the thousands), whereas symbolic BDD CPre (`and`/`or`/`exist`) is ~linear in BDD
size. So at the *same* ~3ⁿ representation size, per-step cost could drop from ~O(9ⁿ) to ~O(3ⁿ) —
potentially rescuing the mid-size arbiters (arbiter7/8) that currently time out on operation
cost, though not the large ones (memory still ~3ⁿ). Confirming this requires building the actual
symbolic CPre (the expensive half of the spike) — **decision pending** (node-count half done).

Spike reusable at `/tmp/bddspike.cc` (+ dumped antichains `/tmp/arb{3,4,5,6}.vecs`).

#### Finding 3d — SPIKE RESULT: symmetry canonicalization collapses the arbiter antichain from exponential to LINEAR

Where a BDD failed (3c), symmetry succeeds. The arbiter clients are interchangeable, so the
winning region is invariant under the client-permutation group. Offline spike
(`/tmp/orbit_spike.py` + column-signature block analysis) on the dumped antichains: the coords
split into exactly **4 size-n blocks** (the 4 per-client states) + 3 shared singletons (= 4n+3),
and a client swap `(a,b)` permutes position `a↔b` in all 4 blocks at once. Result:

```
 n(clients)  antichain_vectors  valid client-transpositions  orbits(canonical reps)  collapse
 3               45                    3/3  (full S_3)              13                  3.5x
 4              189                    6/6  (full S_4)              20                  9.4x
 5              743                   10/10 (full S_5)              27                 27.5x
 6             2439                   15/15 (full S_6)              34                 71.7x
```

- **Full S_n is a genuine symmetry** (every client-transposition preserves the antichain set).
- Orbit count is **linear in n** (13,20,27,34 = 7n−8) while the raw antichain is ~3ⁿ. So
  canonicalizing counter-vectors under S_n makes the arbiter winning region **polynomial
  (linear!)** — the representation win the BDD couldn't deliver.
- ⇒ Sound symmetry reduction (winning region is Φ-invariant; canonicalize to orbit reps) should
  let acacia scale arbiters **linearly**, past the wall where BOTH acacia and ltlsynt currently
  die (ltlsynt itself is exponential here, walling at arbiter9→10). This is the go-signal for the
  symmetry-reduction build (detect+verify S_n from the spec via relabel+are_equivalent; canonical
  ize completed cpre iterates with orbit-aware domination; canonical tie-break for `-s` strategy).
  Caveat: this measures the antichain's own symmetry (an upper bound); the sound build uses
  spec-verified Φ ⊆ these — but here all client-transpositions verify, so they coincide.

## Implementation: `realizability_simplifier` + `Any` default (this round)

Both changes landed in `src/solver/create_automaton.hh` and `src/solver/solver_invoker.cc`,
built as `build_rs` (worktree binary; system Spot 2.15.1).

- **`ACACIA_TRANSLATION_PREF` default flipped `Small` → `Any`** (`create_automaton.hh`).
  Unconditional, no soundness dependency.
- **`spot::realizability_simplifier` added** in `run_one_ltl::operator()`
  (`solver_invoker.cc`), called on `spot_formula` right before the per-path
  transforms (X-insertion / negation) and `create_automaton`.

### ⚠️ Correctness bug found and fixed: the simplifier is UNSOUND under acacia's swapped unreal-check convention

First attempt applied the simplifier unconditionally (all paths). This **broke soundness**:
on `prioritized_arbiter_unreal12_16` (ground truth UNREALIZABLE), `-r` (real-check-only)
started returning **REALIZABLE** (wrong) instead of the correct inconclusive `UNKNOWN`. Root
cause: acacia's unrealizability check works by *swapping* which AP list is called "input" vs
"output" (`run_ltl`, before constructing the runner) and then applying either an X-insertion
(`UNREAL_X_FORMULA`) or a `push_aps` (`UNREAL_X_AUTOMATON`) reduction — a specific,
self-contained correctness argument. `realizability_simplifier`'s own soundness proof is for
the *classical* realizability question (matching exactly how `ltlsynt` uses it, on the
plain declared spec) and does not obviously compose with acacia's swap-based dualization.
Direct evidence: applying it to the swap-oriented formula for this instance collapsed the
formula to literal `1` (tautology), and the resulting fast-path verdict (`0` = "output player
does not win") **contradicted** the full antichain solver's own answer on the unsimplified
automaton (`1`) for the exact same question — an internal inconsistency, not just a slowdown.

**Fix**: scope the simplifier strictly to `not check_unreal.has_value ()` (the classical
real-check path only) — mirrors `ltlsynt.cc:648`'s own usage exactly. Also skipped when
`synth_fname.has_value ()` (an actual `-s` AIGER controller is requested), since the forced
APs would need `patch_mealy`/`patch_game` to be reintroduced into the emitted strategy, not
wired up here.

**Consequence**: this closes off the ~880× win for the *unreal* cluster specifically (most of
what Finding 2a measured) until the swap-compatible reduction is properly derived — flagged
below as a distinct, higher-risk backlog item rather than folded into the safe win.

**Correctness validated** (this scoped version): 120-instance sample (60 `realizable/` + 60
`unrealizable/`, ground truth from directory), `/tmp/correctness_check.py`: 85 solved
correctly, 31 timeouts (safe), 3 crash/no-match (reproduced identically on the pre-existing
`build_exp` baseline — not a regression), **zero wrong-polarity answers**.

**Final 3-way A/B** on the 106-instance regression set (45 `both_ok` + 36 unreal-slow + 25
unreal-timeout), clean/idle machine, 20s timeout:
```
build_exp (Small, no simplifier)        81 solved / 634s
build_any (Any,   no simplifier)        82 solved / 579s
build_rs  (Any,   RS scoped-to-real)    82 solved / 579s
```
`build_rs` == `build_any` **exactly** on this set (matches on every instance, incl. sub-0.1s
cases) — expected, since this set is dominated by the unreal cluster where the scoped
simplifier correctly does not fire; it's a validated no-op here, not a regression. All of the
measured win on this set comes from `Small`→`Any`. (Also: the earlier apparent "`ltl2dba_theta14`
Any regression" — `Any` timing out while `Small` solved in 18.9s — did **not** reproduce on
this clean/idle rerun: `Any` solved in 17.8-17.9s, i.e. equal-or-faster than `Small`. That was
pure CPU-contention noise from an earlier run competing with other load, not a real effect.)

## Quick acacia-vs-ltlsynt benchmark round (`build_rs`, this round's config)

Ran `build_rs` (Any + RS scoped-to-real) over the full **loss set (192) + slow set (64) = 256
instances**, 20s timeout, ltlsynt numbers reused from the cached `loss-set-2024_20s.csv`
(ltlsynt itself unchanged, no need to re-run it). Script: `/tmp/quick_bench.py` →
`/tmp/quick-bench-results.csv`.

```
Loss set (ltlsynt solved, OLD acacia didn't):     192
  NOW solved by new acacia (Any+scoped-RS):         6  (3.1%)
Slow set (both solved, OLD acacia >2x slower):     64
  faster with new acacia:                          55
  total time on slow set: old=190s  new=163s  (~14% reduction; new caps timeouts at 20s)
```

Newly-recovered loss instances: `ModdifiedLedMatrix5X`, `SensorPart`, `TwoCountersDisButA8/A9`,
`ltl2dba_beta6`, `ltl2dba_theta14`.

**Reading**: this round's two safe changes are a modest, real, validated improvement (6
timeouts turned into solves, 55/64 slow instances sped up, ~14% less total time on the slow
set) but do **not** close the big structural gap. The vast majority of the loss set (186/192)
is still unreached — confirming Findings 1/1b/3: the fixpoint-explosion arbiter family
(Finding 3) is untouched by either change, and the *unreal* translation-bound cluster (Finding
2a's ~880× case) is still blocked behind the swap-path soundness issue (backlog item 1b).

**Measurement caveat**: `TwoCountersGui` shows `old=OK/5.88s` (from the official cached
`best_decomp_mona` benchmark, built with `-flto`, verbose off) but `new=UNKNOWN/6.34s` on
`build_rs`. This is **not a regression from this round's code changes** — `build_exp` (no
`Any`, no simplifier, same worktree build config) shows the identical `UNKNOWN` in the 3-way
A/B above. It's a build-configuration artifact (missing `-flto` + verbose enabled in all three
worktree binaries) affecting a fork-race outcome on this one instance, not something the
patch touched.

## `-u formula` unreal-path extension: attempted, safe but not yet a win (in progress)

Per follow-up: is the same simplifier possible on the `UNREAL_X_FORMULA` strategy specifically
(as opposed to `UNREAL_X_AUTOMATON`)? Since acacia's own code comments this X-insertion step as
a "Mealy-to-Moore" conversion, and `spot::realizability_simplifier` has a
`global_equiv_moore` option precisely for Moore semantics (`apcollect.cc:591-596`; the default
`global_equiv` assumes Mealy — at most one input per equivalence class vs none for Moore), the
theory was: apply the simplifier **after** the X-insertion, with `polarity | global_equiv_moore`
instead of the default.

Implemented in `solver_invoker.cc` inside the `UNREAL_X_FORMULA` branch, options gated behind
`ACACIA_RS_UNREAL_FORMULA_OPTS` for easy override. Tested on the reference bug case
(`prioritized_arbiter_unreal12_16`, ground truth UNREALIZABLE):

- `-U -u formula`: went from a >35s TIMEOUT (no answer) to an instant `UNKNOWN` — **safe** (no
  wrong polarity), but not yet useful.
- Root cause (confirmed via verbose substitution log): the `polarity` chain is individually
  sound here — `r_0 := 0`, `r_1 := 0`, then `g_0 := 1`, `g_1 := 1`, then `r_m := 1`, correctly
  collapsing the formula step-by-step to a **tautology `1`**. The bug is downstream: a
  deterministic-Büchi automaton for `1` (one state, self-loop, always accepting) should mean
  the output player wins trivially (`current_output_player_wins = true`), but
  `spot_nba_fastpath.hh`'s fast path returns `0` (false) for it, which surfaces as inconclusive
  instead of the correct `UNREALIZABLE`.

### Chased, and correctly abandoned: this is NOT a fixable bug, it's an open theoretical question

Investigated `deterministic_forbidden_fast_path` (`spot_nba_fastpath.hh:363-392`) in detail.
`current_output_player_wins = false` for the universal (always-accepting) automaton is
**mathematically forced**, not a computation error: it treats `aut_forbid` as "the condition
the output player must eventually-always avoid"; a 1-state always-accepting self-loop
automaton triggers on *every* step of *every* play, so no strategy can ever avoid it — `false`
is the only consistent answer to that sub-question. Whether "output loses the avoid-game on
this un-negated, swapped, X-shifted automaton" correctly maps to "**original** spec is
UNREALIZABLE" depends on the specific game-theoretic correspondence acacia's swap-and-X-insert
reduction establishes (presumably proven in the underlying TACAS'23 paper) — not something
derivable with confidence from the code alone.

**Decisive evidence this needs its own careful derivation, not a guess**: the exact sibling
degenerate case — an **empty** (0-state) automaton on an unreal path — is already flagged in
the codebase itself (`solver_invoker.cc`, commit `57afe62f "fix: guard against empty automaton
in run_one_ltl"`) with the comment *"we cannot soundly map an empty language to UNREAL (see
issue #109 for the proper fast-path pre-check), so we return inconclusive there"*. The
original author, facing the mirror-image of this exact problem, explicitly chose the safe
conservative non-answer over guessing at the mapping. Given that precedent, and that I already
shipped one unsound guess earlier this session (Finding 2a's original unscoped attempt),
**I stopped here rather than hand-derive a fix**: the current behavior (safe `UNKNOWN`) is the
theoretically-appropriate conservative answer for a genuinely open question, not a bug to
patch. Revisit only with the actual paper's theorem in hand, or much more extensive validation
infrastructure than a few hand-picked instances can provide.

## FINAL FIX: sound up-front simplification (supersedes the `-u formula` attempt above)

Read the acacia paper (Cadilhac & Pérez, TACAS'23, arXiv:2204.06079) §2 + §5 "Checking
nonrealizability" to get the actual reduction, instead of guessing further. Confirmed via a
full code trace against the paper:

- **Realizability**: `BackwardRealizability(A)` is positive iff the output player can keep the
  play *out of* `L(A)` (payoff = complement of `L(A)`). The real path feeds `A(¬φ)`, so "avoid
  `¬φ`" = "enforce `φ`" = realizable.
- **Unrealizability (§5)**: uses **determinacy** — build `B` from **`¬φ` with inputs/outputs
  swapped and outputs pushed one step forward** (the `X`-shift / paper's Algorithm 3 =
  `push_aps`), so that a *positive* `BackwardRealizability(B)` ⟺ the **input player** wins ⟺
  the original spec is **unrealizable**. Crucially, the un-negated (swapped, shifted) formula
  is fed into an **avoid/dual** game — acacia's own code never literally negates on the unreal
  paths; the negation is implicit in the swap-and-shift construction.
- **Root cause of the earlier bug**: `spot::realizability_simplifier` preserves "can the
  OUTPUT player **enforce** this formula" (standard realizability). The real path is sound
  because it simplifies `φ` (what the system enforces) *in the standard frame*, before
  negating. My `-u formula` attempt simplified the swapped, `X`-shifted `φ'` directly — i.e. it
  answered "can output enforce `φ'`" when the game actually asks "can output avoid `φ'`"
  (equivalently enforce `¬φ'`) — the **wrong question**. That's why it spuriously collapsed a
  genuinely-UNREALIZABLE formula to `1`, and — confirmed by an exhaustive check — why it
  produced **2 actual wrong-polarity answers** (`simple_arbiter_10`,
  `simple_arbiter_enc_pb_8_pe_`: ground truth REALIZABLE, buggy build said UNREALIZABLE in
  0.01s).

**The fix**: simplify the **original spec `φ` once, in the standard frame** (original inputs as
inputs, default Mealy options `polarity | global_equiv`) — **before** the I/O swap, in `run_ltl`
— so every forked child (real + both unreal strategies) inherits the smaller formula.
**Soundness**: the simplifier preserves realizability of `φ`; by determinacy, "`φ` realizable"
⟺ "`φ` not unrealizable", so the same rewrite equally preserves the unrealizability verdict.
This is exactly how `ltlsynt` itself uses the class (`ltlsynt.cc:648`, once, up front, on the
plain declared spec).

**Implementation** (`src/solver/solver_invoker.cc`): removed both in-`operator()` simplifier
calls (the real-only one and the unsound `UNREAL_X_FORMULA` Moore-aware one); added a single
`spot::realizability_simplifier` call in `run_ltl`, right after parsing and before
`input_aps.swap(output_aps)`, gated on `not synth_fname.has_value()`.

### Validation (exhaustive, mandatory since this is a soundness fix)

- **Correctness gate**: ran the fixed binary over the **full** `tests/ltl/realizable/` (487) +
  `tests/ltl/unrealizable/` (415) = **902 labelled instances**, comparing every definitive
  verdict to the directory ground truth. **Result: 0/902 wrong-polarity answers** (685 solved
  correctly, 158 safe timeouts, 3 safe-inconclusive, 56 harness-level "no answer captured").
  The known bug instances now correctly time out (matching the *un-patched* baseline —
  confirmed `build_exp` also times out on both, ltlsynt says REALIZABLE) instead of falsely
  reporting UNREALIZABLE.
  - The 56 "harness" entries are **not** a regression: every one spot-checked (5+ instances,
    including 5 repeated runs of one) reproduces the **correct** answer in isolation, and the
    exact same behavior (an internal `std::bad_alloc`-and-recover path in acacia, unrelated to
    this patch) reproduces identically on the pre-patch `build_exp` baseline. This looks like a
    stale-process/memory-limit interaction across a long sequential batch in the test harness,
    not a code defect — flagged for awareness, not blocking.
- **Timing** (`/tmp/quick_bench.py`, 192 loss + 64 slow instances, 20s timeout, vs cached
  ltlsynt times):

  | | before this fix (unsound `-u formula` reverted) | **after this fix** |
  |---|---|---|
  | Loss set recovered | 6/192 (3.1%) | **71/192 (37.0%)** |
  | Slow set faster | 55/64 | **56/64** |
  | Slow-set total time | 190s → 163s (−14%) | **190s → 80s (−58%)** |

  A 12× jump in loss-set recovery over the unsound attempt, and now fully sound. Virtually all
  `*_arbiter_unreal*`/`prioritized_arbiter_unreal*`/`simple_arbiter_unreal*` instances that used
  to time out at 17-20s now resolve in **~0.01s**, matching ltlsynt's own ~0.02s — the long-`X`
  translation-blowup cluster (Finding 2/2a) is now closed via the sound route, not the
  abandoned fast-path guess.
- Remaining loss (121/192, mostly the fixpoint-explosion arbiter family, Finding 3/3a) is
  **unaffected**, as expected — that bottleneck is structurally different (antichain-size
  explosion, not translation cost) and this fix doesn't touch it.

## Optimization backlog (acacia-engine only; no new Spot reliance)

Ranked by (est. instances × confidence / effort):

1. **`spot::realizability_simplifier`, applied once up front on the original spec** (Finding
   2a, resolved) — ✅ **DONE: implemented, correctness-validated (0/902 wrong-polarity,
   exhaustive), and delivers the big win.** Superseded two earlier, narrower attempts
   (real-check-only scoping; an unsound `UNREAL_X_FORMULA`-specific extension) once the paper's
   §5 reduction showed the sound fix is simpler: simplify `φ` once, in the standard frame,
   before the I/O swap, so real + both unreal children all inherit it (sound by determinacy).
   **71/192 (37.0%) loss-set instances recovered**, slow-set time cut 190s→80s (−58%). See "FINAL
   FIX" section above for the full derivation and validation.
2. **`Small` → `Any` translation preference** — ✅ **implemented as the new default**
   (`create_automaton.hh`). **VALIDATED (small, zero-regression win).** A/B over 106
   instances (45 `both_ok` regression sample + 36 unreal-slow + 25 unreal-timeout), 20s
   timeout, no-leak runner:
   - Small **81 solved / 638s**  vs  Any **82 solved / 583s** (+1 coverage, −9% time).
   - Any recovered/sped **5** (OneCounterGuiA8, TwoCountersDisButA6/7/8/9). Any **regressed
     0** instances.
   - Does NOT help: fixpoint-bound arbiter timeouts, nor formulas where translation cost is
     inherent.
3. **Drop the `-u both` default to `-u automaton`** — ❌ **REFUTED, do NOT do this.** The two
   unreal strategies are *complementary*, not redundant: `-u automaton` alone loses instances
   where the `formula` strategy is the concluding path. Measured (`-u both` vs `-u automaton`,
   first 8 unreal instances): automaton-only **failed 4/8** — `LightsTotal_2c5b09da`
   (both 1.4s vs automaton TIMEOUT), `OneCounterGuiA6/A7/A8` (both <4s vs automaton UNKNOWN) —
   while matching on `TwoCountersDisButA3-6`. Keep `-u both`. (Note: once item 1 makes
   translation ~free, both unreal strategies become cheap to race anyway, further reducing the
   incentive to drop one.)
4. **Antichain-size explosion in the fixpoint** (Finding 3, realizable arbiter family, n≥6
   clients) — the **largest remaining gap** (most of the 121/192 loss still open after item 1).
   ❌ **K-schedule tuning ruled out (Finding 3a, tested not assumed):** forcing single fixed K
   values shows K=5 alone already exceeds 30s on `arbiter6` (K=4 is cheap but insufficient) —
   no `DEFAULT_KMIN/KINC` schedule can dodge this, the *minimal sufficient* K is itself
   intractable. Two concrete, ranked candidates now that Finding 3b shows *why* it's this bad:
   - **(a) Symbolic/BDD downset backend, scoped to this cluster** — ⭐ promoted this round.
     Finding 3b confirms the antichain's blowup is combinatorial/permutation-symmetric (near-
     duplicate vectors differing in a handful of coordinates), not information-theoretically
     large — exactly the case a BDD (bit-blasted counters, interleaved per-client variable
     order) can share compactly, unlike any explicit-list backend (Finding 1). Acacia already
     links a mature BDD engine (BuDDy via Spot) so the plumbing exists; the historical
     `sharingtree_backed`/`sharingtrie_backed` attempt underperformed on the *whole suite*, but
     that's an average-case argument this cluster-specific case is exempt from (measure
     narrowly against arbiter5/6/7/8 + prioritized/round_robin_arbiter, not the full suite).
     Not yet prototyped.
   - **(b) Backward boolean-state saturation** — cheaper, complementary, do first. Only
     *forward* saturation is implemented (`TODO` in `forward_saturation.hh:21`); the backward
     variant (ac+ paper) finds strictly more boundable states, shrinking the counting
     *dimensions* directly — reduces every vector's width before the antichain even forms,
     independent of representation. Much smaller change than (a); worth trying regardless of
     what a BDD prototype shows.
   - Lower-priority/uncertain: alternative input-picker order; exploiting arbiter
     symmetry/decomposition structurally (`DECOMPOSE_SPEC` doesn't apply since clients share
     the resource / aren't independent).
5. **Data-structure tuning (general, whole-suite)** — low ceiling (≤5 instances); deprioritize
   as a general *coverage* lever. Superseded for the arbiter cluster specifically by item 4a.

## Symmetry reduction: design + status (item 4a, in progress)

### Detection (done, committed — `src/solver/symmetry.hh`)

Detects and **structurally verifies** client-index-transposition automorphisms `(φ,π)` of the
game automaton: `π` permutes a pair of client indices across every indexed AP family (`r_i`,
`g_i`, …, found via `atomic_prop_collect`/name parsing); `φ` is a state permutation found by a
backtracking label-preserving isomorphism search `A → π(A)` (`π(A)` built via
`spot::relabel_here`) — necessary because the game automaton is **nondeterministic**, so a
simple forward-propagation match (my first attempt) fails. Every generator returned is verified;
unverified candidates are silently dropped (conservative — fewer symmetries is always sound).
Confirmed on the real automata: **full S_n detected on arbiter3..6** (3/3, 6/6, 10/10, 15/15
generators), matching the offline Finding-3d spike.

### The soundness subtlety that rules out a naive implementation

`f` (the antichain) is Φ-invariant **only** at the safe set and at full-CPre fixpoints — **never
mid-sweep**. acacia's `solve()` processes one input at a time (`k_bounded_safety_aut.hh`,
`cpre_inplace`); after processing an arbitrary subset of inputs, `f` is invariant only under the
subgroup fixing that subset, not full Φ. Canonicalizing a non-Φ-invariant `f` mid-loop is
**unsound both directions** (∪-canonicalizing over-approximates → false REAL; ∩-canonicalizing
under-approximates → false UNREAL). So "canonicalize `f` wherever convenient" is wrong; the
reduction has to be built into the CPre step itself.

### The sound, efficient algorithm (derived, not yet implemented)

**Lemma.** For Φ-invariant `f` and any single input `i`, let `T_i := ∪_o PreHat(f,i,o)` (union
over all outputs compatible with `i`). Then **`T_i` is `Stab(i)`-invariant** (the subgroup of Φ
fixing `i` setwise): for `ψ ∈ Stab(i)`, equivariance of `PreHat` gives
`ψ(T_i) = ∪_o PreHat(ψf,ψi,ψo) = ∪_o PreHat(f,i,ψo) = ∪_{o'} PreHat(f,i,o') = T_i` (the last step
because `ψ` permutes the output alphabet bijectively, so `{ψo}` ranges over all `o` again).

**Consequence.** For any set of coset representatives `{φ_1,…,φ_r}` of `Φ/Stab(i)` (r =
orbit-size of `i`, NOT |Φ|), `f_new := f ∩ ⋂_k φ_k(T_i)` is **Φ-invariant** (a standard fact:
intersecting `H`-invariant `X` over `G/H` coset representatives is `G`-invariant regardless of
representative choice), and equals the result of applying `CPre_{i'}` for **every** `i'` in the
orbit of `i`, batched. So: **compute `T_i` once per input-orbit representative, canonicalize it
under `Stab(i)`, then combine across the (polynomially-many) coset representatives** — never
materialize the full orbit or the full input alphabet.

**Concrete structure for arbiters.** Φ = S_n acting on client indices; for a boolean input `i`
(which clients currently request), `Stab(i)` is the **Young subgroup**
`S_{|requesting|} × S_{|not requesting|}` (permute requesting clients among themselves, and
non-requesting clients among themselves). Its cosets ↔ *which* subset of clients requests, for a
**fixed request-count** — so **input-orbits = "how many clients request"**, collapsing `2ⁿ`
raw inputs to **`n+1` orbit representatives**. `f` stays a canonical-rep antichain (polynomial,
per Finding 3d) throughout; only the existing per-`(i,o)` `PreHat`/actioner computation
(`actioners/standard.hh:93-121`, unchanged) needs to run once per orbit rep instead of once per
raw input.

**Scope of what's NOT yet handled**: this derivation folds symmetry into the **outer input
loop** only; the **inner union over outputs** (`T_i` itself) is still computed by full
enumeration over raw outputs compatible with `i` (as today). A parallel Young-subgroup argument
likely applies there too (outputs also have client structure) but is deliberately deferred —
implementing outer-loop-only first is lower-risk and still likely a major win (the outer loop is
what iterates until fixpoint; collapsing `2ⁿ`→`n+1` representative inputs is the dominant
saving). `-s` synthesis needs a canonical-representative tie-break to reconstruct a concrete
(symmetry-*breaking*) strategy from the quotient — deferred to after the decision-only path is
validated (task tracked separately; see plan file).

### Correction: naive coset-intersection over Φ/Stab(i) is NOT efficient

Re-deriving the batched-orbit step more carefully: `⋂_{coset reps of Φ/Stab(i)} φ_k(T_i)` is
algebraically equal to `⋂_{φ∈Φ} φ(T_i) = {v : orbit(v) ⊆ T_i}` (since `T_i` is `Stab(i)`-
invariant, redundant terms within a coset collapse). But testing "is `v`'s **entire** orbit
contained in `T_i`" by enumerating the orbit is exactly as expensive as the raw antichain —
orbit sizes are multinomial coefficients, back to ~3ⁿ. **The coset-representative idea by
itself does not avoid the blowup**; it needs to be paired with a genuinely compact
representation and a non-enumerative domination test (below).

### The actual polynomial representation: canonical count-vectors

Since the per-client block width `B` and the current bound `K` are fixed (don't grow with `n`),
there are only `r ≤ (K+2)^B` distinct **client value-tuple types**. Represent a canonical
(orbit-representative) vector not as `n` per-client values but as a **count-vector**
`c : types → ℕ`, `Σ c(t) = n` — "how many clients have each type". Two raw vectors are
S_n-orbit-equivalent **iff** they have the same count-vector (a standard multiset-orbit fact).
This is the actual polynomial representation (consistent with the measured linear orbit growth,
Finding 3d) — `r` is a small constant, so `c` has boundedly-many nonzero-capacity coordinates
regardless of `n`. Shared/singleton coordinates (outside all client blocks; Φ fixes them
pointwise) are carried alongside unchanged, ordinary coordinate-wise.

### Domination between count-vectors: exact, via max-flow (derived, provably polynomial)

`contains(T, v)` — is count-vector `v` dominated by some count-vector `u` in antichain `T`? —
reduces to **bipartite transportation feasibility**: left nodes = `u`'s types with supply
`u(t)`, right nodes = `v`'s types with demand `v(t')`, edge `(t,t')` allowed iff `t ≥ t'`
pointwise on the (fixed-length) type tuples; feasible iff max-flow = `n`. This is exactly "does
there exist a per-client pairing (a bijection matching `u`'s realized clients to `v`'s) such
that every paired client's `u`-value dominates their `v`-value" — the flow decomposition IS the
pairing. Polynomial in `r` (a constant), independent of `n`. Shared coordinates are AND-ed in
via a plain pointwise `≥` check.

`union_with` reduces to pairwise all-against-all filtering by the same `contains` test
(dominated count-vectors dropped) — no merge-enumeration needed, exact, polynomial.

### `intersect_with`: no exact polynomial characterization found — capped, SOUND under-approximation instead

The standard antichain identity `downset(A) ∩ downset(B) = downset({meet(a,b) : a∈A,b∈B})`
still holds, but for count-vectors the *set of achievable merge outcomes* from pairing two
type-distributions depends on **which** client-to-client pairing (transportation plan) is
chosen — unlike `contains` (pure feasibility), `intersect` needs the *maximal achievable*
merges, i.e. (a bound on) the extreme points of the transportation polytope between the two
distributions, which is not obviously polynomial in `r` in general (no rearrangement/exchange
argument found yet that bounds it).

**Resolution (chosen): a capped, honestly-incomplete but SOUND under-approximation.**
Key soundness fact: since acacia's fixpoint is a **greatest-fixed-point** (monotonically
shrinking) Kleene iteration, an `intersect` that only ever **omits** some valid merge outcomes
(never fabricates an invalid one) yields a result that is a **subset** of the exact intersection
at every step. Subset-of-true means: any point that survives to the end (in particular `init`)
is genuinely in the true winning region, so **every reached REALIZABLE/UNREALIZABLE verdict
stays sound** — the only cost is precision (the algorithm may spuriously shrink `f` too far and
fall back to `UNKNOWN`/an unnecessary K-increment where an exact computation would have
concluded). This is the same conservative posture already used elsewhere in acacia (e.g. the
empty-automaton special case in `solver_invoker.cc`), just applied to a new operation.

Concrete capped algorithm per pair `(c_u ∈ f.antichain, c_v ∈ f1i.antichain)`: (1) one **greedy
dominance-order matching** between the two type-distributions (always yields one valid, checkable
candidate merge, `O(r log r)`); (2) a **bounded number of local flow-swap perturbations** of that
matching (swap flow between two compatible type-pairs) to catch nearby maximal outcomes,
`O(r²)` additional candidates. Every candidate is constructed as an explicit, checkable
transportation plan, so validity (hence soundness) is trivial to assert per-candidate; only
*completeness* (finding literally every maximal merge) is sacrificed.

### Assembling the full CPre step: computing `T_i` for a representative input (derived, resolves the third gap)

Processing a representative input `i` (e.g. "exactly `k` of `n` clients request", `Stab(i) ≅
S_k × S_{n-k}`, a Young subgroup) needs `T_i = ∪_o PreHat(f,i,o)`, computed from `f`'s
**Φ-canonical** (unsplit) antichain. The subtlety: since `f`'s count-vectors don't record which
of their equal-typed clients are "the same physical client" from one orbit member to another,
and `PreHat` under a *fixed* (non-symmetric) `i` is only `Stab(i)`-equivariant (not
Φ-equivariant, per the derivation above), **different ways of assigning `u`'s counted clients
into `i`'s REQ/NREQ groups can give genuinely different `PreHat` results** — there is no single
canonical "the" split.

**Resolution: reuse the exact soundness trick that already validated `intersect_with`.**
`T_i` only ever appears as `f ∩ T_i`. If `T_i` is under-approximated (a genuine subset of the
true `T_i`, never containing a spurious point), then `f ∩ T_i_approx ⊆ f ∩ T_i_true` — the same
safe direction. So `T_i` is built from a **bounded set of candidate REQ/NREQ split
assignments** per canonical `u` (e.g. sorted-highest-dominance-rank-to-REQ,
sorted-lowest-to-REQ, a couple more — the same "few sort-key orientations" idea as
`intersect_with`'s Northwest-corner candidates): realize each split as one concrete raw vector,
apply the **existing, unmodified** `actioners::standard::apply` (reusing the already-correct
`PreHat` machinery verbatim — no new game semantics to re-derive or re-validate), convert the
raw result back to a Φ-canonical count-vector via `to_count`, and union everything. Every
candidate is a genuinely achievable raw point by construction, so soundness is structural, not
tested-in, exactly like `intersect_with`.

**Getting back to Φ-canonical form is free, not lossy.** The REQ/NREQ tag used while building a
candidate is bookkeeping tied to *which representative input* was used — it is never part of
the raw vector itself. So merging same-type counts back together (forgetting which were "REQ"
for this step) is an **exact, lossless projection** back to the unsplit canonical form, not an
approximation — `to_count` applied directly to the raw `PreHat` result already produces this
form with no separate "split representation" data structure needed at all.

**The resulting algorithm** (per representative input `i`, replacing one `cpre_inplace` call):
```
T_i_candidates = {}
for u in f.antichain:
  for split in bounded_candidate_splits(u):        # e.g. 2-4 sorted-rank orientations
    raw_u = realize (u, block_layout, split)         # one concrete raw vector for this split
    for o in outputs compatible with i:
      raw_result = actioner.apply (raw_u, action_for (i, o), backward)   # EXISTING, unmodified
      T_i_candidates.push (to_count (raw_result, block_layout))
T_i = union_with (T_i_candidates, {})                # exact, already validated
f_new = intersect_with (f, T_i)                       # capped-sound, already validated
```
repeated over the representative inputs (one per input-orbit, e.g. `n+1` "how many clients
request" categories for the arbiter family) until none change `f` — mirroring `solve()`'s
existing outer loop shape, just over representatives instead of raw inputs and count-vector `f`
instead of a raw antichain. Sound by composing two already-validated under-approximation
arguments (candidate-split `T_i` construction, `intersect_with`); the overall GFP
monotone-shrinking argument from `intersect_with`'s derivation extends unchanged: `f` can only
end up smaller than the true fixpoint, so any DEFINITIVE verdict reached remains sound.

### Status

Detection (`symmetry.hh`): done, committed (`57684512`, `146ee391`), verified on real
nondeterministic arbiter automata (full S_n, 3/6/10/15 generators for n=3..6). Domination algebra
(`symmetric_downset.hh`: exact `contains` via max-flow, exact `union_with`, capped-sound
`intersect_with`): done, committed (`74ab4708`, `be2b1a12`), validated with thousands of
randomized cross-checks against brute-force enumeration, 0 unsound points across all trials
(single-pair and multi-element-antichain shapes). Block/slot extraction (`symmetric_blocks.hh`):
done, committed (`45205be5`), validated synthetically (24 configurations) AND against the real
arbiter automata (blocks=4, shared=3 for n=3..6, matching the independent offline
column-signature analysis exactly). Raw-vector <-> count-vector conversion
(`symmetric_conversion.hh`: `to_count_vector`/`realize`/`candidate_split_keys`): done, committed
(`06e48b37`), validated with round-trip tests over synthetic layouts. All four pieces pass
together under `meson test --suite symmetry`.

**Algorithm design is complete end-to-end** (detection → domination algebra → block extraction →
conversion → the full-CPre-step assembly derived just above). Every layer is independently unit
tested. **What's NOT yet done is wiring this into acacia's live pipeline** — a different kind of
work (acacia-specific `ios_precomputer`/`actioner` plumbing, not further algorithm derivation),
checkpointed here deliberately rather than rushed. Concretely, resuming needs:

1. **Representative-input + compatible-output construction.** For each of the ~`n+1` orbit
   representatives ("exactly `k` of `n` clients request"), build one concrete raw input `bdd`
   from `group.families`/`group.indices` (e.g. via `bdd_ithvar` on the first `k` client APs'
   variables), and find its compatible outputs. Reuse patterns from
   `src/ios_precomputers/standard.hh` (`get_next_letter`/output enumeration) and
   `src/actioners/standard.hh` (`compute_action_vec`) — read these fully before implementing;
   they weren't explored in this session beyond the backward-step formula already reused
   verbatim in the design (`actioners/standard.hh:93-121`).
2. **The symmetric solve loop**, mirroring `k_bounded_safety_aut::solve()`/`cpre_inplace()`
   shape but over: `f` as a `vector<symmetric_downset::count_vector>` (not a `SetOfStates`),
   representative inputs instead of picked raw inputs, and the `T_i` construction algorithm
   derived above (bounded candidate splits × `actioner.apply`, unioned, then
   `intersect_with`). Needs the K-increment step re-derived for count-vector form (shift
   counting-type coordinates by `kinc` — should be a direct map over `count_vector::counts`
   keys, no new algebra) and the `contains(f, init)` convergence test (`symmetric_downset::
   contains`, already exact).
3. **Dispatch**: in `solve_game` (`src/solver/solve_game.cc`), call `symmetry::detect` +
   `compute_block_layout` on the automaton; if a usable layout is found (and, for now, no
   `-s`/synthesis requested — strategy lifting is Phase 2, not attempted), route to the new
   symmetric solver; otherwise fall through unchanged to the existing antichain path. Gate
   behind a flag so the default build is untouched.
4. **Validation** (mandatory, same gate that validated every fix this session): the full
   ltlsynt-oracle corpus (`tests/ltl/{realizable,unrealizable}`, zero wrong-polarity required)
   before any performance claim; then arbiter-cluster timing (`arbiter5..10`,
   `prioritized_/simple_/round_robin_arbiter*`) to measure the actual win.

No decision-vs-synthesis correctness issue is expected to be introduced by the plumbing itself
(the algebra is already sound); the risk in this phase is purely mechanical (misreading an
existing precomputer's data shape, an off-by-one in state/AP indexing) — exactly the kind of
mistake the oracle gate in step 4 is designed to catch before anything is trusted.

### Resume update — first live-pipeline wiring pass

Implemented an **opt-in, decision-only** live symmetry path behind
`ACACIA_ENABLE_SYMMETRIC_SOLVER` (default `0`, so normal builds are untouched):

- `symmetric_blocks.hh`: `block_layout` now carries `slot_to_index`, matching recovered
  automaton-state slots back to AP-level client indices via the same transposition stabilizer
  signatures. This is required so representative input BDDs and realized counter-vectors talk
  about the same physical client slots. Synthetic block/conversion tests were updated.
- `symmetric_k_bounded_safety_aut.hh`: new experimental solver header. It builds
  representative input letters from indexed input-family count distributions plus shared-input
  assignments, enumerates compatible raw output letters using the existing `actioners::standard`
  `PreHat` machinery, converts raw predecessors back to count-vectors, and runs the count-vector
  GFP loop. Synthesis is deliberately unsupported; `solve_game.cc` falls back to the existing
  solver unless the symmetric path proves a win.
- Added explicit work caps (`ACACIA_SYMMETRY_MAX_PRE_WORK`, `ACACIA_SYMMETRY_MAX_TI_SIZE`) so the
  opt-in prototype yields to the classic solver instead of monopolizing a run when `T_i`
  construction grows too large.

Validation so far:

- Default `build_rs` (symmetry path disabled) compiles.
- Opt-in `build_sym` (`-DACACIA_ENABLE_SYMMETRIC_SOLVER=1`, tests enabled) compiles.
- `meson test -C build_sym --suite symmetry`: 4/4 pass.
- Smoke: `arbiter_pb_5_pe_`, `-r -v -K 8`, detects full `S_5`, builds 6 representative input
  orbits, then hits the `T_i` work cap and falls back; the classic solver returns `REALIZABLE`
  and the wrapper passes.

Important result: the remaining integration bottleneck is **not** symmetry detection, block
layout, representative input construction, or dispatch. It is the **inner `T_i = union_o
PreHat(f,i,o)` construction**: even with only one representative input orbit, raw output
enumeration plus candidate split realizations can create enough count-vector candidates that the
current exact incremental union/intersect path is too expensive. The next useful step is to
apply a Young-subgroup/orbit argument to outputs too, or otherwise make `T_i` construction
non-enumerative/strongly capped without losing all precision. Raising the caps merely recreates
the earlier timeout; it is not a real fix.

### Resume update — `union_o` output-orbit spike

Implemented a default-off benchmark/diagnostic spike behind
`ACACIA_SYMMETRY_UNIONO_SPIKE` (requires `ACACIA_ENABLE_SYMMETRIC_SOLVER=1`). It does **not**
change the solver result path: the live solver still computes `T_i` from the raw compatible
outputs and falls back exactly as before. The spike computes, logs, and unit-tests the
stabilizer-aware output representatives that a later optimized `union_o` can use.

- For each representative input, the code derives the Young-subgroup buckets induced by the
  input count-vector, then enumerates one output assignment per orbit instead of every raw
  output letter.
- The diagnostic reports raw letters/actions, representative letters/actions, estimated raw vs.
  representative work, a small hybrid action budget, and the size/cap status of the
  representative-only `T_i`.
- Added `symmetric-output-orbits-test`, covering the output-representative count formula,
  cap behavior, and concrete representative assignments.

Validation:

- `meson compile -C build_rs` passes with the symmetry path disabled.
- `meson compile -C build_sym` passes with
  `-DACACIA_ENABLE_SYMMETRIC_SOLVER=1 -DACACIA_SYMMETRY_UNIONO_SPIKE=1`.
- `meson test -C build_sym --suite symmetry`: 4/4 pass.
- Smoke: `arbiter_pb_5_pe_`, `AB_OPTS='-r -v -K 8'`, returns `REALIZABLE` through the classic
  fallback and emits the expected compression signal:
  ```
  raw_actions=32 rep_actions=6  raw_work=128  rep_work=24  rep_Ti=6
  raw_actions=32 rep_actions=10 raw_work=768  rep_work=240 rep_Ti=5
  raw_actions=32 rep_actions=12 raw_work=1152 rep_work=432 rep_Ti=8
  ```

This confirmed the right next implementation target: replace or augment raw output enumeration
inside `union_o` with the stabilizer-aware representatives, plus a bounded fallback path for
precision.

### Resume update — optimized representative `union_o` first pass

Implemented the first behavioral use of the output representatives. The optimization is still
only reachable through the opt-in symmetric solver (`ACACIA_ENABLE_SYMMETRIC_SOLVER=1`), and is
controlled separately by `ACACIA_SYMMETRY_OPTIMIZE_UNIONO` (default `1`). Normal builds still
compile with the symmetric solver disabled by default.

- Representative output actions are now built when either `ACACIA_SYMMETRY_OPTIMIZE_UNIONO` or
  `ACACIA_SYMMETRY_UNIONO_SPIKE` is enabled.
- In the symmetric solve loop, `union_o` uses `meta->output_rep_actions` whenever the
  stabilizer-aware representatives are complete. If representative generation is unavailable or
  capped, it keeps the old raw-output path. If the representative pre step itself exceeds the
  work/Ti caps, the quotient solver returns `nullopt` and the classic solver fallback runs.
- Spike diagnostics now reuse the representative `T_i` they compute for logging, so a spike
  build no longer runs the same representative pre step twice.
- Added an aggregate quotient-pre budget, `ACACIA_SYMMETRY_MAX_TOTAL_PRE_WORK` (default `8192`,
  `0` disables it), so a sequence of individually permitted representative steps cannot consume
  an entire benchmark timeout before fallback.

Validation:

- Default `build_rs`: compiles.
- `build_sym` with `-DACACIA_ENABLE_SYMMETRIC_SOLVER=1`: compiles and
  `meson test -C build_sym --suite symmetry` passes 4/4.
- `build_sym` with
  `-DACACIA_ENABLE_SYMMETRIC_SOLVER=1 -DACACIA_SYMMETRY_UNIONO_SPIKE=1`: compiles and
  `meson test -C build_sym --suite symmetry` passes 4/4.
- Smoke with default caps (`arbiter_pb_5_pe_`, `AB_OPTS='-r -v -K 8'`) now actually uses
  representative outputs: it gets past the previous raw cap, increments from K=2 to K=5, then
  falls back at loop 3 when representative work reaches `rep_work=1344` (the comparable raw work
  would be `raw_work=3584`). The classic fallback returns `REALIZABLE` and the wrapper passes.
- Before adding the aggregate budget, deliberately raising the per-step caps to
  `ACACIA_SYMMETRY_MAX_PRE_WORK=8192` and `ACACIA_SYMMETRY_MAX_TI_SIZE=512` let the quotient loop
  advance to K=8/loop 6, but the smoke then hit a 90s timeout. With the aggregate budget in
  place, the same larger-cap experiment falls back at K=8/loop 4 (`aggregate quotient work budget
  exceeded`) and the wrapper still returns `PASS`. So simply raising per-step caps is ruled out,
  and the aggregate cap is the guard that keeps larger experiments bounded.

Current conclusion: output-orbit representatives are soundly detected and used, and they remove
the first raw `union_o` wall. The remaining blocker is no longer raw output enumeration alone;
once the cap is raised, the quotient fixpoint/intersection work at higher K can still run too
long. The next implementation target should be a stronger `T_i`/intersection approximation, not
a larger per-step cap.

### Resume update — stronger count-vector union/intersection pass

Implemented the next implementation pass in the count-vector downset operations used by the
opt-in symmetric solver:

- `union_with` now builds the maximal antichain incrementally with `add_maximal` instead of
  materializing all candidates and then pairwise-filtering the whole vector. This keeps
  intermediate `T_i`/`f` sets smaller during representative `union_o` construction.
- `intersect_with` now explores a bounded set of concrete Northwest-corner transportation plans
  rather than a single ordering. It includes total-value ascending/descending orientations plus
  per-client-coordinate orientations, capped by `ACACIA_SYMMETRY_INTERSECT_MAX_PLANS` (default
  `20`). Each candidate is still induced by a valid explicit transportation plan, so the
  operation remains a sound under-approximation: it may miss valid maximal meets, but it does
  not invent invalid ones.

Validation:

- `build_sym` with `-DACACIA_ENABLE_SYMMETRIC_SOLVER=1 -DACACIA_SYMMETRY_UNIONO_SPIKE=1`
  compiles.
- `meson test -C build_sym --suite symmetry`: 4/4 pass.
- `symmetric-downset-test`: 0 unsound points in 400 single-pair trials and 80
  multi-element-antichain trials; informational single-pair completeness improved to 524/685
  brute-force points found.
- Default/non-symmetric `build_rs` still compiles.
- A no-spike opt-in build (`-DACACIA_ENABLE_SYMMETRIC_SOLVER=1`) also compiles and passes the
  symmetry suite.

Smoke result on `arbiter_pb_5_pe_`, `AB_OPTS='-r -v -K 8'`: the quotient path still falls back
to the classic solver and the wrapper returns `REALIZABLE`, but the strengthened approximation
shrinks the last reached loop before fallback (`f` at loop 3 drops from 11 to 8). It now caps at
`rep_work=1056` versus comparable `raw_work=2816`. Temporarily raising
`ACACIA_SYMMETRY_MAX_PRE_WORK` to `1536` advanced to loop 4 (`f=22`) before fallback, but made
the smoke slower; the default cap should stay conservative for benchmarking.

Current conclusion after this pass: the optimized `union_o` path is benchmarkable and guarded
well enough to compare against mainline. It is not yet expected to dominate the arbiter cluster,
because the quotient still often yields to the classic solver before proving the instance.

### Benchmark checkpoint — targeted TLSF arbiter subset vs `origin/master`

Built two release/LTO benchmark binaries with comparable `best_decomp_mona` settings:

- Baseline: `origin/master` at `593a9d8e`, built in `/tmp/acacia-master-bench/build_bench_main`.
- Branch: `optimize-vs-ltlsynt`, built in `build_bench_sym` with
  `-DACACIA_ENABLE_SYMMETRIC_SOLVER=1` and diagnostic spike logging off.

Benchmark runner: `benchmarking/run-tlsf.py`, translating TLSF via syfco before timing acacia,
running each solver invocation in its own process group, and killing the whole process group on
timeout. The benchmark job itself was wrapped in a user-systemd cgroup after the OOM concern:
`MemoryMax=8G`, `MemorySwapMax=0`, with acacia's own `-l 4` also passed. Flags were
`-r -K 8`, timeout `30s`, selected TLSF corpus list `/tmp/tlsf-arbiter-target.list`.

Result on 18 selected arbiter-family instances:

```
mainline: 11/18 solved, total solver time 210.965s
branch:   11/18 solved, total solver time 211.562s
verdict/result mismatches: 0
```

Hard timeout set was unchanged: `arbiter_pb_{6,7,8}`, `abcg_arbiter_pb_{3,4,5}`, and
`round_robin_arbiter_pb_4` timed out on both binaries. Small arbiter cases show the expected
prototype overhead before the quotient path either proves or falls back (`arbiter_pb_3`: 0.007s
mainline vs 0.162s branch; `arbiter_pb_4`: 0.028s vs 0.307s; `arbiter_pb_5`: 0.568s vs 0.702s).

Conclusion: the optimized representative-`union_o` path is now benchmarkable and bounded, but it
does **not** yet improve coverage or time against mainline on the target TLSF arbiter subset.
The next implementation pass needs to reduce quotient overhead/fallback at K>=5 before a broader
full-corpus benchmark is likely to show a positive signal.

### Resume update — dense/SIMD symmetry downset adapter

Implemented the first serious SIMD-aware infrastructure pass for the symmetry pipeline, reusing
the existing `posets` submodule where it matches the semantics:

- Added `symmetric_dense_downset.hh`, a dense adapter over the existing sparse
  `symmetric_downset::count_vector` representation. It interns client type-tuples into compact
  type IDs, stores shared/count arrays in `posets::utils::vector_mm` for aligned storage, uses
  `std::experimental::simd` for shared-coordinate dominance/min, and precomputes type-dominance
  and type-meet tables per operation.
- The live quotient loop now routes `union_with` and `intersect_with` through the dense adapter
  behind `ACACIA_SYMMETRY_DENSE_SIMD` (default: same as `ACACIA_ENABLE_SYMMETRIC_SOLVER`). The
  external solver state remains the sparse count-vector type for conservative integration.
- Stock `posets::Downset::intersect_with` is deliberately **not** used for the symmetry path:
  symmetry intersection is a bounded set of transportation-plan candidates, not a single
  componentwise meet. `posets` is used for aligned/SIMD storage, not for owning the orbit
  semantics.
- Added profiling hooks behind `ACACIA_SYMMETRY_PROFILE=1`, with buckets for detection, block
  layout, representative I/O setup, output representative construction, actioner build,
  `pre_for_input`, realization, action apply, count conversion, dominance/union, intersection,
  K-increment union, and total quotient solve time. The profiling build was compile-checked and
  `build_sym` was restored afterward.
- Added `ACACIA_SYMMETRY_USE_POSETS_UNION` as a default-off gate for future experiments with a
  posets-backed exact union adapter. It is not used in the live path yet because the custom dense
  antichain currently preserves the symmetry-specific control we need.

Validation:

- New `symmetric-dense-downset-test` passes: dense dominance and union are exact-equivalent to
  the sparse implementation; dense intersection passed 300/300 brute-force soundness checks.
- `meson compile -C build_sym`: passes with dense path and spike diagnostics.
- `meson test -C build_sym --suite symmetry`: 5/5 pass.
- No-spike opt-in build (`-DACACIA_ENABLE_SYMMETRIC_SOLVER=1`): compiles and the symmetry suite
  passes 5/5.
- Default/non-symmetric `build_rs`: compiles.
- Arbiter smoke (`arbiter_pb_5_pe_`, `AB_OPTS='-r -v -K 8'`) still falls back safely and returns
  `REALIZABLE`; dense path preserves the same loop/fallback shape as before this pass.

Current limitation: `pre_for_input` still realizes raw vectors and converts them back to sparse
count-vectors before dense union/intersection. The next performance pass should attack that
conversion/action-apply boundary: cache realized raw buffers and then, if profiling confirms it,
compile direct dense histogram predecessor transforms that bypass raw vectors entirely.

### Resume update — dense intersection hot-path pass

Profiled the dense/SIMD quotient path on the bounded `arbiter_pb_5_pe_` smoke
(`AB_OPTS='-r -v -K 8'`, profile build). Before this pass the quotient setup was no longer
detection-bound:

```
pre_for_input=121.386ms/12
realize=0.932814ms/300
action_apply=3.16466ms/2776
count_conversion=7.19576ms/2776
dominance_union=109.095ms/2776
intersect=355.593ms/11
solve_total=479.85ms/1
```

Implemented the measured-positive pieces:

- Dense intersection now pre-sorts each dense vector's support once per merge plan instead of
  re-sorting inside every `(u, v, plan)` northwest-corner merge.
- Dense intersection now uses cheap pair pruning: if an existing intersection candidate already
  dominates either side of a pair, all pair-generated meets are dominated and skipped; if the pair
  is comparable, it inserts the smaller side directly rather than enumerating every merge plan.
- `candidate_split_keys()` now returns a static array of function pointers instead of rebuilding a
  `std::vector<std::function<...>>` on every pre call.
- Added `realize_into()` and reused the raw vector/type-expansion scratch in `pre_for_input`.
- `to_count_vector()` now reuses one client-type tuple buffer per conversion instead of allocating
  a tuple per client slot.

Final retained profile run on the same smoke:

```
pre_for_input=125.534ms/12
realize=0.741139ms/300
action_apply=3.36189ms/2776
count_conversion=6.28494ms/2776
dominance_union=114.091ms/2776
intersect=293.759ms/11
solve_total=423.153ms/1
```

The immediate post-intersection run was faster (`intersect=264.915ms`, `solve_total=376.372ms`),
so the single-smoke timing has visible noise, but the intersection bucket consistently moved down
from the original ~356ms. A batched `pre_for_input` union experiment was tried and backed out: it
reduced dominance-union calls from 2776 to 89, but the dense batch rebuilds were heavier and
regressed total quotient time (`solve_total=394.721ms` vs. 376.372ms on the comparable run).

Validation:

- `meson compile -C build_sym`: passes with profiling enabled during the pass.
- `meson test -C build_sym --suite symmetry`: 5/5 pass.
- Arbiter smoke still falls back safely and returns `REALIZABLE`.

### Classic-solver profile baseline

Added compile-gated profile buckets for the classic K-bounded safety loop behind
`ACACIA_SYMMETRY_PROFILE=1`. These buckets do not change the default build. The
`classic_backward_apply` bucket is nested inside `classic_pre_build`, so its time is also counted
in the enclosing pre-build bucket.

Profile build: release `build_rs` flags plus `-DACACIA_SYMMETRY_PROFILE=1`, with tests enabled.
Small AMBA decomposed arbiter runs:

```
amba_decomposed_arbiter_2:
  classic_backward_apply=0.02868ms/556
  classic_pre_build=0.048576ms/12
  classic_intersect=0.005845ms/12
  classic_solve_total=0.4256ms/1

amba_decomposed_arbiter_3:
  classic_backward_apply=0.219761ms/4832
  classic_pre_build=0.356619ms/19
  classic_intersect=0.052639ms/19
  classic_solve_total=2.13078ms/1

amba_decomposed_arbiter_4:
  classic_backward_apply=2.98338ms/56528
  classic_pre_build=4.39123ms/33
  classic_intersect=0.843042ms/33
  classic_solve_total=19.2115ms/1
```

Validation:

- `meson compile -C build_rs`: passes with `ACACIA_SYMMETRY_PROFILE` disabled.
- `meson setup build_prof ... -DACACIA_SYMMETRY_PROFILE=1`: configured with release flags and
  tests enabled.
- `meson compile -C build_prof`: passes.
- Profile Meson tests for `ab/amba_decomposed_arbiter_{2,3,4}.ltl`: all pass.

### Equivariant solver validation and benchmark

Implemented the exact equivariant solver behind `ACACIA_ENABLE_EQUIVARIANT_SOLVER=1` and kept
the default build path unchanged. The implementation declines up front unless symmetry detection
finds a verified full symmetric group, the induced block layout is available, every generator
matches the layout transposition exactly, boolean/counting coordinates are not mixed by any
generator, and all input orbits/output letters fit the structural caps. When those checks pass,
the solver keeps the classic raw downset representation and computes one exact backward union per
input-letter orbit representative, then permutes that result across the orbit members.

Full `ab` oracle comparison, run serially (`-j1`) to avoid local memory pressure:

```
baseline build_e1_rs: 624 rows, 551 OK, 71 TIMEOUT, 2 FAIL3
equivariant build_eq: 624 rows, 551 OK, 71 TIMEOUT, 2 FAIL3
ordered outcome mismatches: 0
multiset missing/extra rows: 0
shared FAIL3 cases: ltl2dba_R_10.ltl, ltl2dba_R_12.ltl
```

Saved logs:

- `/tmp/acacia-e1-baseline-testlog.txt`
- `/tmp/acacia-e1-equivariant-testlog.txt`

AMBA decomposed arbiter behavior check (`AB_OPTS='-v'`, 40s cap, baseline `build_e1_rs` versus
equivariant `build_eq`):

| n | Baseline | Eq | Eq behavior |
| ---: | ---: | ---: | --- |
| 2 | 0.013s | 0.012s | declined: not a verified full symmetric group |
| 3 | 0.015s | 0.015s | declined: not a verified full symmetric group |
| 4 | 0.034s | 0.035s | declined: not a verified full symmetric group |
| 5 | 0.496s | 0.518s | declined: not a verified full symmetric group |
| 6 | 12.384s | 14.306s | declined: not a verified full symmetric group |
| 7 | timeout | timeout | timeout |
| 8 | timeout | timeout | timeout |
| 10 | timeout | timeout | timeout |
| 12 | timeout | timeout | timeout |

This is a negative but useful result: the `amba_decomposed_arbiter_*` LTL family does not exercise
the equivariant solver under the current exact full-symmetry checks. Small instances decline
before solving; larger instances still hit the cap in both builds. The plan's expected
"eq solves amba_decomposed" shape was therefore wrong for this corpus slice.

Timing benchmark used a release/LTO pair based on the `self-benchmark.sh` best-powset settings
(`-DDECOMPOSE_SPEC=0`, `-Ofast -flto -fuse-linker-plugin`, tests disabled). The equivariant build
adds only `-DACACIA_ENABLE_EQUIVARIANT_SOLVER=1`. Runner:
`python3 benchmarking/run-tlsf.py`, list `/tmp/acacia-e3-arbiter-tlsf.list`, corpus
`tests/ltl/realizable/*arbiter*.tlsf`, timeout `30s`, each solver invocation in its own process
group. No solver processes remained after either run.

Summary:

```
baseline build_e3_base: 31/67 solved, 36 timeouts, total 1111.974s
equivariant build_e3_eq: 31/67 solved, 36 timeouts, total 1106.300s
verdict/result mismatches: 0
timeout-set changes: 0
largest solved-instance gain: full_arbiter_5.tlsf, 16.652s -> 11.436s
largest solved-instance regression: prioritized_arbiter_enc_4.tlsf, 3.422s -> 3.682s
```

Per-instance table (`R` = realizable, `TO` = timeout):

| Instance | Baseline | Eq | Delta |
| --- | ---: | ---: | ---: |
| `amba_decomposed_arbiter_2.tlsf` | R 0.011s | R 0.008s | -0.003s |
| `amba_decomposed_arbiter_3.tlsf` | R 0.008s | R 0.008s | +0.000s |
| `amba_decomposed_arbiter_4.tlsf` | R 0.024s | R 0.026s | +0.002s |
| `amba_decomposed_arbiter_5.tlsf` | R 0.117s | R 0.111s | -0.006s |
| `amba_decomposed_arbiter_6.tlsf` | R 2.747s | R 2.746s | -0.001s |
| `amba_decomposed_arbiter_7.tlsf` | TO 30.01s | TO 30.03s | +0.023s |
| `amba_decomposed_arbiter_8.tlsf` | TO 30.06s | TO 30.06s | -0.002s |
| `amba_decomposed_arbiter_10.tlsf` | TO 30.06s | TO 30.07s | +0.006s |
| `amba_decomposed_arbiter_12.tlsf` | TO 30.07s | TO 30.04s | -0.022s |
| `full_arbiter-3.tlsf` | R 0.008s | R 0.008s | +0.000s |
| `full_arbiter_2.tlsf` | R 0.006s | R 0.005s | -0.001s |
| `full_arbiter_3.tlsf` | R 0.007s | R 0.008s | +0.001s |
| `full_arbiter_4.tlsf` | R 0.070s | R 0.084s | +0.014s |
| `full_arbiter_5.tlsf` | R 16.652s | R 11.436s | -5.216s |
| `full_arbiter_6.tlsf` | TO 30.05s | TO 30.04s | -0.001s |
| `full_arbiter_7.tlsf` | TO 30.06s | TO 30.05s | -0.009s |
| `full_arbiter_8.tlsf` | TO 30.07s | TO 30.06s | -0.001s |
| `full_arbiter_10.tlsf` | TO 30.07s | TO 30.05s | -0.015s |
| `full_arbiter_12.tlsf` | TO 30.07s | TO 30.07s | +0.000s |
| `full_arbiter_enc_2.tlsf` | R 0.006s | R 0.006s | +0.000s |
| `full_arbiter_enc_4.tlsf` | R 2.979s | R 2.718s | -0.261s |
| `full_arbiter_enc_6.tlsf` | TO 30.04s | TO 30.04s | +0.001s |
| `full_arbiter_enc_8.tlsf` | TO 30.07s | TO 30.09s | +0.018s |
| `full_arbiter_enc_10.tlsf` | TO 30.05s | TO 30.05s | -0.001s |
| `full_arbiter_enc_12.tlsf` | TO 30.01s | TO 30.01s | +0.002s |
| `prioritized_arbiter-3.tlsf` | R 0.006s | R 0.006s | +0.000s |
| `prioritized_arbiter_1.tlsf` | R 0.005s | R 0.005s | +0.000s |
| `prioritized_arbiter_2.tlsf` | R 0.005s | R 0.005s | +0.000s |
| `prioritized_arbiter_3.tlsf` | R 0.005s | R 0.006s | +0.001s |
| `prioritized_arbiter_4.tlsf` | R 0.006s | R 0.007s | +0.001s |
| `prioritized_arbiter_5.tlsf` | R 0.032s | R 0.033s | +0.001s |
| `prioritized_arbiter_6.tlsf` | R 3.981s | R 3.524s | -0.457s |
| `prioritized_arbiter_7.tlsf` | TO 30.03s | TO 30.03s | +0.001s |
| `prioritized_arbiter_8.tlsf` | TO 30.02s | TO 30.03s | +0.017s |
| `prioritized_arbiter_10.tlsf` | TO 30.00s | TO 30.01s | +0.003s |
| `prioritized_arbiter_12.tlsf` | TO 30.03s | TO 30.03s | -0.003s |
| `prioritized_arbiter_enc_2.tlsf` | R 0.007s | R 0.006s | -0.001s |
| `prioritized_arbiter_enc_4.tlsf` | R 3.422s | R 3.682s | +0.260s |
| `prioritized_arbiter_enc_6.tlsf` | TO 30.04s | TO 30.02s | -0.020s |
| `prioritized_arbiter_enc_8.tlsf` | TO 30.06s | TO 30.06s | +0.001s |
| `prioritized_arbiter_enc_10.tlsf` | TO 30.03s | TO 30.04s | +0.013s |
| `prioritized_arbiter_enc_12.tlsf` | TO 30.05s | TO 30.05s | +0.001s |
| `round_robin_arbiter-3.tlsf` | R 0.031s | R 0.031s | +0.000s |
| `round_robin_arbiter_2.tlsf` | R 0.006s | R 0.006s | +0.000s |
| `round_robin_arbiter_3.tlsf` | R 0.031s | R 0.032s | +0.001s |
| `round_robin_arbiter_4.tlsf` | TO 30.04s | TO 30.04s | +0.000s |
| `round_robin_arbiter_5.tlsf` | TO 30.04s | TO 30.04s | +0.000s |
| `round_robin_arbiter_6.tlsf` | TO 30.03s | TO 30.03s | +0.000s |
| `round_robin_arbiter_7.tlsf` | TO 30.04s | TO 30.05s | +0.011s |
| `round_robin_arbiter_8.tlsf` | TO 30.03s | TO 30.03s | -0.002s |
| `round_robin_arbiter_10.tlsf` | TO 30.05s | TO 30.05s | -0.001s |
| `round_robin_arbiter_12.tlsf` | TO 30.06s | TO 30.06s | -0.001s |
| `simple_arbiter_2.tlsf` | R 0.005s | R 0.005s | +0.000s |
| `simple_arbiter_3.tlsf` | R 0.005s | R 0.005s | +0.000s |
| `simple_arbiter_4.tlsf` | R 0.005s | R 0.005s | +0.000s |
| `simple_arbiter_5.tlsf` | R 0.006s | R 0.006s | +0.000s |
| `simple_arbiter_6.tlsf` | R 0.226s | R 0.205s | -0.021s |
| `simple_arbiter_7.tlsf` | TO 30.03s | TO 30.03s | -0.001s |
| `simple_arbiter_8.tlsf` | TO 30.03s | TO 30.03s | +0.001s |
| `simple_arbiter_10.tlsf` | TO 30.03s | TO 30.03s | +0.000s |
| `simple_arbiter_12.tlsf` | TO 30.03s | TO 30.03s | +0.000s |
| `simple_arbiter_enc_2.tlsf` | R 0.006s | R 0.006s | +0.000s |
| `simple_arbiter_enc_4.tlsf` | R 0.007s | R 0.006s | -0.001s |
| `simple_arbiter_enc_6.tlsf` | TO 30.03s | TO 30.03s | +0.000s |
| `simple_arbiter_enc_8.tlsf` | TO 30.05s | TO 30.03s | -0.019s |
| `simple_arbiter_enc_10.tlsf` | TO 30.05s | TO 30.06s | +0.008s |
| `simple_arbiter_enc_12.tlsf` | TO 30.05s | TO 30.05s | +0.005s |

## Measurement notes / gotchas
- acacia forks real+unreal worker children; `timeout`/`subprocess` kills only the parent and
  **orphans the workers** (seen: 7 stray procs at 99% CPU for 12 min), which silently inflates
  later timings. `benchmarking/run-subset.py` now runs each instance in its own process group
  and `killpg`s on timeout. Always sanity-check `pgrep acacia-bonsai` between runs.
- Experiment binaries live in the `optimize-vs-ltlsynt` worktree: `build_exp` (=best_decomp_mona
  flags, verbose on, `Small`) and `build_any` (same + `-DACACIA_TRANSLATION_PREF=…::Any`), both
  linking system Spot 2.15.1. They differ from the logged `best_decomp_mona` (no `-flto`,
  verbose on), so use them for A/B against each other, not against the old logs.

## Reproduce
```
python3 benchmarking/loss-set.py --logs ../acacia-bonsai/_bm-logs-top4-on-2024_20s \
        --acacia best_decomp_mona --csv loss-set-2024_20s.csv
```
Backend coverage + per-path isolation: see commands in the session log (uses the prebuilt
`../acacia-bonsai/build_*/src/acacia-bonsai` binaries; `-r` real-only, `-U -u {automaton,formula}`
unreal-only).
