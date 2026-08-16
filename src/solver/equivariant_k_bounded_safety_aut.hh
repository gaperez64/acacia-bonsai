#pragma once

// Exact equivariant helpers for the K-bounded safety solver.
//
// This path keeps the classic raw antichain representation and uses verified
// client symmetries only to avoid recomputing backward sets for input letters
// that are permutations of one another. Small groups use an explicit orbit
// sweep when that is cheaper than repeatedly closing a large antichain under
// generators. It deliberately does not use the count-vector quotient downset
// pipeline.

#include "actioners/direction.hh"
#include "actioners/standard.hh"
#include "configuration.hh"
#include "solver/diagnostics.hh"
#include "solver/symmetric_blocks.hh"
#include "solver/symmetric_profile.hh"
#include "solver/symmetry.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <bddx.h>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <spot/twa/twagraph.hh>
#include <utility>
#include <vector>

#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>

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
        for (const auto& e : aut->out (p))
          if ((e.cond & letter) != bddfalse)
            ts.push_back (std::pair ((int) p, (int) e.dst));
      }
      return ts;
    }

    inline bool build_client_family_vars (const spot::twa_graph_ptr& aut, const symmetry::group& G,
                                          const symmetry::block_layout& L, bool want_input,
                                          std::vector<std::vector<int>>& family_slot_vars,
                                          std::set<int>& indexed_input_vars) {
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
                                           std::vector<std::vector<unsigned>>& out, size_t cap) {
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
    inline bdd input_letter_from_slot_types (const std::vector<std::vector<int>>& family_slot_vars,
                                             const std::vector<int>& shared_vars_,
                                             const std::vector<unsigned>& slot_types,
                                             unsigned shared_mask) {
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

  struct orbit_build_result {
      std::optional<std::vector<input_orbit>> orbits;
      const char* decline_reason = nullptr;

      [[nodiscard]] bool has_value () const { return orbits.has_value (); }
      [[nodiscard]] bool empty () const { return not orbits.has_value () or orbits->empty (); }
      auto& operator* () { return *orbits; }
      const auto& operator* () const { return *orbits; }
      auto* operator->() { return &*orbits; }
      const auto* operator->() const { return &*orbits; }
  };

  inline orbit_build_result build_orbits (const spot::twa_graph_ptr& aut, bdd all_inputs,
                                          bdd all_outputs, const symmetry::group& G,
                                          const symmetry::block_layout& L) {
    std::vector<std::vector<int>> family_slot_vars;
    std::set<int> indexed_input_vars;
    if (not detail::build_client_family_vars (aut, G, L, true, family_slot_vars,
                                              indexed_input_vars))
      return {std::nullopt, "no input family slot vars"};
    if (family_slot_vars.size () >= 8 * sizeof (unsigned))
      return {std::nullopt, "too many indexed input families"};

    const unsigned num_types = 1U << (unsigned) family_slot_vars.size ();
    auto type_counts = detail::enumerate_type_counts (L.num_clients, num_types,
                                                      ACACIA_EQUIVARIANT_MAX_ORBITS + 1);
    if (type_counts.size () > ACACIA_EQUIVARIANT_MAX_ORBITS)
      return {std::nullopt, "type-count orbit cap"};

    auto shared_input_vars = detail::shared_vars (aut, all_inputs, indexed_input_vars);
    if (shared_input_vars.size () >= 20)
      return {std::nullopt, "too many shared input vars"};
    const size_t shared_assignments = ((size_t) 1) << shared_input_vars.size ();
    if (shared_assignments * type_counts.size () > ACACIA_EQUIVARIANT_MAX_ORBITS)
      return {std::nullopt, "shared x type orbit cap"};

    std::vector<bdd> output_letters;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_output_enumerate);
      output_letters =
          detail::enumerate_letters (all_outputs, ACACIA_EQUIVARIANT_MAX_OUTPUT_LETTERS);
    }
    if (output_letters.empty ())
      return {std::nullopt, "output letter cap"};

    std::vector<input_orbit> orbits;
    orbits.reserve (shared_assignments * type_counts.size ());
    for (unsigned shared_mask = 0; shared_mask < shared_assignments; ++shared_mask) {
      for (const auto& counts : type_counts) {
        bdd input = detail::input_letter_from_counts (family_slot_vars, shared_input_vars, counts,
                                                      shared_mask);
        input_orbit orbit;
        orbit.canonical_types.reserve (L.num_clients);
        for (unsigned type = 0; type < counts.size (); ++type)
          for (unsigned i = 0; i < counts[type]; ++i)
            orbit.canonical_types.push_back (type);

        {
          ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_action_dedup);
          std::set<action_vec> uniq;
          for (bdd output : output_letters)
            uniq.insert (
                detail::compute_action_vec (aut, detail::compute_transset (aut, input & output)));
          orbit.actions.assign (uniq.begin (), uniq.end ());
        }
        orbits.push_back (std::move (orbit));
      }
    }
    return {std::move (orbits), nullptr};
  }

  struct representative_build_result {
      std::optional<std::vector<bdd>> letters;
      const char* decline_reason = nullptr;

      [[nodiscard]] bool has_value () const { return letters.has_value (); }
      [[nodiscard]] bool empty () const { return not letters.has_value () or letters->empty (); }
      auto& operator* () { return *letters; }
      const auto& operator* () const { return *letters; }
      auto* operator->() { return &*letters; }
      const auto* operator->() const { return &*letters; }
  };

  // Enumerate one complete input letter per orbit.  Indexed client inputs are
  // represented by their per-client Boolean type counts; shared inputs remain
  // explicit.  No output enumeration is needed because the configured classic
  // IO precomputer supplies the representative actions below.
  inline representative_build_result build_representative_letters (
      const spot::twa_graph_ptr& aut, bdd all_inputs, const symmetry::group& G,
      const symmetry::block_layout& L) {
    std::vector<std::vector<int>> family_slot_vars;
    std::set<int> indexed_input_vars;
    if (not detail::build_client_family_vars (aut, G, L, true, family_slot_vars,
                                              indexed_input_vars))
      return {std::nullopt, "no input family slot vars"};
    if (family_slot_vars.size () >= 8 * sizeof (unsigned))
      return {std::nullopt, "too many indexed input families"};

    const unsigned num_types = 1U << (unsigned) family_slot_vars.size ();
    auto type_counts = detail::enumerate_type_counts (L.num_clients, num_types,
                                                      ACACIA_EQUIVARIANT_MAX_ORBITS + 1);
    if (type_counts.size () > ACACIA_EQUIVARIANT_MAX_ORBITS)
      return {std::nullopt, "type-count orbit cap"};

    auto shared_input_vars = detail::shared_vars (aut, all_inputs, indexed_input_vars);
    if (shared_input_vars.size () >= 20)
      return {std::nullopt, "too many shared input vars"};
    const size_t shared_assignments = ((size_t) 1) << shared_input_vars.size ();
    if (shared_assignments * type_counts.size () > ACACIA_EQUIVARIANT_MAX_ORBITS)
      return {std::nullopt, "shared x type orbit cap"};

    std::vector<bdd> representatives;
    representatives.reserve (shared_assignments * type_counts.size ());
    for (unsigned shared_mask = 0; shared_mask < shared_assignments; ++shared_mask)
      for (const auto& counts : type_counts)
        representatives.push_back (detail::input_letter_from_counts (
            family_slot_vars, shared_input_vars, counts, shared_mask));
    return {std::move (representatives), nullptr};
  }

  template <typename InputsToIOs>
  void filter_to_representative_inputs (InputsToIOs& inputs_to_ios,
                                        const std::vector<bdd>& representatives) {
    bdd representative_inputs = bddfalse;
    for (bdd representative : representatives)
      representative_inputs |= representative;

    if constexpr (requires { inputs_to_ios.restrict_inputs (representative_inputs); }) {
      inputs_to_ios.restrict_inputs (representative_inputs);
    }
    else {
      for (auto it = inputs_to_ios.begin (); it != inputs_to_ios.end ();) {
        const bool covers_representative = (it->first & representative_inputs) != bddfalse;
        if (covers_representative)
          ++it;
        else
          it = inputs_to_ios.erase (it);
      }
    }
  }

  // sigma[j] = the member slot that receives the client sitting at
  // representative slot j. c is sorted ascending; s is a permutation of the
  // same multiset. Pair the k-th slot of type t in c with the k-th slot of
  // type t in s.
  inline std::vector<unsigned> match_slots (const std::vector<unsigned>& c,
                                            const std::vector<unsigned>& s, unsigned num_types) {
    const unsigned n = (unsigned) c.size ();
    std::vector<std::vector<unsigned>> slots_of_type (num_types);
    for (unsigned j = 0; j < n; ++j)
      slots_of_type[s[j]].push_back (j);
    std::vector<unsigned> next (num_types, 0), sigma (n);
    for (unsigned j = 0; j < n; ++j)
      sigma[j] = slots_of_type[c[j]][next[c[j]]++];
    return sigma;
  }

  // F3: layout-induced state permutation of a slot permutation.
  inline std::vector<unsigned> phi_from_sigma (const symmetry::block_layout& L,
                                               const std::vector<unsigned>& sigma) {
    std::vector<unsigned> phi (L.num_states);
    for (unsigned q = 0; q < L.num_states; ++q)
      phi[q] = q;
    for (unsigned blk = 0; blk < L.num_blocks; ++blk)
      for (unsigned j = 0; j < L.num_clients; ++j)
        phi[L.block_slot_state[blk][j]] = L.block_slot_state[blk][sigma[j]];
    return phi;
  }

  // T_member = phi . T_rep, using the group action (F4): out[phi[q]] = in[q].
  // A coordinate permutation is an order isomorphism.  It therefore maps the
  // maximal-element antichain bijectively to another antichain, making the
  // trusted vector_backed bulk factory exact.  Other downset backends retain
  // the generic apply() fallback because they may have additional invariants.
  template <typename SetOfStates>
  SetOfStates permute (const SetOfStates& T, const std::vector<unsigned>& phi,
                       posets::utils::vector_mm<VECTOR_ELT_T>& scratch_in,
                       posets::utils::vector_mm<VECTOR_ELT_T>& scratch_out) {
    using state = typename SetOfStates::value_type;
    assert (scratch_in.size () >= phi.size ());
    assert (scratch_out.size () >= phi.size ());
    if constexpr (requires (std::vector<state>&& elements) {
                    SetOfStates::from_antichain_unchecked (std::move (elements));
                  }) {
      std::vector<state> elements;
      elements.reserve (T.size ());
      for (const auto& s : T) {
        assert (s.size () == phi.size ());
        s.to_vector (std::span (scratch_in.data (), phi.size ()));
        for (size_t q = 0; q < phi.size (); ++q)
          scratch_out[phi[q]] = scratch_in[q];
        elements.emplace_back (std::span<const VECTOR_ELT_T> (scratch_out.data (), phi.size ()));
      }
      return SetOfStates::from_antichain_unchecked (std::move (elements));
    }
    else {
      return T.apply ([&phi] (const auto& s) {
        posets::utils::vector_mm<VECTOR_ELT_T> out (s.size (), 0);
        for (size_t q = 0; q < s.size (); ++q)
          out[phi[q]] = s[q];
        return state (out);
      });
    }
  }

  template <typename SetOfStates>
  SetOfStates permute (const SetOfStates& T, const std::vector<unsigned>& phi) {
    posets::utils::vector_mm<VECTOR_ELT_T> scratch_in (phi.size (), 0);
    posets::utils::vector_mm<VECTOR_ELT_T> scratch_out (phi.size (), 0);
    return permute (T, phi, scratch_in, scratch_out);
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
      acacia::diagnostics::observe_action ();
      SetOfStates Tio = [&] {
        acacia::diagnostics::scoped_downset_timer downset_timer;
        return f.apply ([&] (const auto& m) {
          acacia::diagnostics::scoped_fine_timer apply_timer {
              acacia::diagnostics::fine_metric::apply};
          return actioner.apply (m, avec, actioners::direction::backward);
        });
      } ();
      if (first) {
        T = std::move (Tio);
        first = false;
      }
      else {
        acacia::diagnostics::scoped_downset_timer downset_timer;
        T.union_with (std::move (Tio));
      }
      acacia::diagnostics::snapshot_action_progress ();
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

  // Sound GFP tightening.  If f contains the winning region W, then every
  // verified automorphism phi also has phi(f) containing phi(W)=W.  Thus
  // intersecting f with generator images cannot remove W.  At a fixpoint for
  // the verified transposition generators, f is invariant under the generated
  // group.  This is closure of the raw downset, not representative
  // canonicalization.
  template <typename SetOfStates>
  bool close_under_generators (SetOfStates& f, const symmetry::group& G,
                               posets::utils::vector_mm<VECTOR_ELT_T>& scratch_in,
                               posets::utils::vector_mm<VECTOR_ELT_T>& scratch_out) {
    ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_closure);
    bool any_changed = false;
    bool pass_changed;
    do {
      pass_changed = false;
      for (const auto& phi : G.gens) {
        SetOfStates image = permute (f, phi, scratch_in, scratch_out);
        if (not subset_of (f, image)) {
          f.intersect_with (std::move (image));
          pass_changed = true;
          any_changed = true;
        }
      }
    } while (pass_changed);
    return any_changed;
  }

  template <typename SetOfStates>
  bool close_under_generators (SetOfStates& f, const symmetry::group& G) {
    const size_t dimension = G.gens.empty () ? 0 : G.gens.front ().size ();
    posets::utils::vector_mm<VECTOR_ELT_T> scratch_in (dimension, 0);
    posets::utils::vector_mm<VECTOR_ELT_T> scratch_out (dimension, 0);
    return close_under_generators (f, G, scratch_in, scratch_out);
  }

  template <typename SetOfStates, typename PickedInput, typename Actioner>
  void cpre_inplace (SetOfStates& f, const PickedInput& picked_input, Actioner& actioner,
                     unsigned num_states) {
    ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_cpre);
    acacia::diagnostics::scoped_fine_timer cpre_timer {
        acacia::diagnostics::fine_metric::cpre};
    const auto& [input, actions] = picked_input.get ();
    (void) input;
    posets::utils::vector_mm<VECTOR_ELT_T> bottom (num_states, -1);
    SetOfStates predecessors {typename SetOfStates::value_type (bottom)};
    bool first = true;
    for (const auto& action_vec : actions) {
      acacia::diagnostics::observe_action ();
      SetOfStates for_output = [&] {
        acacia::diagnostics::scoped_downset_timer downset_timer;
        return f.apply ([&] (const auto& maximal) {
          acacia::diagnostics::scoped_fine_timer apply_timer {
              acacia::diagnostics::fine_metric::apply};
          return actioner.apply (maximal, action_vec, actioners::direction::backward);
        });
      } ();
      if (first) {
        predecessors = std::move (for_output);
        first = false;
      }
      else {
        acacia::diagnostics::scoped_downset_timer downset_timer;
        predecessors.union_with (std::move (for_output));
      }
      acacia::diagnostics::snapshot_action_progress ();
    }
    acacia::diagnostics::observe_meets (f.size (), predecessors.size ());
    acacia::diagnostics::snapshot_intersection_progress ();
    acacia::diagnostics::scoped_downset_timer downset_timer;
    f.intersect_with (std::move (predecessors));
  }

  template <typename SetOfStates>
  struct result {
      bool attempted = false;
      std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> win;
  };

  inline bool boolean_side_consistent (const symmetry::group& G) {
    for (const auto& g : G.gens)
      for (unsigned q = 0; q < g.size (); ++q)
        if ((q < posets::vectors::bool_threshold) != (g[q] < posets::vectors::bool_threshold))
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

  // For small verified groups, explicitly sweeping the concrete members of
  // every input orbit avoids the potentially much larger intermediate
  // antichains created by generator-fixpoint closure.  This is the original
  // exact equivariant algorithm, now using the trusted permutation fast path.
  template <typename SetOfStates, typename ActionerMaker>
  result<SetOfStates> solve_orbit_sweep (const spot::twa_graph_ptr& aut, VECTOR_ELT_T kmax,
                                         VECTOR_ELT_T kmin, VECTOR_ELT_T kinc,
                                         const bdd& all_inputs, const bdd& all_outputs,
                                         const symmetry::group& G, const symmetry::block_layout& L,
                                         const ActionerMaker& actioner_maker) {
    using state = typename SetOfStates::value_type;
    const unsigned num_states = aut->num_states ();

    orbit_build_result orbits;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_orbit_build);
      orbits = build_orbits (aut, all_inputs, all_outputs, G, L);
    }
    if (not orbits.has_value () or orbits->empty ()) {
      const char* reason =
          orbits.decline_reason ? orbits.decline_reason : "empty input orbit universe";
      acacia::diagnostics::set_equivariant_decline (reason);
      return {false, std::nullopt};
    }

    const unsigned num_types = orbit_type_count (*orbits);
    if (num_types == 0) {
      acacia::diagnostics::set_equivariant_decline ("empty input type universe");
      return {false, std::nullopt};
    }

    acacia::diagnostics::set_equivariant_attempt (L.num_clients, L.num_blocks, orbits->size ());
    std::vector<std::pair<bdd, std::vector<transset>>> empty_itoios;
    VECTOR_ELT_T k = kmin;
    auto actioner = actioner_maker.make (aut, empty_itoios, k);

    posets::utils::vector_mm<VECTOR_ELT_T> init (num_states, -1);
    init[aut->get_init_state_number ()] = 0;
    auto safe_vector = posets::utils::vector_mm<VECTOR_ELT_T> (num_states, k - 1);
    for (size_t q = posets::vectors::bool_threshold; q < num_states; ++q)
      safe_vector[q] = 0;
    SetOfStates f {state (safe_vector)};
    posets::utils::vector_mm<VECTOR_ELT_T> permute_in (num_states, 0);
    posets::utils::vector_mm<VECTOR_ELT_T> permute_out (num_states, 0);

    acacia::diagnostics::snapshot ("after-action-construction");
    ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_solve_loop);
    while (true) {
      acacia::diagnostics::observe_loop (f.size (), k);
      bool changed = false;
      bool incremented = false;

      for (const auto& orbit : *orbits) {
        acacia::diagnostics::scoped_fine_timer cpre_timer {
            acacia::diagnostics::fine_metric::cpre};
        SetOfStates representative = compute_T (f, orbit.actions, actioner, num_states);
        std::vector<unsigned> sequence = orbit.canonical_types;
        do {
          const auto sigma = match_slots (orbit.canonical_types, sequence, num_types);
          SetOfStates member =
              permute (representative, phi_from_sigma (L, sigma), permute_in, permute_out);
          if (not subset_of (f, member)) {
            acacia::diagnostics::observe_meets (f.size (), member.size ());
            acacia::diagnostics::snapshot_intersection_progress ();
            acacia::diagnostics::scoped_downset_timer downset_timer;
            f.intersect_with (std::move (member));
            changed = true;
          }
        } while (std::next_permutation (sequence.begin (), sequence.end ()));

        if (not f.contains (state (init))) {
          if (k >= kmax) {
            acacia::diagnostics::set_final_reason ("kmax-initial-out");
            return {true, std::nullopt};
          }
          k += kinc;
          actioner.setK (k);
          f = f.apply ([&] (const state& maximal) {
            posets::utils::vector_mm<VECTOR_ELT_T> bumped (maximal.size (), 0);
            for (size_t q = 0; q < posets::vectors::bool_threshold; ++q)
              bumped[q] = maximal[q] + kinc;
            return state (bumped);
          });
          incremented = true;
          break;
        }
      }

      if (incremented)
        continue;
      if (not changed) {
        acacia::diagnostics::set_final_reason ("fixedpoint");
        std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> win;
        win.emplace (k, std::move (f));
        return {true, std::move (win)};
      }
    }
  }

  template <typename SetOfStates, typename IOsPrecomputerMaker, typename ActionerMaker,
            typename InputPickerMaker>
  result<SetOfStates> try_solve (spot::twa_graph_ptr aut, VECTOR_ELT_T kmax, VECTOR_ELT_T kmin,
                                 VECTOR_ELT_T kinc, const bdd& all_inputs, const bdd& all_outputs,
                                 const IOsPrecomputerMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker) {
    using state = typename SetOfStates::value_type;
    const unsigned num_states = aut->num_states ();

#if ACACIA_SYMMETRY_PROFILE
    struct equivariant_profile_reporter {
        ~equivariant_profile_reporter () {
          acacia::solver_detail::symmetric::profile::global ().report ();
        }
    } profile_reporter;
    acacia::solver_detail::symmetric::profile::global ().reset ();
#endif

    auto decline = [] (const char* reason) {
      verb_do (1, vout << "[equivariant] declining: " << reason << "\n");
      acacia::diagnostics::set_equivariant_decline (reason);
      return result<SetOfStates> {false, std::nullopt};
    };

    if (num_states > ACACIA_EQUIVARIANT_MAX_STATES and
        not symmetry::has_indexed_family_certificate_hypothesis ())
      return decline ("too many automaton states");

    symmetry::indexed_ap_analysis indexed;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_ap_scan);
      indexed = symmetry::analyze_indexed_aps (aut, all_inputs, all_outputs);
    }
    if (num_states > ACACIA_EQUIVARIANT_MAX_STATES and not indexed.syntax_certified)
      return decline ("too many automaton states without a matching syntax certificate");
    if (indexed.empty ())
      return decline ("no indexed AP families");
    if (indexed.input_families == 0)
      return decline ("no indexed input AP families");
    if (indexed.indices.size () < ACACIA_EQUIVARIANT_MIN_CLIENTS)
      return decline ("too few indexed clients");

    symmetry::group G;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_detect);
      G = symmetry::detect_full_symmetric_generators (aut, indexed);
    }
    if (not G.full_symmetric) {
      std::string reason = "not a verified full symmetric group (";
      reason += std::to_string (G.gens.size ());
      reason += "/";
      reason += std::to_string (G.indices.empty () ? 0 : G.indices.size () - 1);
      reason += " star gens)";
      verb_do (1, vout << "[equivariant] declining: " << reason << "\n");
      acacia::diagnostics::set_equivariant_decline (reason);
      return {false, std::nullopt};
    }

    std::optional<symmetry::block_layout> L;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_block_layout);
      L = symmetry::compute_block_layout (G, num_states);
    }
    if (not L.has_value ())
      return decline ("no usable block layout");

    if (ACACIA_EQUIVARIANT_MIN_BLOCKS > 0 and L->num_blocks < ACACIA_EQUIVARIANT_MIN_BLOCKS) {
      verb_do (1, vout << "[equivariant] declining: low block payoff"
                       << " clients=" << L->num_clients << " blocks=" << L->num_blocks
                       << " min_blocks=" << ACACIA_EQUIVARIANT_MIN_BLOCKS << "\n");
      return decline ("low block payoff");
    }

    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_generator_match);
      if (not symmetry::generators_match_layout (G, *L))
        return decline ("generators do not match block layout");
    }

    if (not boolean_side_consistent (G))
      return decline ("generator crosses boolean/counting threshold");

    if (ACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS > 0 and
        L->num_clients <= ACACIA_EQUIVARIANT_MAX_SWEEP_CLIENTS)
      return solve_orbit_sweep<SetOfStates> (aut, kmax, kmin, kinc, all_inputs, all_outputs, G, *L,
                                             actioner_maker);

    representative_build_result representatives;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_orbit_build);
      representatives = build_representative_letters (aut, all_inputs, G, *L);
    }
    if (not representatives.has_value () or representatives->empty ())
      return decline (representatives.decline_reason ? representatives.decline_reason
                                                     : "empty input orbit universe");

    verb_do (1, vout << "[equivariant] trying solver: clients=" << L->num_clients << " blocks="
                     << L->num_blocks << " orbits=" << representatives->size () << "\n");
    acacia::diagnostics::set_equivariant_attempt (L->num_clients, L->num_blocks,
                                                  representatives->size ());

    VECTOR_ELT_T k = kmin;
    auto inputs_to_ios = (ios_precomputer_maker.make (aut, all_inputs, all_outputs)) ();
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_representative_filter);
      filter_to_representative_inputs (inputs_to_ios, *representatives);
    }
    if (inputs_to_ios.empty ())
      return decline ("no precomputed representative inputs");
    auto actioner = actioner_maker.make (aut, inputs_to_ios, k);
    auto fwd_actions = actioner.actions ();
    if (fwd_actions.empty ())
      return decline ("no representative actions");
    auto input_picker = input_picker_maker.make (fwd_actions, actioner);

    posets::utils::vector_mm<VECTOR_ELT_T> init (num_states);
    init.assign (num_states, -1);
    init[aut->get_init_state_number ()] = 0;

    auto safe_vector = posets::utils::vector_mm<VECTOR_ELT_T> (num_states, k - 1);
    for (size_t i = posets::vectors::bool_threshold; i < num_states; ++i)
      safe_vector[i] = 0;
    SetOfStates f = SetOfStates (state (safe_vector));

    posets::utils::vector_mm<VECTOR_ELT_T> permute_in (num_states, 0);
    posets::utils::vector_mm<VECTOR_ELT_T> permute_out (num_states, 0);
