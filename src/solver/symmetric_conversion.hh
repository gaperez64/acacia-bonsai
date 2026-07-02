#pragma once

// Conversion between acacia's raw, automaton-state-indexed counter vectors
// (posets::utils::vector_mm<VECTOR_ELT_T>) and symmetric_downset::count_vector
// (the canonical, orbit-representative form), using a symmetry::block_layout.
// See DIAGNOSIS.md, "Symmetry reduction: design + status", for the full
// derivation this supports.

#include "configuration.hh"
#include "solver/symmetric_blocks.hh"
#include "solver/symmetric_downset.hh"

#include <posets/utils/vector_mm.hh>

#include <array>
#include <algorithm>
#include <cassert>
#include <vector>

namespace symmetry {

  using split_key = long (*) (const symmetric_downset::type_t&);

  // Exact, lossless: every raw vector maps to a unique count-vector (the
  // per-client-type multiset). No approximation here.
  inline symmetric_downset::count_vector to_count_vector (
      const block_layout& L, const posets::utils::vector_mm<VECTOR_ELT_T>& v) {
    symmetric_downset::count_vector c;
    c.shared.resize (L.shared_states.size ());
    for (size_t i = 0; i < L.shared_states.size (); ++i)
      c.shared[i] = v[L.shared_states[i]];

    symmetric_downset::type_t t (L.num_blocks);
    for (unsigned slot = 0; slot < L.num_clients; ++slot) {
      for (unsigned b = 0; b < L.num_blocks; ++b)
        t[b] = v[L.block_slot_state[b][slot]];
      c.counts[t]++;
    }
    return c;
  }

  // Realize ONE concrete raw vector for a count-vector, assigning its counted
  // client-types to the n abstract slots in DESCENDING order of `key(type)`
  // (ties broken by the type's own total order, for determinism). Different
  // `key` choices give different (but always genuinely valid/achievable)
  // client-to-slot assignments -- this is how the bounded candidate-split
  // enumeration for T_i (DIAGNOSIS.md) is implemented: callers try a few
  // different keys and union the resulting PreHat outcomes.
  inline void realize_into (
      const block_layout& L, const symmetric_downset::count_vector& c,
      split_key key, posets::utils::vector_mm<VECTOR_ELT_T>& v,
      std::vector<symmetric_downset::type_t>& expanded) {
    if (v.size () != L.num_states)
      v = posets::utils::vector_mm<VECTOR_ELT_T> (L.num_states);

    for (size_t i = 0; i < L.shared_states.size (); ++i)
      v[L.shared_states[i]] = (VECTOR_ELT_T) c.shared[i];

    expanded.clear ();
    expanded.reserve (L.num_clients);
    for (auto& [t, cnt] : c.counts)
      for (long i = 0; i < cnt; ++i) expanded.push_back (t);
    assert (expanded.size () == L.num_clients);
    std::sort (expanded.begin (), expanded.end (),
               [&] (const symmetric_downset::type_t& a, const symmetric_downset::type_t& b) {
                 const long ka = key (a), kb = key (b);
                 if (ka != kb) return ka > kb;
                 return a < b;  // deterministic tie-break
               });

    for (unsigned slot = 0; slot < L.num_clients; ++slot)
      for (unsigned b = 0; b < L.num_blocks; ++b)
        v[L.block_slot_state[b][slot]] = (VECTOR_ELT_T) expanded[slot][b];
  }

  inline posets::utils::vector_mm<VECTOR_ELT_T> realize (
      const block_layout& L, const symmetric_downset::count_vector& c, split_key key) {
    posets::utils::vector_mm<VECTOR_ELT_T> v (L.num_states);
    std::vector<symmetric_downset::type_t> expanded;
    realize_into (L, c, key, v, expanded);
    return v;
  }

  // A small, fixed set of candidate sort keys for the bounded split
  // enumeration: total-sum descending/ascending, and per-block-lexicographic
  // descending/ascending -- cheap, deterministic, and each a genuinely valid
  // realization (soundness is structural, not from exhausting all splits).
  inline long split_key_sum_desc (const symmetric_downset::type_t& t) {
    long s = 0;
    for (auto x : t) s += x;
    return s;
  }

  inline long split_key_sum_asc (const symmetric_downset::type_t& t) {
    return -split_key_sum_desc (t);
  }

  inline long split_key_first_desc (const symmetric_downset::type_t& t) {
    return t.empty () ? 0L : (long) t[0];
  }

  inline long split_key_first_asc (const symmetric_downset::type_t& t) {
    return -split_key_first_desc (t);
  }

  inline const std::array<split_key, 4>& candidate_split_keys () {
    static const std::array<split_key, 4> keys = {
      split_key_sum_desc, split_key_sum_asc, split_key_first_desc, split_key_first_asc};
    return keys;
  }

}  // namespace symmetry
