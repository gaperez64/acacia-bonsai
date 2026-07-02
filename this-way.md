# this-way.md — Replace the count-vector quotient solver with an exact equivariant solver

Execution plan for a coding agent. Work in THIS worktree
(`/home/gperez/GIT-repos/acacia-bonsai-optimize`, branch `optimize-vs-ltlsynt`).
Follow phases in order. Do not skip a validation gate. Do not "improve" the design.
When a phase is done and green, `git commit` (do NOT push unless the human asks).

---

## 0. Context — read this once, it explains every rule below

Acacia-bonsai solves LTL realizability as a K-bounded safety game: a greatest
fixpoint (GFP) `f ← f ∩ CPre(f)` over *downsets* of integer vectors indexed by
automaton states (`src/solver/k_bounded_safety_aut.hh`). On n-client arbiter
specs, the automaton is symmetric under client permutations, and we hoped to
exploit that.

The **existing attempt** (`src/solver/symmetric_k_bounded_safety_aut.hh` +
`symmetric_downset.hh`, behind `ACACIA_ENABLE_SYMMETRIC_SOLVER`) quotients the
*state*: it stores orbit representatives as "count-vectors" and replaces the
downset algebra with max-flow dominance tests and capped transportation-plan
intersections. **Measured result: it is structurally slower and almost never
concludes** (see DIAGNOSIS.md, sections "Benchmark checkpoint" and "dense
intersection hot-path pass"): per-operation costs are ~10³× worse than the SIMD
raw ops, and its intersection is a lossy under-approximation whose losses
compound through the GFP until the initial state falls out and it falls back —
making 100 % of its work overhead. **Do not extend that pipeline. Leave its
files alone.**

The **new design** (this plan) keeps the classical solver's *exact* raw
antichain and all its optimized downset machinery, and uses symmetry only to
avoid *recomputing* sets that are provably permutations of each other:

- With Φ the verified automorphism group, the fixpoint iterate `f` is
  Φ-invariant at well-defined boundaries, and `T_i := ∪_o PreHat(f, i, o)`
  (the per-input backward set) satisfies `T_{π·i} = φ_π · T_i`.
- So: compute `T` **once per input-letter orbit** (for n-client arbiters:
  ~n+1 orbits instead of 2ⁿ input letters), and obtain every other input's `T`
  by **permuting vector coordinates** — an O(|T|·d) copy, no backward passes,
  no dominance recomputation.
- Everything stays exact. There is no approximation, no work cap inside the
  loop, and therefore no fallback: if the structural preconditions hold, the
  equivariant solver returns the definitive answer; otherwise it declines
  *up front* (before doing any solving work) and the classic solver runs
  untouched.

**Soundness is the top priority of this whole effort.** Every rule below marked
MUST is a soundness rule, not a style preference.

---

## 1. Ground rules

1. **MUST NOT** modify the classic solver's behavior: `k_bounded_safety_aut.hh`
   may only gain compile-gated profiling scopes (Phase A, optional). A build
   without the new flag must be byte-for-byte behaviorally identical to today.
2. **MUST NOT** touch `src/solver/symmetric_downset.hh`,
   `symmetric_dense_downset.hh`, `symmetric_k_bounded_safety_aut.hh`,
   `symmetric_conversion.hh` or their tests, except where Phase B explicitly
   says to read/copy code *from* them. Never enable
   `ACACIA_ENABLE_SYMMETRIC_SOLVER` and the new flag in the same build.
3. **MUST NOT** introduce any approximation, cap, or heuristic *inside* the
   solve loop. All caps are structural pre-checks that cause an up-front
   decline. If you are tempted to cap something mid-loop, stop: the design is
   wrong, report back instead.
4. **MUST NOT** add dependencies or touch the `posets` submodule.
5. Every phase ends with the listed validation commands green. If a gate fails
   and the fix isn't obvious from this plan (the one permitted fix is the
   permutation-direction flip in Phase C), stop and report; do not guess.
6. Code style: match the repo (2-space indent, space before `(`, `not`/`and`
   instead of `!`/`&&`, headers are `#pragma once` + inline functions).
7. Kill stray processes between benchmark runs: acacia forks worker children;
   check `pgrep -a acacia-bonsai` and kill leftovers, or timings are garbage
   (documented gotcha in DIAGNOSIS.md).

---

## 2. Mathematical facts you may use without proof

These were derived and verified this project; treat them as given. Notation:
states `q` of the automaton `aut`; vectors `v : states → {−1..K}`; a *downset*
is represented by its antichain of maximal elements (`SetOfStates`).

