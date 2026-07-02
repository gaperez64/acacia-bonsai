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

#include <algorithm>
#include <functional>
#include <vector>

namespace symmetry {

  // Exact, lossless: every raw vector maps to a unique count-vector (the
  // per-client-type multiset). No approximation here.
  inline symmetric_downset::count_vector to_count_vector (
      const block_layout& L, const posets::utils::vector_mm<VECTOR_ELT_T>& v) {
    symmetric_downset::count_vector c;
    c.shared.resize (L.shared_states.size ());
    for (size_t i = 0; i < L.shared_states.size (); ++i)
      c.shared[i] = v[L.shared_states[i]];

    for (unsigned slot = 0; slot < L.num_clients; ++slot) {
      symmetric_downset::type_t t (L.num_blocks);
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
  inline posets::utils::vector_mm<VECTOR_ELT_T> realize (
      const block_layout& L, const symmetric_downset::count_vector& c,
      const std::function<long (const symmetric_downset::type_t&)>& key) {
    posets::utils::vector_mm<VECTOR_ELT_T> v (L.num_states);

    for (size_t i = 0; i < L.shared_states.size (); ++i)
      v[L.shared_states[i]] = (VECTOR_ELT_T) c.shared[i];

    std::vector<symmetric_downset::type_t> expanded;
    expanded.reserve (L.num_clients);
    for (auto& [t, cnt] : c.counts)
      for (long i = 0; i < cnt; ++i) expanded.push_back (t);
    std::sort (expanded.begin (), expanded.end (),
               [&] (const symmetric_downset::type_t& a, const symmetric_downset::type_t& b) {
                 const long ka = key (a), kb = key (b);
                 if (ka != kb) return ka > kb;
                 return a < b;  // deterministic tie-break
               });

    for (unsigned slot = 0; slot < L.num_clients; ++slot)
      for (unsigned b = 0; b < L.num_blocks; ++b)
        v[L.block_slot_state[b][slot]] = (VECTOR_ELT_T) expanded[slot][b];
    return v;
  }

  // A small, fixed set of candidate sort keys for the bounded split
  // enumeration: total-sum descending/ascending, and per-block-lexicographic
  // descending/ascending -- cheap, deterministic, and each a genuinely valid
  // realization (soundness is structural, not from exhausting all splits).
  inline std::vector<std::function<long (const symmetric_downset::type_t&)>>
  candidate_split_keys () {
    using type_t = symmetric_downset::type_t;
    std::function<long (const type_t&)> sum_desc = [] (const type_t& t) {
      long s = 0;
      for (auto x : t) s += x;
      return s;
    };
    std::function<long (const type_t&)> sum_asc = [] (const type_t& t) {
      long s = 0;
      for (auto x : t) s += x;
      return -s;
    };
    std::function<long (const type_t&)> first_desc = [] (const type_t& t) {
      return t.empty () ? 0L : (long) t[0];
    };
    std::function<long (const type_t&)> first_asc = [] (const type_t& t) {
      return t.empty () ? 0L : -(long) t[0];
    };
    return {sum_desc, sum_asc, first_desc, first_asc};
  }

}  // namespace symmetry
