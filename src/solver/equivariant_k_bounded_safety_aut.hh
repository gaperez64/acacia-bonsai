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
#include <cstddef>
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

  template <typename SetOfStates>
  struct result {
      bool attempted = false;
      std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> win;
  };

  inline bool boolean_side_consistent (const symmetry::group& G) {
    for (const auto& g : G.gens)
      for (unsigned q = 0; q < g.size (); ++q)
        if ((q < posets::vectors::bool_threshold) !=
            (g[q] < posets::vectors::bool_threshold))
          return false;
    return true;
  }

  inline unsigned orbit_type_count (const std::vector<input_orbit>& orbits) {
    unsigned count = 0;
    for (const auto& orbit : orbits)
      for (unsigned type : orbit.canonical_types)
        count = std::max (count, type + 1);
    return count;
  }

  template <typename SetOfStates>
  result<SetOfStates> try_solve (spot::twa_graph_ptr aut, VECTOR_ELT_T kmax,
                                 VECTOR_ELT_T kmin, VECTOR_ELT_T kinc,
                                 const bdd& all_inputs, const bdd& all_outputs) {
    using state = typename SetOfStates::value_type;
    const unsigned num_states = aut->num_states ();

    auto decline = [] (const char* reason) {
      verb_do (1, vout << "[equivariant] declining: " << reason << "\n");
      return result<SetOfStates> {false, std::nullopt};
    };

    if (num_states > ACACIA_EQUIVARIANT_MAX_STATES)
      return decline ("too many automaton states");

    const auto G = symmetry::detect (aut, all_inputs, all_outputs);
    if (not G.full_symmetric)
      return decline ("not a verified full symmetric group");

    auto L = symmetry::compute_block_layout (G, num_states);
    if (not L.has_value ())
      return decline ("no usable block layout");

    if (not symmetry::generators_match_layout (G, *L))
      return decline ("generators do not match block layout");

    if (not boolean_side_consistent (G))
      return decline ("generator crosses boolean/counting threshold");

    auto orbits = build_orbits (aut, all_inputs, all_outputs, G, *L);
    if (not orbits.has_value () or orbits->empty ())
      return decline ("input orbit construction failed or was capped");

    const unsigned num_types = orbit_type_count (*orbits);
    if (num_types == 0)
      return decline ("empty input type universe");

    verb_do (1, vout << "[equivariant] trying solver: clients=" << L->num_clients
                     << " blocks=" << L->num_blocks
                     << " orbits=" << orbits->size () << "\n");

    std::vector<std::pair<bdd, std::vector<transset>>> empty_itoios;
    VECTOR_ELT_T k = kmin;
    auto actioner = actioners::standard<state>::make (aut, empty_itoios, k);

    posets::utils::vector_mm<VECTOR_ELT_T> init (num_states);
    init.assign (num_states, -1);
    init[aut->get_init_state_number ()] = 0;

    auto safe_vector = posets::utils::vector_mm<VECTOR_ELT_T> (num_states, k - 1);
    for (size_t i = posets::vectors::bool_threshold; i < num_states; ++i)
      safe_vector[i] = 0;
    SetOfStates f = SetOfStates (state (safe_vector));

    int loopcount = 0;
    while (true) {
      ++loopcount;
      bool changed = false;
      bool incremented = false;
      verb_do (1, vout << "[equivariant] Loop# " << loopcount
                       << ", f of size " << f.size ()
                       << ", input orbits=" << orbits->size () << "\n");

      for (const auto& orbit : *orbits) {
        SetOfStates T_rep = compute_T (f, orbit.actions, actioner, num_states);
        std::vector<unsigned> seq = orbit.canonical_types;
        size_t members = 0;
        do {
          ++members;
          const auto sigma = match_slots (orbit.canonical_types, seq, num_types);
          const auto phi = phi_from_sigma (*L, sigma);
          SetOfStates T_mem = permute (T_rep, phi);
          if (not subset_of (f, T_mem)) {
            f.intersect_with (std::move (T_mem));
            changed = true;
          }
        } while (std::next_permutation (seq.begin (), seq.end ()));
        verb_do (2, vout << "[equivariant] processed orbit with " << members
                         << " members, f size=" << f.size () << "\n");

        if (not f.contains (state (init))) {
          if (k >= kmax) {
            verb_do (1, vout << "[equivariant] initial state out at max K\n");
            return {true, std::nullopt};
          }
          verb_do (1, vout << "[equivariant] Incrementing k from " << (int) k
                           << " to " << (int) (k + kinc) << "\n");
          k += kinc;
          actioner.setK (k);
          f = f.apply ([&] (const state& s) {
            auto vec = posets::utils::vector_mm<VECTOR_ELT_T> (s.size (), 0);
            for (size_t i = 0; i < posets::vectors::bool_threshold; ++i)
              vec[i] = s[i] + kinc;
            return state (vec);
          });
          incremented = true;
          break;
        }
      }

      if (incremented)
        continue;
      if (not changed) {
        verb_do (1, vout << "[equivariant] fixed point reached at K=" << (int) k
                         << ", f of size " << f.size () << "\n");
        std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> win;
        win.emplace (k, std::move (f));
        return {true, std::move (win)};
      }
    }
  }

}  // namespace acacia::solver_detail::equivariant