- **(F1) Automorphisms.** `symmetry::detect` (`src/solver/symmetry.hh`) returns
  verified generators: state permutations `g` such that swapping one pair of
  client indices `(a,b)` in all AP families and relabeling states by `g` maps
  the automaton onto itself (edges, labels, acceptance, initial state).
  `full_symmetric == true` means every pairwise client transposition verified,
  so the verified group is the full symmetric group S_n on client slots.
- **(F2) Layout.** `symmetry::compute_block_layout`
  (`src/solver/symmetric_blocks.hh`) partitions states into `shared_states`
  (fixed by every generator) and `num_blocks` blocks of `num_clients` slots
  each: `block_slot_state[b][slot]`. It declines (`nullopt`) for n < 3.
- **(F3) Induced permutations.** IF every verified generator for pair `(a,b)`
  equals the layout-induced map "swap slot(a) and slot(b) in every block, fix
  everything else" (you will verify this at runtime, Phase B), THEN for ANY
  slot permutation σ the map
  `φ_σ : block_slot_state[b][j] ↦ block_slot_state[b][σ(j)]`, identity on
  shared states, is a verified automorphism (composition of verified ones).
- **(F4) Group action on vectors.** `(φ·v)[φ(q)] = v[q]`.
- **(F5) Equivariance.** For an automorphism φ with AP permutation π, and the
  backward step `pre` implemented by
  `actioner.apply (·, avec, actioners::direction::backward)`:
  `pre_{π(i), π(o)} (φ·v) = φ · pre_{i,o} (v)`. Consequently, if the downset
  `f` is Φ-invariant, then `T_{π·i} = φ_π · T_i` where
  `T_i = ∪_{all outputs o} pre_{i,o}` applied over f.
- **(F6) Invariance of iterates.** The initial safe downset is Φ-invariant
  (uniform values). If `f` is Φ-invariant and you replace it by
  `f ∩ ⋂_{i ∈ one whole input orbit} T_i` (all members of the orbit), the
  result is Φ-invariant again. The K-bump map (add `kinc` to all non-boolean
  coordinates) preserves Φ-invariance. **Mid-orbit, `f` is NOT invariant** —
  this is why the loop rules in Phase D exist.
- **(F7) Letters as type sequences.** Input letters factor into: one "type" per
  client slot (type = bit per indexed input family, e.g. arbiters have one
  family `r_` so types are {0,1}) plus one assignment of shared (non-indexed)
  input APs. Two input letters are in the same orbit iff they have the same
  multiset of slot types and the same shared assignment. The orbit
  representative is the letter whose slot-type sequence is sorted ascending.

---

## Phase A (optional, timeboxed 1 h) — profile the classic solver baseline

