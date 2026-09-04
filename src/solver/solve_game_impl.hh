#pragma once

#include "actioners/no_ios_precomputation.hh"
#include "config/component_checks.hh"
#include "ios_precomputers/delegate.hh"
#include "posets/downsets.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors.hh"
#include "posets/vectors/traits.hh"
#include "solver/configured_components.hh"
#include "solver/game_backend.hh"
#if ACACIA_ENABLE_EQUIVARIANT_SOLVER
# include "solver/equivariant_k_bounded_safety_aut.hh"
#endif
#if ACACIA_FORWARD_SAFETY_SOLVER
# include "solver/forward_k_bounded_safety_aut.hh"
#endif
#include "solver/k_bounded_safety_aut.hh"
#include "utils/verbose.hh"
#include <spot/twaalgos/mealy_machine.hh>

#include <bddx.h>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <spot/twa/acc.hh>
#include <spot/twa/twagraph.hh>
#include <utility>
#include <vector>

namespace acacia::solver_detail {

  /**
   * Function to treat the winning region of a game, if any. The winning region
   * having a value means that it contains the initial state.
   */
  template <class SetOfStates>
  std::optional<spot::twa_graph_ptr> post_real (
      std::optional<std::pair<VECTOR_ELT_T, SetOfStates>>&& win_res, bool do_synthesis,
      spot::twa_graph_ptr aut, const bdd& all_inputs, const bdd& all_outputs) {
    using state = typename SetOfStates::value_type;

    if (not win_res.has_value ())
      return std::nullopt;
    if (not do_synthesis)
      return aut;

    // We got here, so there is a winning region and we need to do synthesis.
    const auto& [k, winning_region] = *win_res;

    // What follows is mostly the synthesis procedure as ncharl intended, but
    // rewritten by gaperez64 using spot instead of AIGER.
    //
    // Note: here we use a specific combination of ios_precomputer and actioner;
    // these are NOT necessarily the one dictated by the macros.
    auto actioner_factory = actioners::no_ios_precomputation<state> ();
    auto inputs_to_ios = ios_precomputers::delegate::make (aut, all_inputs, all_outputs) ();
    auto actioner = actioner_factory.make (aut, inputs_to_ios, k);
    auto io_fwd_actions = actioner.actions ();

    verb_do (3,
             vout << "Winning region = downset of size " << winning_region.size () << std::endl);
    verb_do (3, vout << "Found using k = " << (int) k << std::endl);

    // We will use the maxima from the winning region downset as the state of
    // the Mealy machine representation of the controller.
    std::vector<const state*> state_space;
    state_space.reserve (winning_region.size ());

    // First, find the index of the element that dominates the initial state
    // from the universal co-Buchi automaton. While looping over the antichain,
    // populate state_space.
    auto init_vector = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), -1);
    init_vector[aut->get_init_state_number ()] = 0;
    state init_state (init_vector);
    unsigned init_idx = 0;
    bool found = false;
    for (const auto& elem_in_antichain : winning_region) {
      state_space.push_back (&elem_in_antichain);
      if (elem_in_antichain.partial_order (init_state).geq ())
        found = true;
      if (not found)
        init_idx++;
    }
    assert (found and winning_region.size () == state_space.size ());
    verb_do (2, vout << "Index of max element dominating the initial state is " << init_idx
                     << std::endl);

    // We can now start creating the Mealy machine that represents the strategy.
    // It may contain nondeterminism; Spot can handle this when minimizing and
    // producing AIGER circuits.
    spot::twa_graph_ptr mealy = spot::make_twa_graph (aut->get_dict ());
    mealy->copy_ap_of (aut);
    mealy->set_acceptance (spot::acc_cond::acc_code::t ());
    bdd* output_cube = new bdd ();
    *output_cube = all_outputs;
    mealy->set_named_prop<bdd> ("synthesis-outputs", output_cube);
    mealy->new_states (winning_region.size ());
    mealy->set_init_state (init_idx);

    // Populate the Mealy machine by exploring the maxima from the initial
    // state/maximum.
    std::vector<unsigned> states_todo = {init_idx};
    std::vector<bool> visited (state_space.size (), false);
    while (not states_todo.empty ()) {
      unsigned src = states_todo.back ();
      states_todo.pop_back ();
      if (visited[src])
        continue;
      visited[src] = true;
      SetOfStates singleton (state_space[src]->copy ());

      for (auto& [input_letter, action_vecs] : io_fwd_actions) {
        verb_do (2, vout << "Input: " << spot::bdd_to_formula (input_letter, aut->get_dict ())
                         << std::endl);

        bdd strat;
        unsigned tgt;
        [[maybe_unused]] bool at_least_one = false;
        for (const auto& avec : action_vecs) {
          SetOfStates fwd = singleton.apply ([&avec, &actioner] (const auto& max_elem) {
            auto&& ret = actioner.apply (max_elem, avec, actioners::direction::forward);
            verb_do (3, vout << " " << max_elem << " -> " << ret << std::endl);
            return ret;
          });
          assert (fwd.size () == 1);

          if (winning_region.contains (*fwd.begin ())) {
            strat = avec.output ();
            verb_do (2, vout << "dominated with IO = "
                             << spot::bdd_to_formula (input_letter & strat, aut->get_dict ())
                             << ": " << fwd);
            tgt = 0;
            for (const auto& elem_in_antichain : winning_region) {
              if (elem_in_antichain.partial_order (*fwd.begin ()).geq ())
                break;
              tgt++;
            }
            assert (tgt < winning_region.size ());
            mealy->new_edge (src, tgt, input_letter & strat);
            if (not visited[tgt])
              states_todo.push_back (tgt);
            at_least_one = true;
            break;
          }
        }
        assert (at_least_one);
      }
    }
    assert (spot::is_mealy (mealy));
    assert (spot::is_separated_mealy (mealy));

    // Use bisimulation with output assignment, and no split output.
    spot::simplify_mealy_here (mealy, 2, false);

    return mealy;
  }

  template <class SpecializedDownset>
  std::optional<spot::twa_graph_ptr> solve_with_downset (
      spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax, const VECTOR_ELT_T& kmin,
      const VECTOR_ELT_T& kinc, const bdd& all_inputs, const bdd& all_outputs, bool do_synthesis,
      [[maybe_unused]] const std::vector<symmetry::indexed_family_hint>& hints,
      acacia::game_backend backend) {
    acacia::config::checks::check_solver_components<SpecializedDownset> ();
#if ACACIA_ENABLE_EQUIVARIANT_SOLVER
    // Deliberately bind the equivariant pre-pass to backward: the measured
    // forward configuration excluded it, so this preserves both measured
    // configurations when both solvers are compiled in.
    if (backend == acacia::game_backend::backward and not do_synthesis) {
      auto eq = acacia::solver_detail::equivariant::try_solve<SpecializedDownset> (
          aut, kmax, kmin, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (),
          ACTIONER<typename SpecializedDownset::value_type> (), INPUT_PICKER (), hints);
      if (eq.attempted)
        return post_real<SpecializedDownset> (std::move (eq.win), do_synthesis, aut, all_inputs,
                                              all_outputs);
    }
#endif

    using IOsPrecomputationMaker = IOS_PRECOMPUTER;
    using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
    using InputPickerMaker = INPUT_PICKER;
#if ACACIA_FORWARD_SAFETY_SOLVER
    // post_real rebuilds actions and the input/output partition for synthesis.
    // The forward certificate has not yet been re-verified against that
    // reconstruction, so strategy production must keep using the backward
    // solver even when the forward decision backend is compiled in.
    if (backend == acacia::game_backend::forward and not do_synthesis) {
      auto forward =
          forward_k_bounded_safety_aut_detail<SpecializedDownset,
                                              IOsPrecomputationMaker,
                                              ActionerMaker, InputPickerMaker> (
              aut, kmin, kmax, kinc, all_inputs, all_outputs,
              IOS_PRECOMPUTER (),
              ACTIONER<typename SpecializedDownset::value_type> (),
              INPUT_PICKER ());
      auto win = forward.solve ();
      if (not forward.should_fallback_to_backward ())
        return post_real<SpecializedDownset> (
            std::move (win), do_synthesis, aut, all_inputs, all_outputs);
    }
#else
    // CLI requests are rejected while parsing.  Abort if an internal caller
    // bypasses that guard so a forward-labelled run can never use backward.
    if (backend == acacia::game_backend::forward)
      std::abort ();
#endif
    auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker,
                                           ActionerMaker, InputPickerMaker> (
        aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (),
        ACTIONER<typename SpecializedDownset::value_type> (), INPUT_PICKER ());
    return post_real<SpecializedDownset> (skn.solve (), do_synthesis, aut, all_inputs,
                                          all_outputs);
  }

}  // namespace acacia::solver_detail
