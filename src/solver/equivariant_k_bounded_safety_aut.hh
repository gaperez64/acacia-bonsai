#pragma once

// Exact equivariant helpers for the K-bounded safety solver.
//
// This path keeps the classic raw antichain representation and uses verified
// client symmetries only to avoid recomputing backward sets for input letters
// that are permutations of one another. It deliberately does not use the
// count-vector quotient downset pipeline.

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

namespace acacia::solver_detail::equivariant {

  using transset = std::vector<std::pair<int, int>>;
  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;

  struct input_orbit {
      // Sorted ASCENDING; this is the orbit representative's slot->type map.
      std::vector<unsigned> canonical_types;
      // Deduplicated backward actions of the representative letter, over ALL
      // output letters. Deduplication of identical action_vecs is exact for
      // the union.
      std::vector<action_vec> actions;
  };

  namespace detail {

    inline bool support_contains_var (bdd support, int var) {
      const bdd v = bdd_ithvar (var);
      return bdd_exist (support, v) != support;
    }

    inline std::vector<bdd> enumerate_letters (bdd support, size_t cap) {
      std::vector<bdd> letters;
      bdd remaining = bddtrue;
      while (remaining != bddfalse) {
        if (letters.size () >= cap)
          return {};
        bdd letter = bdd_satoneset (remaining, support, bddtrue);
        letters.push_back (letter);
        remaining -= letter;
      }
      return letters;
    }

    inline transset compute_transset (const spot::twa_graph_ptr& aut, bdd letter) {
      transset ts;
      for (size_t p = 0; p < aut->num_states (); ++p) {
        for (const auto& e : aut->out (p)) {
          if ((e.cond & letter) != bddfalse)
            ts.push_back (std::pair ((int) p, (int) e.dst));
        }
      }
      return ts;
    }

    inline bool build_client_family_vars (
        const spot::twa_graph_ptr& aut, const symmetry::group& G, const symmetry::block_layout& L,
        bool want_input,
        std::vector<std::vector<int>>& family_slot_vars, std::set<int>& indexed_input_vars) {
      auto dict = aut->get_dict ();
      for (const auto& [prefix, fam] : G.families) {
        if (fam.is_input != want_input)
          continue;
        std::vector<int> slot_vars (L.num_clients, -1);
        for (unsigned slot = 0; slot < L.num_clients; ++slot) {
          const long idx = L.slot_to_index[slot];
          auto it = fam.idx2ap.find (idx);
          if (it == fam.idx2ap.end ())
            return false;
          const int var = dict->varnum (it->second);
          slot_vars[slot] = var;
          indexed_input_vars.insert (var);
        }
        family_slot_vars.push_back (std::move (slot_vars));
      }
      return not family_slot_vars.empty ();
    }

    inline std::vector<int> shared_vars (const spot::twa_graph_ptr& aut, bdd support,
                                         const std::set<int>& indexed_vars) {
      std::vector<int> vars;
      auto dict = aut->get_dict ();
      for (const spot::formula& ap : aut->ap ()) {
        const int var = dict->varnum (ap);
        if (indexed_vars.contains (var))
          continue;
        if (support_contains_var (support, var))
          vars.push_back (var);
      }
      std::sort (vars.begin (), vars.end ());
      return vars;
    }

    inline void enumerate_type_counts_rec (unsigned type, unsigned num_types, unsigned remaining,
                                           std::vector<unsigned>& counts,
                                           std::vector<std::vector<unsigned>>& out,
                                           size_t cap) {
      if (out.size () >= cap)
        return;
      if (type + 1 == num_types) {
        counts[type] = remaining;
        out.push_back (counts);
        return;
      }
      for (unsigned c = 0; c <= remaining; ++c) {
        counts[type] = c;
        enumerate_type_counts_rec (type + 1, num_types, remaining - c, counts, out, cap);
        if (out.size () >= cap)
          return;
      }
    }

    inline std::vector<std::vector<unsigned>> enumerate_type_counts (unsigned num_clients,
                                                                     unsigned num_types,
                                                                     size_t cap) {
      std::vector<std::vector<unsigned>> out;
      std::vector<unsigned> counts (num_types, 0);
      enumerate_type_counts_rec (0, num_types, num_clients, counts, out, cap);
      return out;
    }

