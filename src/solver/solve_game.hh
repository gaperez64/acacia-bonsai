#pragma once

#include "actioners.hh"
#include "boolean_states.hh"
#include "configuration.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "k_bounded_safety_aut.hh"
#include "posets/downsets.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors.hh"
#include "posets/vectors/traits.hh"
#include "solve_game.hh"
#include "utils/static_switch.hh"

#include <bddx.h>
#include <spot/twa/twa.hh>
#include <utility>

#define UNREACHABLE [] ([[maybe_unused]] int x) { std::unreachable (); }

/**
 * Function to treat the winning region of a game, if any. The winning region
 * having a value means that it contains the initial state.
 */
template <class SetOfStates>
bool post_real (std::optional<std::pair<VECTOR_ELT_T, SetOfStates>>&& win_res,
                const std::optional<std::string>& synth_fname, spot::twa_graph_ptr aut,
                const bdd& all_inputs, const bdd& all_outputs) {
  using state = typename SetOfStates::value_type;

  if (not win_res.has_value () or not synth_fname.has_value ())
    return win_res.has_value ();

  const auto& [k, winning_region] = *win_res;

  // What follows is mostly the synthesis procedure as ncharl intended
  auto actioner_factory = actioners::no_ios_precomputation<state> ();
  auto inputs_to_ios = ios_precomputers::delegate::make (aut, all_inputs, all_outputs) ();
  auto actioner = actioner_factory.make (aut, inputs_to_ios, k);

  verb_do (2, vout << "Winning region = downset of size " << winning_region.size () << std::endl);
  verb_do (2, vout << "Found using k = " << k << std::endl);

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
  size_t init_idx = 0;
  bool found = false;
  for (const auto& elem_in_antichain : winning_region) {
    state_space.push_back (&elem_in_antichain);
    if (elem_in_antichain.partial_order (init_state).leq ())
      found = true;
    if (not found)
      init_idx++;
  }
  assert (found and winning_region.size () == state_space.size ());
  verb_do (
      2, vout << "Index of max element dominating the initial state is " << init_idx << std::endl);

#if 0

      // explore and store transitions
      auto input_output_fwd_actions = actioner.actions ();


      std::vector<std::vector<transition>> transitions; // for every state: a vector of transitions (one per input)
      std::vector<unsigned int> states_todo = { 0 };

      while (!states_todo.empty ()) {
        // pop the last state (depth-first search)
        unsigned int src = states_todo[states_todo.size () - 1];
        states_todo.pop_back ();

        verb_do (2, vout << "Element " << states[src] << "\n");

        // make sure transitions vector is large enough
        while (src >= transitions.size ()) {
          transitions.push_back ({});
        }

        for (auto& tuple : input_output_fwd_actions) {
          // .first = input (BDD)
          // .second = list<action_vec>
          //  -> for this input, a list (one per compatible IO) of actions
          //  where an action maps each state q to a list of (p, is_q_accepting) tuples
          //  + the action includes the IO
          verb_do (2, vout << "Input: " << bdd_to_formula (tuple.first) << "\n");

          // add all compatible IOs that keep us in the safe region (+ encoding of destination state)
          std::pair<bdd, State> p = get_transition (states[src], tuple.second, actioner, F);
          // note: it may be that an IO is returned that keeps us in the safe region but requires adding a new element (index == -1)
          // it could be that there does exist an IO that doesn't make us add a new maximal element, so we could add a new argument
          // to get_transition to pass the current states, which would then be checked first - may make a slightly smaller circuit,
          // at the cost of taking longer (as we no longer stop at the first IO)

          int index = get_dominated_index (states, p.second);
          // ^ returns index of FIRST element that dominates

          if (index == -1) {
            // we didn't know this state was reachable yet: it's not in states
            // -> add it, and add it to states_todo so we also check its successors
            index = states.size ();
            states.push_back (get_dominated_element (F, p.second));
            states_todo.push_back (index);
          }

          transitions[src].push_back ({ p.first, index });

          verb_do (2, vout << "\n");
        }

        verb_do (2, vout << "\n");
      }



      verb_do (2, vout << "-> states = " << states << "\n");

      // Print transitions
      for (unsigned int i = 0; i < states.size (); i++) {
        verb_do (2, vout << "State " << i << ":\n");
        for (const auto& t : transitions[i]) {
          verb_do (2, vout << bdd_to_formula (t.IO) << " -> state " << t.new_state << "\n");
        }
      }

      verb_do (2, vout << "\n");

      // create APs to encode the mapping of the automaton states to integers
      // number of variables to encode the state
      unsigned int mapping_bits = ceil (log2 (states.size ()));
      assert (states.size () <= (1ull << mapping_bits));
      verb_do (1, vout << states.size () << " reachable states -> " << mapping_bits << " bit(s)\n\n");



      // create atomic propositions
      std::vector<bdd> state_vars, state_vars_prime;
      bdd state_vars_prime_cube = bddtrue;
      for (unsigned int i = 0; i < mapping_bits; i++) {
        unsigned int v = aut->register_ap (spot::formula::ap ("Y" + std::to_string (i)));
        state_vars.push_back (bdd_ithvar (v)); // store v instead of the bdd object itself?

        v = aut->register_ap (spot::formula::ap ("Z" + std::to_string (i)));
        state_vars_prime.push_back (bdd_ithvar (v));
        state_vars_prime_cube &= bdd_ithvar (v);
      }


      bdd encoding = bddfalse;

      // create BDD encoding using the states & transitions
      for (unsigned int i = 0; i < states.size (); i++) {
        bdd state_encoding = binary_encode (i, state_vars);
        bdd trans_encoding = bddfalse;
        // for every transition from state i
        for (const transition& ts : transitions[i]) {
          trans_encoding |= ts.IO & binary_encode (ts.new_state, state_vars_prime);
        }
        encoding |= state_encoding & trans_encoding;
      }

      verb_do (2, vout << "Resulting BDD:\n" << bdd_to_formula (encoding) << "\n\n");

      // turn cube (single bdd) into vector<bdd>
      std::vector<bdd> input_vector = cube_to_vector (input_support);
      std::vector<bdd> output_vector = cube_to_vector (output_support);


      // AIGER
      aiger aig (input_vector, state_vars, output_vector, aut);


      int i = 0;
      // for each output: function(current_state, input) that says whether this output is made true
      for (const bdd& o : output_vector) {
        bdd pos = bdd_exist (encoding & o, output_support & state_vars_prime_cube);
        bdd neg = !bdd_exist (encoding & (!o), output_support & state_vars_prime_cube);
        bdd g_o = (bdd_nodecount (pos) < bdd_nodecount (neg)) ? pos : neg;
        verb_do (2, vout << "g_" << bdd_to_formula (o) << ": " << bdd_to_formula (g_o) << "\n");
        aig.add_output (i++, g_o);
      }

      i = 0;
      // new state as function(current_state, input)
      for (const bdd& m : state_vars_prime) {
        bdd pos = bdd_exist (encoding & m, output_support & state_vars_prime_cube);
        bdd neg = !bdd_exist (encoding & (!m), output_support & state_vars_prime_cube);
        bdd f_l = (bdd_nodecount (pos) < bdd_nodecount (neg)) ? pos : neg;
        verb_do (2, vout << "f_" << bdd_to_formula (m) << ": " << bdd_to_formula (f_l) << "\n");
        aig.add_latch (i++, f_l);
      }


      if (synth_fname != "-") {
        std::ofstream f (synth_fname);
        aig.output (f, false);
        f.close ();
      } else {
        utils::vout << "\n\n\n";
        aig.output (utils::vout, true);
      }

      verb_do (1, vout << "\n\n");
#endif

  return true;
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
bool solve_game (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax, const VECTOR_ELT_T& kmin,
                 const VECTOR_ELT_T& kinc, const bdd& all_inputs, const bdd& all_outputs,
                 std::optional<std::string> synth_fname) {
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

  bool realizable = false;

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
                realizable = post_real<SpecializedDownset> (skn.solve (), synth_fname, aut,
                                                            all_inputs, all_outputs);
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
          realizable = post_real<SpecializedDownset> (skn.solve (), synth_fname, aut, all_inputs,
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
    realizable =
        post_real<SpecializedDownset> (skn.solve (), synth_fname, aut, all_inputs, all_outputs);
#endif
  }

  return realizable;
}
