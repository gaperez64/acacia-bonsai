#pragma once

#include "actioners.hh"
#include "actioners/no_ios_precomputation.hh"
#include "boolean_states.hh"
#include "configuration.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "ios_precomputers/delegate.hh"
#include "k_bounded_safety_aut.hh"
#include "posets/downsets.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors.hh"
#include "posets/vectors/traits.hh"
#include "utils/static_switch.hh"
#include <spot/twaalgos/mealy_machine.hh>

#include <bddx.h>
#include <fstream>
#include <spot/twa/acc.hh>
#include <spot/twa/twa.hh>
#include <utility>

#define UNREACHABLE [] ([[maybe_unused]] int x) { std::unreachable (); }

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

  // We got here, so there is a winning region and we need to do synthesis
  const auto& [k, winning_region] = *win_res;

  // What follows is mostly the synthesis procedure as ncharl intended, but
  // rewritten by gaperez64 using spot instead of AIGER.
  //
  // Note: here we use a specific combination of ios_precomputer and actioner,
  // these are NOT necessarily the one dictated by the macros
  auto actioner_factory = actioners::no_ios_precomputation<state> ();
  auto inputs_to_ios = ios_precomputers::delegate::make (aut, all_inputs, all_outputs) ();
  auto actioner = actioner_factory.make (aut, inputs_to_ios, k);
  auto io_fwd_actions = actioner.actions ();

  verb_do (3, vout << "Winning region = downset of size " << winning_region.size () << std::endl);
  verb_do (3, vout << "Found using k = " << (int) k << std::endl);

  // We will use the maxima from the winning region downset as the state of
  // the Mealy machine representation of the controller
  std::vector<const state*> state_space;
  state_space.reserve (winning_region.size ());

  // First, let's find the index of the element that dominates the initial
  // state from the universal co-Buchi automaton. Since we're looping over the
  // antichain, we take the opportunity to populate the state_space.
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
  verb_do (
      2, vout << "Index of max element dominating the initial state is " << init_idx << std::endl);

  // We can now start creating the Mealy machine that represents the strategy.
  // (Note that we will have some nondeterminism, it should be fine. Spot uses
  // automata to store Mealy machines, minimize them, determinize, and then
  // spit out small AIGER circuits for them.)
  spot::twa_graph_ptr mealy = make_twa_graph (aut->get_dict ());
  mealy->copy_ap_of (aut);
  mealy->set_acceptance (spot::acc_cond::acc_code::t ());
  bdd* output_cube = new bdd ();
  *output_cube = all_outputs;
  mealy->set_named_prop<bdd> ("synthesis-outputs", output_cube);
  mealy->new_states (winning_region.size ());
  mealy->set_init_state (init_idx);

  // To populate the Mealy machine, we will now explore the maxima in a DFS
  // fashion from the initial state/maximum.
  std::vector<unsigned> states_todo = {init_idx};
  std::vector<bool> visited (state_space.size (), false);
  while (not states_todo.empty ()) {
    unsigned src = states_todo.back ();
    states_todo.pop_back ();
    if (visited[src])  // since states_todo is a vector, not a set...
      continue;
    visited[src] = true;
    SetOfStates singleton (state_space[src]->copy ());

    // go through transitions, if we reach an unexplored state, push in stack
    for (auto& [input_letter, action_vecs] : io_fwd_actions) {
      // input_letter of type input (BDD)
      // action_vecs of type list<action_vec>
      //
      // Essentially: for this input letter, a list (one per IO compatible
      // with it) of action vectors, i.e. a vector of similarly labelled
      // transitions along with the output letter that enables them
      verb_do (2, vout << "Input: " << spot::bdd_to_formula (input_letter, aut->get_dict ())
                       << std::endl);

      // look for compatible IOs that keep us in the safe region
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
                           << spot::bdd_to_formula (input_letter & strat, aut->get_dict ()) << ": "
                           << fwd);
          // get index of first element in winning region that dominates sucessor
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
          break;  // deterministic strategy: one output per (state, input)
        }
      }
      assert (at_least_one);
    }
  }
  assert (is_mealy (mealy));
  assert (is_separated_mealy (mealy));  // transition conditions of form
                                        // (in) & (out)

  // use bisimulation with out assignment, and no split output
  spot::simplify_mealy_here (mealy, 2, false);

  return mealy;
}

/**
 * Complicated construction to make sure that we solve the game while making
 * good use of the downsets library. In a nutshell, we check how many boolean
 * states we have to fit their counters into an array of bitsets or a vector of bools
 * while keeping the rest of the counters in a proper downset. Since array
 * sizes have to be fixed during compile time, we need to prepare a few sizes
 * in advance here and otherwise default to other means...
 *
 * Two macros can be used to control the switching:
 * - NO_ARRAY_CAP_MAX
 * - USE_BOOLVEC_OVER_BITSET
 *
 * (see also utils/static_switch.hh)
 */