    inline bdd input_letter_from_counts (const std::vector<std::vector<int>>& family_slot_vars,
                                         const std::vector<int>& shared_vars_,
                                         const std::vector<unsigned>& type_counts,
                                         unsigned shared_mask) {
      bdd letter = bddtrue;
      unsigned slot = 0;
      for (unsigned type = 0; type < type_counts.size (); ++type) {
        for (unsigned n = 0; n < type_counts[type]; ++n, ++slot) {
          for (unsigned fam = 0; fam < family_slot_vars.size (); ++fam) {
            const bdd v = bdd_ithvar (family_slot_vars[fam][slot]);
            letter &= ((type >> fam) & 1U) ? v : !v;
          }
        }
      }
      for (unsigned i = 0; i < shared_vars_.size (); ++i) {
        const bdd v = bdd_ithvar (shared_vars_[i]);
        letter &= ((shared_mask >> i) & 1U) ? v : !v;
      }
      return letter;
    }

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

    inline action_vec compute_action_vec (const spot::twa_graph_ptr& aut, const transset& ts) {
      action_vec ret (aut->num_states ());
      for (const auto& [p, q] : ts)
        ret[q].push_back (std::make_pair ((unsigned) p, aut->state_is_accepting (q)));
      return ret;
    }

  }  // namespace detail

  inline std::optional<std::vector<input_orbit>> build_orbits (
      const spot::twa_graph_ptr& aut, bdd all_inputs, bdd all_outputs,
      const symmetry::group& G, const symmetry::block_layout& L) {
    std::vector<std::vector<int>> family_slot_vars;
    std::set<int> indexed_input_vars;
    if (not detail::build_client_family_vars (aut, G, L, true, family_slot_vars,
                                              indexed_input_vars))
      return std::nullopt;
    if (family_slot_vars.size () >= 8 * sizeof (unsigned))
      return std::nullopt;

    const unsigned num_types = 1U << (unsigned) family_slot_vars.size ();
    auto type_counts = detail::enumerate_type_counts (L.num_clients, num_types,
                                                      ACACIA_EQUIVARIANT_MAX_ORBITS + 1);
    if (type_counts.size () > ACACIA_EQUIVARIANT_MAX_ORBITS)
      return std::nullopt;

    auto shared_input_vars = detail::shared_vars (aut, all_inputs, indexed_input_vars);
    if (shared_input_vars.size () >= 20)
      return std::nullopt;
    const size_t shared_assignments = ((size_t) 1) << shared_input_vars.size ();
    if (shared_assignments * type_counts.size () > ACACIA_EQUIVARIANT_MAX_ORBITS)
      return std::nullopt;

    auto output_letters = detail::enumerate_letters (all_outputs,
                                                     ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS);
    if (output_letters.empty ())
      return std::nullopt;

    std::vector<input_orbit> orbits;
    orbits.reserve (shared_assignments * type_counts.size ());
    for (unsigned shared_mask = 0; shared_mask < shared_assignments; ++shared_mask) {
      for (const auto& counts : type_counts) {
        bdd input =
            detail::input_letter_from_counts (family_slot_vars, shared_input_vars, counts,
                                              shared_mask);
        input_orbit orbit;
        orbit.canonical_types.reserve (L.num_clients);
        for (unsigned type = 0; type < counts.size (); ++type)
          for (unsigned i = 0; i < counts[type]; ++i)
            orbit.canonical_types.push_back (type);

        std::set<action_vec> uniq;
        for (bdd output : output_letters)
          uniq.insert (
              detail::compute_action_vec (aut, detail::compute_transset (aut, input & output)));
        orbit.actions.assign (uniq.begin (), uniq.end ());
        orbits.push_back (std::move (orbit));
      }
    }
    return orbits;
  }

  // sigma[j] = the member slot that receives the client sitting at
  // representative slot j. c is sorted ascending; s is a permutation of the
  // same multiset. Pair the k-th slot of type t in c with the k-th slot of
  // type t in s.
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

  // Exact T = union over ALL outputs of the backward step.
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
      if (first) {
        T = std::move (Tio);
        first = false;
      }
      else
        T.union_with (std::move (Tio));
    }
    return T;
  }

  // f is a subset of the downset T iff every maximal element of f is in T.
  template <typename SetOfStates>
  bool subset_of (const SetOfStates& f, const SetOfStates& T) {
    for (const auto& m : f)
      if (not T.contains (m))
        return false;
    return true;
  }

}  // namespace acacia::solver_detail::equivariant