Purpose: a denominator for the final report ("where does mainline spend time on
arbiters"). If this fights you for more than an hour, skip it; nothing later
depends on it.

1. In `src/solver/symmetric_profile.hh`, extend `enum class bucket` and
   `name()` with: `classic_backward_apply`, `classic_pre_build`,
   `classic_intersect`, `classic_solve_total`.
2. In `src/solver/k_bounded_safety_aut.hh` add
   `#include "solver/symmetric_profile.hh"` and, ONLY inside
   `#if ACACIA_SYMMETRY_PROFILE` guards (the macro
   `ACACIA_SYMMETRY_PROFILE_SCOPE` is already a no-op otherwise):
   - scope `classic_solve_total` around the body of `solve ()`;
   - in `cpre_inplace` (the `CPRE_AVOID_UNIONS == 0` branch): scope
     `classic_pre_build` around the whole per-action loop, scope
     `classic_backward_apply` *inside* the lambda around the
     `actioner.apply` call (note: nested, so backward_apply time is counted in
     both buckets — say so when reporting), scope `classic_intersect` around
     `f.intersect_with`.
   - after `solve ()`'s return points… simplest: wrap the loop in a helper or
     add `acacia::solver_detail::symmetric::profile::global ().report ();`
     just before each `return` in `solve ()`, inside `#if` guards.
3. Build a profile copy of the release config:
   `meson configure build_rs | grep -E 'cpp_args|buildtype'` to see the flags,
   then `meson setup build_prof <same flags> -Dcpp_args='<same> -DACACIA_SYMMETRY_PROFILE=1'`
   (append to existing cpp_args, don't replace).
4. Run 2–3 arbiter instances (find them:
   `ls tests/ltl/realizable | grep -i arbiter`), e.g.
   `meson test -C build_prof 'ab/amba_decomposed_arbiter_5.ltl' -v --timeout-multiplier 4`,
   and copy the `[symmetry][profile]` lines into DIAGNOSIS.md under a new
   heading "Classic-solver profile baseline".
5. Commit ("profile: compile-gated buckets for the classic solve loop").
   Verify first: a build WITHOUT `ACACIA_SYMMETRY_PROFILE` still compiles
   (`meson compile -C build_rs`) — the default path must be unchanged.

---

## Phase B — groundwork: generator metadata + structural verification

### B1. Record which AP pair each generator came from

File: `src/solver/symmetry.hh`.

- In `struct group`, add the field (right after `gens`):
  ```cpp
  // gen_pairs[t] is the client-index pair (a, b) whose verified transposition
  // produced gens[t]. Same order and length as gens.
  std::vector<std::pair<long, long>> gen_pairs;
  ```
- In `detect ()`, immediately after `G.gens.push_back (f.phi);` add
  `G.gen_pairs.push_back ({a, b});`.

### B2. Verify generators match the layout (enables F3)

File: `src/solver/symmetric_blocks.hh`, add at the end of `namespace symmetry`:

```cpp
  // True iff every verified generator is EXACTLY the layout-induced block
  // transposition for its AP index pair: identity everywhere except
  // block_slot_state[b][slot(a)] <-> block_slot_state[b][slot(b)] in every
  // block b. When this holds, the layout-induced map of ANY slot permutation
  // is an automorphism (composition of verified transpositions), which is
  // what the equivariant solver needs. Declining here is always sound.
  inline bool generators_match_layout (const group& G, const block_layout& L) {
    if (G.gens.size () != G.gen_pairs.size ())
      return false;
    std::map<long, unsigned> slot_of_index;
    for (unsigned s = 0; s < L.slot_to_index.size (); ++s)
      slot_of_index[L.slot_to_index[s]] = s;
    for (size_t t = 0; t < G.gens.size (); ++t) {
      const auto [a, b] = G.gen_pairs[t];
      auto ita = slot_of_index.find (a), itb = slot_of_index.find (b);
      if (ita == slot_of_index.end () or itb == slot_of_index.end ())
        return false;
      std::vector<unsigned> expected (L.num_states);
      for (unsigned q = 0; q < L.num_states; ++q) expected[q] = q;
      for (unsigned blk = 0; blk < L.num_blocks; ++blk) {
        const unsigned sa = L.block_slot_state[blk][ita->second];
        const unsigned sb = L.block_slot_state[blk][itb->second];
        expected[sa] = sb;
        expected[sb] = sa;
      }
      if (G.gens[t] != expected)
        return false;
    }
    return true;
  }
```

### B3. Extend the synthetic blocks test

File: `tests/symmetric_blocks_test.cc`. The synthetic groups there already have
exactly this shape; fill in `gen_pairs` when building them
(`G.gen_pairs.push_back ({a, b});` next to each `G.gens.push_back`), and for
every case that yields a layout, assert `generators_match_layout (G, *L)` is
true. Also add one negative case: corrupt one generator (swap two entries) and
assert `generators_match_layout` returns false.

### Gate B

```
meson compile -C build_rs
meson test -C build_sym --suite symmetry
```
All green (the symmetry suite currently has 5 tests; all must still pass).
Commit ("symmetry: record generator AP pairs; verify generators match layout").

---

## Phase C — the equivariant core + its oracle test (the critical gate)

### C1. New header `src/solver/equivariant_k_bounded_safety_aut.hh`

Namespace `acacia::solver_detail::equivariant`. This header is included ONLY
from `solve_game_impl.hh` behind the new flag (Phase D), but write it so it
compiles standalone (the test includes it directly).

Includes you'll need:
```cpp
#include "actioners/direction.hh"
#include "actioners/standard.hh"
#include "configuration.hh"
#include "solver/symmetric_blocks.hh"
#include "solver/symmetry.hh"
#include "utils/verbose.hh"
#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#include <bddx.h>
#include <spot/twa/twagraph.hh>
```

New config macros (add defaults to `src/configuration.hh` following the
existing pattern):
```
ACACIA_ENABLE_EQUIVARIANT_SOLVER   (default 0)
ACACIA_EQUIVARIANT_MAX_STATES      (default 512)   // decline before detect
ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS (default 4096)
ACACIA_EQUIVARIANT_MAX_ORBITS      (default 4096)
```

**Helpers (duplicate, don't move).** Copy these `detail::` functions VERBATIM
from `src/solver/symmetric_k_bounded_safety_aut.hh` into your own
`namespace detail` (duplication is deliberate — rule 2 forbids touching the
old header): `support_contains_var`, `enumerate_letters`, `compute_transset`,
`build_client_family_vars`, `shared_vars`, `enumerate_type_counts_rec`,
`enumerate_type_counts`, `input_letter_from_counts`, `compute_action_vec`.
Also copy the type aliases `transset`, `action`, `action_vec`.

**New helper — letter from an arbitrary slot-type sequence** (used by the test
and useful for debugging):
```cpp
    // Like input_letter_from_counts, but for an explicit per-slot type
    // sequence instead of a sorted count distribution.
    inline bdd input_letter_from_slot_types (
        const std::vector<std::vector<int>>& family_slot_vars,
        const std::vector<int>& shared_vars_,
        const std::vector<unsigned>& slot_types, unsigned shared_mask) {
      bdd letter = bddtrue;
      for (unsigned slot = 0; slot < slot_types.size (); ++slot)
        for (unsigned fam = 0; fam < family_slot_vars.size (); ++fam) {
          const bdd v = bdd_ithvar (family_slot_vars[fam][slot]);
          letter &= ((slot_types[slot] >> fam) & 1U) ? v : !v;
        }
      for (unsigned i = 0; i < shared_vars_.size (); ++i) {
        const bdd v = bdd_ithvar (shared_vars_[i]);
        letter &= ((shared_mask >> i) & 1U) ? v : !v;
      }
      return letter;
    }
```

**Data structure:**
```cpp
  struct input_orbit {
      // Sorted ASCENDING; this is the orbit representative's slot->type map.
      std::vector<unsigned> canonical_types;
      // Deduplicated backward actions of the representative letter, over ALL
      // output letters (exact; deduplication of identical action_vecs is
      // exactness-preserving for the union).
      std::vector<action_vec> actions;
  };
```

**Orbit construction** `build_orbits (aut, all_inputs, all_outputs, G, L)`
returning `std::optional<std::vector<input_orbit>>` (nullopt = decline):
1. `build_client_family_vars (aut, G, L, /*want_input=*/true, ...)`; decline on
   false. Decline if `family_slot_vars.size () >= 8 * sizeof (unsigned)`.
2. `num_types = 1u << family_slot_vars.size ()`;
   `type_counts = enumerate_type_counts (L.num_clients, num_types, ACACIA_EQUIVARIANT_MAX_ORBITS + 1)`;
   decline if over the cap.
3. Shared input vars via `shared_vars (aut, all_inputs, indexed_input_vars)`;
   decline if `>= 20` of them; total orbits =
   `(1u << shared) * type_counts.size ()`; decline if `> ACACIA_EQUIVARIANT_MAX_ORBITS`.
4. `output_letters = enumerate_letters (all_outputs, ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS)`;
   **decline if it returns empty** (that is its cap-exceeded signal) — the
   union over outputs must be complete, truncation is unsound.
5. For each `shared_mask`, for each `counts` in `type_counts`:
   - rep letter = `input_letter_from_counts (family_slot_vars, shared_input_vars, counts, shared_mask)`;
   - `canonical_types` = type 0 repeated `counts[0]` times, then type 1
     repeated `counts[1]` times, … (ascending by construction);
   - actions: `std::set<action_vec> uniq;` then for each output letter `o`:
     `uniq.insert (compute_action_vec (aut, compute_transset (aut, rep & o)));`
     finally copy `uniq` into the vector.

**Slot-permutation machinery:**
```cpp
  // sigma[j] = the member slot that receives the client sitting at
  // representative slot j. c is sorted ascending; s is a permutation of the
  // same multiset. Pair the k-th slot of type t in c with the k-th slot of
  // type t in s (any type-preserving matching is valid: same-type slots of
  // the representative are exchangeable by its stabilizer).
  inline std::vector<unsigned> match_slots (const std::vector<unsigned>& c,
                                            const std::vector<unsigned>& s,
                                            unsigned num_types) {
    const unsigned n = (unsigned) c.size ();
    std::vector<std::vector<unsigned>> slots_of_type (num_types);
    for (unsigned j = 0; j < n; ++j) slots_of_type[s[j]].push_back (j);
    std::vector<unsigned> next (num_types, 0), sigma (n);
    for (unsigned j = 0; j < n; ++j) sigma[j] = slots_of_type[c[j]][next[c[j]]++];
    return sigma;
  }

  // F3: layout-induced state permutation of a slot permutation.
  inline std::vector<unsigned> phi_from_sigma (const symmetry::block_layout& L,
                                               const std::vector<unsigned>& sigma) {
    std::vector<unsigned> phi (L.num_states);
    for (unsigned q = 0; q < L.num_states; ++q) phi[q] = q;
    for (unsigned blk = 0; blk < L.num_blocks; ++blk)
      for (unsigned j = 0; j < L.num_clients; ++j)
        phi[L.block_slot_state[blk][j]] = L.block_slot_state[blk][sigma[j]];
    return phi;
  }
```

**Set operations** (templates over `SetOfStates`; `state` is
`typename SetOfStates::value_type`):

```cpp
  // T_member = phi . T_rep, using the group action (F4): out[phi[q]] = in[q].
  // A permutation maps antichains to antichains, so apply() is exact here.
  template <typename SetOfStates>
  SetOfStates permute (const SetOfStates& T, const std::vector<unsigned>& phi) {
    return T.apply ([&phi] (const auto& s) {
      posets::utils::vector_mm<VECTOR_ELT_T> out (s.size (), 0);
      for (size_t q = 0; q < s.size (); ++q)
        out[phi[q]] = s[q];
      return typename SetOfStates::value_type (out);
    });
  }

  // Exact T = union over ALL outputs of the backward step (mirrors the
  // classic cpre_inplace construction of f1i, CPRE_AVOID_UNIONS == 0).
  template <typename SetOfStates, typename Actioner>
  SetOfStates compute_T (const SetOfStates& f, const std::vector<action_vec>& actions,
                         Actioner& actioner, unsigned num_states) {
    posets::utils::vector_mm<VECTOR_ELT_T> bot (num_states, 0);
    bot.assign (num_states, -1);
    SetOfStates T {typename SetOfStates::value_type (bot)};
    bool first = true;
    for (const auto& avec : actions) {
      SetOfStates Tio = f.apply ([&] (const auto& m) {
        return actioner.apply (m, avec, actioners::direction::backward);
      });
      if (first) { T = std::move (Tio); first = false; }
      else T.union_with (std::move (Tio));
    }
    return T;
  }

  // f is a subset of the downset T iff every maximal element of f is in T.
  // Used for exact change detection: f.intersect_with(T) changes f iff this
  // is false. (Comparing sizes is NOT a valid change test for downsets.)
  template <typename SetOfStates>
  bool subset_of (const SetOfStates& f, const SetOfStates& T) {
    for (const auto& m : f)
      if (not T.contains (m))
        return false;
    return true;
  }
```

### C2. The oracle test `tests/equivariant_pre_test.cc` — MUST pass before Phase D

This test is the safety net for every direction convention (σ, φ, the group
action, the letter construction). Structure (follow the patterns of
`tests/symmetric_blocks_test.cc` and `tests/elevator_preprocessor_test.cc`,
including the `namespace utils { unsigned verbose = 0; voutstream vout; }`
preamble):

1. **Build a hand-made symmetric automaton** for n ∈ {3, 4} clients,
   1 + 2n states, registered APs `r_0..r_{n-1}` (inputs) and `g_0..g_{n-1}`
   (outputs), `aut->prop_state_acc (true)`, Büchi acceptance
   (`aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ())`):
   - state 0 (shared): edge `0 -> 0` with cond `bddtrue`; for each client i,
     edge `0 -> 1+i` with cond `r_i`;
   - state `1+i`: edge `1+i -> 1+i` cond `!g_i` **marked accepting** (`{0}`),
     edge `1+i -> n+1+i` cond `g_i` **marked accepting** (`{0}`)
     (all out-edges marked, so `state_is_accepting (1+i)` is true);
   - state `n+1+i`: edge `n+1+i -> n+1+i` cond `!g_i`, edge `n+1+i -> 0`
     cond `g_i` (unmarked).
   Build `all_inputs` = conjunction of `bdd_ithvar` of the `r_i`,
   `all_outputs` = same for `g_i`.
2. Run `symmetry::detect (aut, all_inputs, all_outputs)`; assert
   `full_symmetric`, `indices.size () == n`, and
   `gen_pairs.size () == gens.size ()`. Run `compute_block_layout`; assert a
   layout with `num_blocks == 2`, `num_clients == n`. Assert
   `generators_match_layout (G, *L)`.
3. Set `posets::vectors::bool_threshold` and
   `posets::vectors::bitset_threshold` to `aut->num_states ()` (run 1) and
   re-run everything with `bool_threshold = 1 + n` (run 2 — states `n+1+i`
   boolean; they form whole orbits so this is layout-legal).
4. Use `SetOfStates = posets::downsets::vector_backed<posets::vectors::vector_backed<VECTOR_ELT_T>>`
   (include `<posets/downsets.hh>`). Build the actioner exactly as the solver
   will: declare
   `std::vector<std::pair<bdd, std::vector<detail::transset>>> empty_itoios;`
   (empty — the actioner's precomputed action list is unused; we only need
   `apply` and `setK`), then
   `auto actioner = actioners::standard<state>::make (aut, empty_itoios, K);`
   with `K = 3`.
5. Build Φ-invariant test downsets `f`:
   - the safe downset (vector of `K-1` everywhere, 0 on boolean states);
   - 5 random downsets: draw 4 random vectors in `{-1..K-1}` (0 on boolean
     coords), then close under symmetry: for EVERY permutation σ of `0..n-1`
     (`std::next_permutation` over the identity), add `permute` of each vector
     by `phi_from_sigma (L, sigma)`; insert all into one `SetOfStates` (the
     antichain construction handles domination).
6. `build_orbits (...)` on the automaton; assert it succeeds and the orbit
   count is `n + 1` (one input family, no shared inputs).
7. **The oracle check.** For each `f`, each orbit, each member sequence `s`
   (enumerate with `do { ... } while (std::next_permutation (seq))` starting
   from `canonical_types`):
   - equivariant answer: `sigma = match_slots (canonical, s, num_types)`;
     `phi = phi_from_sigma (L, sigma)`;
     `T_eq = permute (compute_T (f, orbit.actions, actioner, N), phi)`;
   - direct answer: build the member's own letter with
     `input_letter_from_slot_types`, compute its deduped action_vecs over all
     output letters from scratch (same recipe as `build_orbits` step 5), and
     `T_direct = compute_T (f, those_actions, actioner, N)`;
   - assert set equality: `subset_of (T_eq, T_direct) and subset_of (T_direct, T_eq)`.
8. Register in `tests/meson.build` next to the other symmetry tests:
   ```meson
   equivariant_pre_test = executable ('equivariant-pre-test',
                                      'equivariant_pre_test.cc',
                                      include_directories : inc,
                                      dependencies : [spot_dep, bddx_dep, posets_dep])
   test ('equivariant-pre', equivariant_pre_test, suite : ['unit', 'symmetry'])
   ```

**Permitted fix if the oracle fails ONLY on the permutation direction:** flip
the action in `permute` (use `out[q] = s[phi[q]]` instead of
`out[phi[q]] = s[q]`) — equivalently invert φ — and re-run. If it still fails,
or fails only for SOME members, something else is wrong (most likely
`match_slots`): stop and report with a small failing example printed.

### Gate C

```
meson test -C build_sym --suite symmetry     # now 6 tests, all green
meson compile -C build_rs                    # default build unaffected
```
Commit ("equivariant: exact per-orbit backward sets + permutation oracle test").

---

## Phase D — the equivariant solve loop + dispatch

### D1. The solver entry point (same header)

```cpp
  template <typename SetOfStates>
  struct result {
      bool attempted = false;  // false => structural decline, run classic
      std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> win;  // classic contract
  };

  template <typename SetOfStates>
  result<SetOfStates> try_solve (spot::twa_graph_ptr aut, VECTOR_ELT_T kmax,
                                 VECTOR_ELT_T kmin, VECTOR_ELT_T kinc,
                                 const bdd& all_inputs, const bdd& all_outputs);
```

**Structural pre-checks, in order (any failure ⇒ `return {false, {}}`):**
1. `aut->num_states () > ACACIA_EQUIVARIANT_MAX_STATES` ⇒ decline (before
   `detect`, whose backtracking search must not run on big automata).
2. `G = symmetry::detect (...)`; decline unless `G.full_symmetric`.
3. `L = symmetry::compute_block_layout (G, aut->num_states ())`; decline if
   empty.
4. `generators_match_layout (G, *L)` — decline if false.
5. Boolean-side consistency: for every generator `g` and state `q`,
   `(q < posets::vectors::bool_threshold) == (g[q] < posets::vectors::bool_threshold)`;
   decline if violated (a permutation must never move a counting coordinate
   onto a boolean one).
6. `orbits = build_orbits (...)`; decline if empty/nullopt.

**The loop.** Mirror the classic `solve ()`
(`k_bounded_safety_aut.hh:58-129`) for the safe vector, init vector, and
K-bump — copy those code blocks. Then:

```
k = kmin;  actioner = actioners::standard<state>::make (aut, empty_itoios, k)
f = SetOfStates (state (safe_vector))          // classic safe set
init = classic init vector
restart:
  changed = false
  for each orbit O in orbits:
    # f is Phi-invariant here (F6) -- REQUIRED for the next line to be sound
    T_rep = compute_T (f, O.actions, actioner, num_states)
    seq = O.canonical_types                     # ascending = first permutation
    do:
      sigma = match_slots (O.canonical_types, seq, num_types)
      T_mem = permute (T_rep, phi_from_sigma (L, sigma))
      if not subset_of (f, T_mem):
        f.intersect_with (std::move (T_mem)); changed = true
    while std::next_permutation (seq)
    # orbit fully processed -> f is Phi-invariant again; ONLY here may we
    # look at init or bump K (MUST: never mid-orbit; see F6)
    if not f.contains (state (init)):
      if k >= kmax: return {true, std::nullopt}          # unreal within K
      k += kinc; actioner.setK (k)
      f = f.apply (classic K-bump lambda)                 # verbatim from classic
      goto restart                                        # recompute T from new f
  if not changed: return {true, {{k, std::move (f)}}}     # exact fixpoint, init in f
  goto restart
```

Implementation notes:
- `empty_itoios`: `std::vector<std::pair<bdd, std::vector<transset>>>` — the
  actioner precomputes nothing from it; we drive `apply` with our own
  per-orbit `action_vec`s.
- The identity member (`seq == canonical_types`) also goes through
  `permute` — a wasted copy, but uniform code with no move-ordering bug. Do
  not "optimize" this in the first version.
- Add `verb_do (1, ...)` progress lines mirroring the classic ones (loop
  count, `f.size ()`, number of orbits, members processed) prefixed
  `[equivariant]`, and one line at decline stating which pre-check declined.
- Termination is guaranteed: `f` only shrinks between K-bumps (finite
  lattice), K-bumps are bounded by `kmax`. **No caps in the loop.**

### D2. Dispatch

File: `src/solver/solve_game_impl.hh`, function `solve_with_downset`. At the
top of the function body add:

```cpp
#if ACACIA_ENABLE_EQUIVARIANT_SOLVER
    if (not do_synthesis) {
      auto eq = acacia::solver_detail::equivariant::try_solve<SpecializedDownset> (
          aut, kmax, kmin, kinc, all_inputs, all_outputs);
      if (eq.attempted)
        return post_real<SpecializedDownset> (std::move (eq.win), do_synthesis, aut,
                                              all_inputs, all_outputs);
    }
#endif
```

with the `#include "solver/equivariant_k_bounded_safety_aut.hh"` at the top of
the file, also guarded by `#if ACACIA_ENABLE_EQUIVARIANT_SOLVER`.

Do NOT touch the old `ACACIA_ENABLE_SYMMETRIC_SOLVER` block in
`solve_game.cc`; it stays default-off and unused.

### D3. Build config

Create the build:
```
meson configure build_rs | grep -E 'cpp_args|buildtype'   # copy these flags
meson setup build_eq <same options> \
  -Dcpp_args='<existing args> -DACACIA_ENABLE_EQUIVARIANT_SOLVER=1'
```

### Gate D

1. `meson compile -C build_rs` — default build unaffected.
2. `meson compile -C build_eq` — flag build compiles.
3. `meson test -C build_eq --suite symmetry` — all green.
4. Smoke: pick a small arbiter, e.g.
   `meson test -C build_eq 'ab/amba_decomposed_arbiter_3.ltl' -v` — must PASS,
   and the verbose log must show `[equivariant]` actually solving (fixpoint
   reached), not declining. Also run one NON-symmetric instance and confirm
   the log shows a clean decline followed by the classic solver.
5. Commit ("equivariant: exact symmetry-deduplicated solve loop + dispatch").

---

## Phase E — validation (mandatory, in this order)

### E1. Full-corpus oracle — zero verdict changes

The meson `ab` suite runs every LTL instance through
`check-real-correct.sh`, which knows the expected verdict; a wrong polarity is
a FAIL. Run baseline and flag build the same way, same machine:

```
meson test -C build_rs --suite ab 2>&1 | tail -5    # baseline
meson test -C build_eq --suite ab 2>&1 | tail -5
```

Save both logs (`build_*/meson-logs/testlog.txt` → copy aside). Diff the
per-test outcomes (`grep -E 'OK|FAIL|TIMEOUT' testlog.txt`-level comparison).
**Acceptance: the equivariant build has NO failure that the baseline does not
have.** Timeouts may differ slightly; new timeouts on previously-passing tests
are a regression — investigate (most likely a decline check is too permissive
and detect() is running long on something big; tighten
`ACACIA_EQUIVARIANT_MAX_STATES`). Any new FAIL that is a verdict flip is a
soundness bug: STOP, do not tune anything, report with the instance name.

### E2. Arbiter behavior check

For each arbiter instance in the corpus (ascending size), run the eq build
with verbosity and record: declined-or-solved, number of orbits, `f.size ()`
at fixpoint, wall time; same wall time for `build_rs`. Present as a small
table in DIAGNOSIS.md. Expected shape: eq solves (not declines) the
`amba_decomposed_arbiter_*` family, matches all verdicts, and its per-pass
work no longer scales with 2ⁿ input letters. If eq is slower on every size,
that is a valid (negative) result — record it honestly; do NOT add caps or
heuristics to force a win.

### E3. Timing benchmark (only after E1 is clean)

Reuse the flow from DIAGNOSIS.md "Benchmark checkpoint": build a release/LTO
pair (baseline flags from `benchmarking/`/`self-benchmark.sh` best config vs
same + `-DACACIA_ENABLE_EQUIVARIANT_SOLVER=1`), run
`benchmarking/run-tlsf.py` on the arbiter TLSF subset
(`tests/ltl/realizable/*arbiter*.tlsf` — build a list file), 30 s timeout,
each instance in its own process group (the script already does this). Check
`pgrep -a acacia-bonsai` between runs. Record: solved counts, total times,
per-instance table, verdict mismatches (must be 0). Append to DIAGNOSIS.md
under "Equivariant solver benchmark".

### E4. Commit

Commit the DIAGNOSIS.md updates ("docs: equivariant solver validation +
benchmark results"). Do not push unless the human asks.

---

## Phase F — wrap-up

1. In DIAGNOSIS.md, add a short section "Count-vector quotient pipeline:
   retired" stating: kept in-tree behind `ACACIA_ENABLE_SYMMETRIC_SOLVER`
   (default off) as research code; superseded by the exact equivariant solver
   because (a) its per-op costs exceeded the orbit-collapse benefit at every
   reachable size and (b) its lossy meet compounds through the GFP and almost
   never concludes. One paragraph, link the benchmark numbers.
2. Update `this-way.md` phase checkboxes / add a "results" footer.
3. Final commit.

---

## Explicitly OUT OF SCOPE — do not do these even if they look easy

- **Synthesis (`-s`) through the equivariant path.** The computed `f` is the
  exact classic winning region, so this should eventually work via
  `post_real` unchanged — but enabling it needs the model-check gate and its
  own validation round. Keep the `not do_synthesis` guard.
- **Output-side dedup inside `compute_T`** (computing one backward pass per
  output orbit under the representative's stabilizer and permuting): a real
  further win, but only worth it if E3 profiling shows `compute_T` still
  dominating. Design is in the session notes; needs its own oracle test.
- **Reducing the number of intersections** via generator-sweep symmetrization
  (`T ← T ∩ φ_τ·T` to a fixpoint). Experimental; measured intersect counts
  must justify it first.
- Any change to the count-vector pipeline, the posets submodule, K-schedules,
  input pickers, or translation settings.

---

## Results footer

- [x] Phase A: added compile-gated classic-solver profile buckets and recorded the small AMBA
  profile baseline in `DIAGNOSIS.md`. Commit: `d016ecdd`.
- [x] Phase B: recorded generator AP pairs and added the layout/generator equality check.
  Symmetry suite passed. Commit: `fa689cb5`.
- [x] Phase C: added the exact equivariant helper layer and permutation oracle test. Symmetry
  suite passed with 6 tests. Commit: `d1761f32`.
- [x] Phase D: added the exact equivariant solve loop and compile-gated dispatch. Default and
  equivariant builds compiled; symmetry suite passed; symmetric PB smoke solved and
  non-symmetric smoke declined cleanly. Commit: `6c229958`.
- [x] Phase E1: full `ab` oracle comparison was clean: 624 rows on both builds, 551 OK, 71
  TIMEOUT, 2 shared FAIL3, and 0 ordered outcome mismatches.
- [x] Phase E2: AMBA decomposed LTL family did not exercise the equivariant path; sizes 2-6
  declined as "not a verified full symmetric group" and larger sizes timed out in both builds.
- [x] Phase E3: release/LTO TLSF arbiter subset benchmark was verdict-clean: 31/67 solved and
  36 timeouts on both builds, 0 result mismatches, and total time 1111.974s baseline versus
  1106.300s equivariant.
- [x] Phase E4: committed the validation and benchmark notes. Commit: `f66349db`.
- [x] Phase F: retired the count-vector quotient pipeline in the diagnosis notes and recorded
  this results footer. Commit: final wrap-up commit.