#ifndef NDEBUG
    assert (not close_under_generators (f, G, permute_in, permute_out));
#endif

    int loopcount = 0;
    acacia::diagnostics::snapshot ("after-action-construction");
    ACACIA_SYMMETRY_PROFILE_SCOPE (equivariant_solve_loop);
    while (true) {
      ++loopcount;
      acacia::diagnostics::observe_loop (f.size (), k);
      verb_do (1, vout << "[equivariant] Loop# " << loopcount << ", f of size " << f.size ()
                       << ", representative inputs=" << fwd_actions.size () << "\n");

      auto input = [&] {
        acacia::diagnostics::scoped_fine_timer timer {
            acacia::diagnostics::fine_metric::picker};
        return input_picker (f);
      } ();
      acacia::diagnostics::snapshot_loop_progress ("equivariant-after-picker");
      if (not input.has_value ()) {
        verb_do (1, vout << "[equivariant] fixed point reached at K=" << (int) k << ", f of size "
                         << f.size () << "\n");
        acacia::diagnostics::set_final_reason ("fixedpoint");
        std::optional<std::pair<VECTOR_ELT_T, SetOfStates>> win;
        win.emplace (k, std::move (f));
        return {true, std::move (win)};
      }

      cpre_inplace (f, *input, actioner, num_states);
      acacia::diagnostics::snapshot_loop_progress ("equivariant-after-cpre");
      close_under_generators (f, G, permute_in, permute_out);
      acacia::diagnostics::snapshot_loop_progress ("equivariant-after-closure");

      if (not f.contains (state (init))) {
        if (k >= kmax) {
          verb_do (1, vout << "[equivariant] initial state out at max K\n");
          acacia::diagnostics::set_final_reason ("kmax-initial-out");
          return {true, std::nullopt};
        }
        verb_do (1, vout << "[equivariant] Incrementing k from " << (int) k << " to "
                         << (int) (k + kinc) << "\n");
        k += kinc;
        actioner.setK (k);
        f = f.apply ([&] (const state& s) {
          auto vec = posets::utils::vector_mm<VECTOR_ELT_T> (s.size (), 0);
          for (size_t i = 0; i < posets::vectors::bool_threshold; ++i)
            vec[i] = s[i] + kinc;
          return state (vec);
        });
        acacia::diagnostics::snapshot_loop_progress ("equivariant-after-k-bump");
#ifndef NDEBUG
        // The K-bump is uniform on every counting coordinate and generators
        // do not cross bool_threshold, so it commutes with every generator.
        assert (not close_under_generators (f, G, permute_in, permute_out));
#endif
      }
    }
  }

}  // namespace acacia::solver_detail::equivariant