std::optional<spot::twa_graph_ptr> solve_game (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                                               const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                                               const bdd& all_inputs, const bdd& all_outputs,
                                               bool do_synthesis) {
  if (all_outputs == bddtrue)  // no output APs; system has no control
    verb_do (2, vout << "Warning: synthesis without output APs\n");

  // Compute how many boolean states will actually be put in bitsets.
  constexpr auto max_bools_in_bitsets = posets::vectors::nbitsets_to_nbools (STATIC_MAX_BITSETS);
  auto nbitsetbools = aut->num_states () - posets::vectors::bool_threshold;
  if (nbitsetbools > max_bools_in_bitsets) {
    verb_do (1, vout << "Warning: bitsets not large enough, using regular vectors for some "
                        "Boolean states.\n"
                     /*   */
                     << "\tTotal # of Boolean-for-bitset states: "
                     << nbitsetbools
                     /*   */
                     << ", max: " << max_bools_in_bitsets << std::endl);
    nbitsetbools = max_bools_in_bitsets;
  }

#ifdef NO_ARRAY_CAP_MAX
# pragma message("STATIC_ARRAY_CAP_MAX is being set to 0!")
  constexpr auto STATIC_ARRAY_CAP_MAX = 0;
#else
  constexpr auto STATIC_ARRAY_CAP_MAX =
      posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
          STATIC_ARRAY_MAX);
#endif

  // Maximize usage of the nonbool implementation
  auto nonbools = aut->num_states () - nbitsetbools;
  size_t actual_nonbools =
      (nonbools <= STATIC_ARRAY_CAP_MAX)
          ? posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (
                nonbools)
          : posets::vectors::traits<posets::vectors::VECTOR_IMPL, VECTOR_ELT_T>::capacity_for (
                nonbools);
  if (actual_nonbools >= aut->num_states ())
    nbitsetbools = 0;
  else
    nbitsetbools -= (actual_nonbools - nonbools);

  posets::vectors::bitset_threshold = aut->num_states () - nbitsetbools;

  verb_do (1, vout << "Bitset threshold set at " << posets::vectors::bitset_threshold << "\n");

  std::optional<spot::twa_graph_ptr> res = std::nullopt;

  if (actual_nonbools <= STATIC_ARRAY_CAP_MAX) {  // Array & Bitsets
    static_switch_t<STATIC_ARRAY_CAP_MAX> {}(
        [&] (auto vnonbools) {
          static_switch_t<STATIC_MAX_BITSETS> {}(
              [&] (auto vbitsets) {
                using SpecializedDownset =
                    posets::downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                        posets::vectors::ARRAY_IMPL<VECTOR_ELT_T, std::max (vnonbools.value, 1UL)>,
                        vbitsets.value>>;
                using IOsPrecomputationMaker = IOS_PRECOMPUTER;
                using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
                using InputPickerMaker = INPUT_PICKER;
                auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker,
                                                       ActionerMaker, InputPickerMaker> (
                    aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (),
                    ACTIONER<typename SpecializedDownset::value_type> (), INPUT_PICKER ());
                res = post_real<SpecializedDownset> (skn.solve (), do_synthesis, aut, all_inputs,
                                                     all_outputs);
              },
              UNREACHABLE, posets::vectors::nbools_to_nbitsets (nbitsetbools));
        },
        UNREACHABLE, actual_nonbools);
  }
  else {  // Vectors & Bitsets
#ifndef USE_BOOLVEC_OVER_BITSET
    static_switch_t<STATIC_MAX_BITSETS> {}(
        [&] (auto vbitsets) {
          using SpecializedDownset =
              posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
                  posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>, vbitsets.value>>;

          using IOsPrecomputationMaker = IOS_PRECOMPUTER;
          using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
          using InputPickerMaker = INPUT_PICKER;
          auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker,
                                                 ActionerMaker, InputPickerMaker> (
              aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (),
              ACTIONER<typename SpecializedDownset::value_type> (), INPUT_PICKER ());
          res = post_real<SpecializedDownset> (skn.solve (), do_synthesis, aut, all_inputs,
                                               all_outputs);
        },
        UNREACHABLE, posets::vectors::nbools_to_nbitsets (nbitsetbools));
#else
    using SpecializedDownset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<
        posets::vectors::x_and_boolvec<posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>>>;

    using IOsPrecomputationMaker = IOS_PRECOMPUTER;
    using ActionerMaker = ACTIONER<typename SpecializedDownset::value_type>;
    using InputPickerMaker = INPUT_PICKER;
    auto skn = k_bounded_safety_aut_detail<SpecializedDownset, IOsPrecomputationMaker,
                                           ActionerMaker, InputPickerMaker> (
        aut, kmin, kmax, kinc, all_inputs, all_outputs, IOS_PRECOMPUTER (),
        ACTIONER<typename SpecializedDownset::value_type> (), INPUT_PICKER ());
    res = post_real<SpecializedDownset> (skn.solve (), do_synthesis, aut, all_inputs, all_outputs);
#endif
  }

  return res;
}
