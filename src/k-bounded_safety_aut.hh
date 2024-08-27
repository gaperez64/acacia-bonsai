#pragma once

#undef MAX_CRITICAL_INPUTS
#define MAX_CRITICAL_INPUTS 1

#include <algorithm>
#include <map>
#include <functional>
#include <random>
#include <list>
#include <chrono>

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>

#include "utils/bdd_helper.hh"
#include "utils/lambda_ptr.hh"
#include "utils/ref_ptr_cmp.hh"
#include <utils/verbose.hh>
#include "utils/typeinfo.hh"

#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>

#include "ios_precomputers.hh"
#include "input_pickers.hh"
#include "actioners.hh"
#include "aiger.hh"

//#define debug(A...) do { std::cout << A << std::endl; } while (0)
#define debug(A...)
#define debug_(A...) do { std::cout << A << std::endl; } while (0)
//#define debug_(A...)
//#define ASSERT(A...) assert (A)
#define ASSERT(A...)
/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class SetOfStates,
          class IOsPrecomputationMaker,
          class ActionerMaker,
          class InputPickerMaker>
class k_bounded_safety_aut_detail {
    using State = typename SetOfStates::value_type;

  public:
    k_bounded_safety_aut_detail (spot::twa_graph_ptr aut, int Kfrom, int Kto, int Kinc,
                                 bdd input_support, bdd output_support,
                                 const IOsPrecomputationMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker) :
      aut {aut}, Kfrom {Kfrom}, Kto {Kto}, Kinc {Kinc},
      input_support {input_support}, output_support {output_support},
      gen {0},
      ios_precomputer_maker {ios_precomputer_maker},
      actioner_maker {actioner_maker},
      input_picker_maker {input_picker_maker}
    { }

    spot::formula bdd_to_formula (bdd f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    auto get_inputs_to_ios (bdd invariant) {
      // call the right constructor: with an extra variant if supported, otherwise the invariant is ignored
      // (it is assumed that other code will see that the invariant was not taken into account,
      //  such as in composition_mt.hh which calls finish_invariant only when needed)
      if constexpr (IOsPrecomputationMaker::supports_invariant) {
        return (ios_precomputer_maker.make (aut, input_support, output_support, invariant)) ();
      } else {
        return (ios_precomputer_maker.make (aut, input_support, output_support)) ();
      }
    }

    std::optional<SetOfStates> solve (SetOfStates& F, bdd invariant, std::vector<int> init_state) {
      int K = Kfrom;

      // Precompute the input and output actions.
      verb_do (1, vout << "IOS Precomputer with invariant " << bdd_to_formula (invariant) << "..." << std::endl);
      auto inputs_to_ios = get_inputs_to_ios (invariant);
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>, vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, K);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      auto input_output_fwd_actions = actioner.actions (); // list<pair<bdd, list<action_vec>>>
      verb_do (1, io_stats (input_output_fwd_actions));

      int loopcount = 0;

      posets::utils::vector_mm<VECTOR_ELT_T> init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      // either the initial state from the automaton, or some given initial
      // configuration
      if (init_state.size () == 0) {
        init[aut->get_init_state_number ()] = 0;
      } else {
        for (size_t i = 0; i < init_state.size (); i++)
          init[i] = init_state[i];
      }

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);

      do {
        loopcount++;
        verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);

        auto&& input = input_picker (F);
        if (not input.has_value ()) // No more inputs, and we just tested that init was present
        {
          //if (!synth.empty ()) synthesis (F, synth, actioner);
          return std::make_optional<SetOfStates> (std::move (F));
        }

        cpre_inplace (F, *input, actioner);

        if (not F.contains (State (init))) {
          if (K >= Kto)
            return std::nullopt;
          verb_do (1, vout << "Incrementing K from " << K << " to " << K + Kinc << std::endl);
          K += Kinc;
          actioner.setK (K);
          verb_do (1, {vout << "Adding Kinc to every vector..."; vout.flush (); });
          F = F.apply ([&] (const State& s) {
            auto vec = posets::utils::vector_mm<VECTOR_ELT_T> (s.size (), 0);
            for (size_t i = 0; i < posets::vectors::bool_threshold; ++i)
              vec[i] = s[i] + Kinc;
            // Other entries are set to 0 by initialization, since they are bool.
            return State (vec);
          });
          verb_do (1, vout << "Done" << std::endl);
          continue;
        }

        // verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);
      } while (1);

      std::abort ();
      return std::nullopt;
    }

    // Disallow copies.
    k_bounded_safety_aut_detail (k_bounded_safety_aut_detail&&) = delete;
    k_bounded_safety_aut_detail& operator= (k_bounded_safety_aut_detail&&) = delete;

  private:
    spot::twa_graph_ptr aut;
    const int Kfrom, Kto, Kinc;
    bdd input_support, output_support;
    std::mt19937 gen;
    const IOsPrecomputationMaker& ios_precomputer_maker;
    const ActionerMaker& actioner_maker;
    const InputPickerMaker& input_picker_maker;

    // This computes F = CPre(F), in the following way:
    // UPre(F) = F \cap F1i
    // F1i = \cup_{o \in O} F1io
    // F1io = PreHat (F, i, o)
    template <typename Action, typename Actioner>
    void cpre_inplace (SetOfStates& F, const Action& io_action, Actioner& actioner) {

      verb_do (2, vout << "Computing cpre(F) with F = " << std::endl << F);

      const auto& [input, actions] = io_action.get ();
#if CPRE_AVOID_UNIONS == 0
      posets::utils::vector_mm<VECTOR_ELT_T> v (aut->num_states (), -1);
      auto vv = typename SetOfStates::value_type (v);
      SetOfStates F1i (std::move (vv));
      bool first_turn = true;
      for (const auto& action_vec : actions) {
        verb_do (3, vout << "one_output_letter:" << std::endl);

        SetOfStates&& F1io = F.apply ([this, &action_vec, &actioner] (const auto& m) {
          auto&& ret = actioner.apply (m, action_vec, actioners::direction::backward);
          verb_do (3, vout << "  " << m << " -> " << ret << std::endl);
          return std::move (ret);
        });

        if (first_turn) {
          F1i = std::move (F1io);
          first_turn = false;
        }
        else
          F1i.union_with (std::move (F1io));
      }
#elif CPRE_AVOID_UNIONS == 1
      // Compute downset once, before intersection

      std::vector<typename SetOfStates::value_type> F1i_vec;
      F1i_vec.reserve (actions.size () * F.size ());
      for (const auto& action_vec : actions) {
        verb_do (3, vout << "one_output_letter:" << std::endl);

        for (const auto& m : F)
          F1i_vec.push_back (actioner.apply (m, action_vec, actioners::direction::backward));
      }

      SetOfStates F1i (std::move (F1i_vec));
#elif CPRE_AVOID_UNIONS == 2
# error Not implemented yet: Remove unions altogether and have intersect take a list
#endif

      F.intersect_with (std::move (F1i));
      verb_do (2, vout << "F = " << std::endl << F);
    }


    // get index of the first dominating element that dominates the vector v
    // Container can be SetOfStates, or std::vector
    template <class Container>
    int get_dominating_index (const Container& saferegion, const State& v) const {
      int i = 0;
      auto it = saferegion.begin ();
      while (it != saferegion.end ()) {
        // FIXME: Avoid copying, maybe by keeping a SetOfStates on the side
        // when using vector? Or using a specific SetOfStates like
        // vector-based
        // FIXME: Or just compare the two vectors!!!
        if (SetOfStates ((*it).copy ()).contains (v)) return i;
        i++;
        ++it;
      }
      return -1; // not found
    }

    template <class Container>
    State get_dominating_element (const Container& saferegion, const State& v) const {
      int i = 0;
      auto it = saferegion.begin ();
      while (it != saferegion.end ()) {
        if (SetOfStates ((*it).copy ()).contains (v)) return (*it).copy ();
        i++;
        ++it;
      }
      std::abort (); // element should be found, if we reach this -> bad
    }

    bdd binary_encode (unsigned int s, const std::vector<bdd>& src) const {
      // ~ bdd_buildcube(s, src.size(), src.data())
      // turn the value into a BDD e.g. with 4 states so 2 variables:
      // state 0: !x1 & !x2
      // state 1:  x1 & !x2
      // state 2: !x1 &  x2
      // state 3:  x1 &  x2

      bdd res = bddtrue;
      for (const bdd& var : src) {
        // use least significant bit for first variable, next bit for second variable, and so on
        bool negate = (s & 1) == 0;
        s >>= 1;
        res &= negate ? (!var) : var;
      }
      assert (s == 0);
      return res;
    }

    // could traverse the BDD instead but this is simpler, this function is only called on input/output support
    std::vector<bdd> cube_to_vector (const bdd& cube) {
      std::vector<bdd> res;
      for (int i = 0; i < bdd_varnum (); i++) {
        bdd var = bdd_ithvar (i);
        if (cube == (cube & var)) {
          res.push_back (var);
        }
      }
      return res;
    }

    struct transition {
      bdd IO;
      int new_state = -1;
    };

    struct badtransition {
      bdd IO;
      State new_state;
    };

  public:
    void winregion(SetOfStates& F, const std::string& winreg_fname, bdd invariant, std::vector<int> init_state) {
      auto inputs_to_ios = ios_precomputers::standard::make (aut, input_support, output_support, invariant) ();
      auto maker = actioners::standard<typename SetOfStates::value_type> ();
      // manually list the two template types so we can set the third (include IOs) to true
      auto actioner = maker.template make <decltype (aut), decltype (inputs_to_ios), true> (aut, inputs_to_ios, Kfrom);

      verb_do (2, vout << "Final F:\n" << F);
      verb_do (1, vout << "F = downset of size " << F.size() << "\n");

      // Latches in the AIGER file are initialized to zero, so it would be nice if index 0 is the initial state
      // -> create new std::vector of dominating elements, start with only an initial one, and then add
      //    the reachable ones
      std::vector<State> states;

      // initial vector = all -1, and 0 for the initial state
      auto init_vector = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), -1);
      // either the initial state from the automaton, or some given initial
      // configuration
      if (init_state.size () == 0) {
        init_vector[aut->get_init_state_number ()] = 0;
      } else {
        for (size_t i = 0; i < init_state.size (); i++)
          init_vector[i] = init_state[i];
      }
      int init_index = get_dominated_index (F, State (init_vector));
      assert (init_index != -1);
      verb_do (1, vout << "Initial vector: " << State (init_vector) << " (index " << init_index << ")\n");
      states.push_back (get_dominating_element (F, State (init_vector)));
      verb_do (1, vout << "-> states = " << states << "\n\n");

      // explore and store transitions
      auto input_output_fwd_actions = actioner.actions ();

      std::vector<std::vector<transition>> transitions; // for every state: a vector of safe transitions
      std::vector<std::vector<badtransition>> badtrans; // for every state: a vector of unsafe transitions
      std::vector<unsigned int> states_todo = { 0 };

      while (!states_todo.empty ()) {
        // pop the last state (depth-first search)
        unsigned int src = states_todo[states_todo.size () - 1];
        states_todo.pop_back ();

        verb_do (2, vout << "Element " << states[src] << "\n");

        // make sure transitions vector is large enough
        while (src >= transitions.size ()) {
          transitions.push_back ({});
          badtrans.push_back ({});
        }

        for (auto& tuple : input_output_fwd_actions) {
          // .first = input (BDD)
          // .second = list<action_vec>
          //  -> for this input, a list (one per compatible IO) of actions
          //  where an action maps each state q to a list of (p, is_q_accepting) tuples
          //  + the action includes the IO
          verb_do (2, vout << "Input: " << bdd_to_formula (tuple.first) << "\n");

          // add all compatible IOs that keep us in the safe region (+ encoding of destination state)
          bool found_one = false;

          // action_vec maps each state q to a list of (p, is_q_accepting) tuples (vector<vector<tuple<unsigned int, bool>>>)
          for (const auto& action_vec : tuple.second) {
            // calculate fwd(m, action), see if this is dominated by some element in the safe region
            SetOfStates&& fwd = SetOfStates (states[src].copy ()).apply ([this, &action_vec, &actioner] (const auto& _m) {
              auto&& ret = actioner.apply (_m, action_vec, actioners::direction::forward);
              verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
              return ret;
            });

            assert (fwd.size () == 1);
            State succ = (*fwd.begin ()).copy ();

            if (F.contains (succ)) {
              found_one = true;
              verb_do (2, vout << "dominated with IO = " << bdd_to_formula (action_vec.IO) << ": " << succ);

              auto it = std::find (states.begin (), states.end(), succ);
              int index = std::distance(states.begin (), it);

              if (it == states.end ()) {
                // we didn't know this state was reachable yet: it's not in states
                // -> add it, and add it to states_todo so we also check its successors
                index = states.size ();
                states.push_back (std::move (succ));
                states_todo.push_back (index);
              }
              transitions[src].push_back ({ action_vec.IO, index });
            } else {
              badtrans[src].push_back ({ action_vec.IO, std::move (succ) });
            }
          }

          if (not found_one) {
            utils::vout << "No transition found from " << states[src] << " with safe region " << F << "\n";
            assert (false);
          }
        }
        verb_do (2, vout << "\n");
      }
      verb_do (2, vout << "-> states = " << states << "\n");  

      // create APs to encode the mapping of the automaton states to integers
      // number of variables to encode the state
      unsigned int mapping_bits = ceil (log2 (states.size ()));
      assert (states.size () <= (1ull << mapping_bits));
      verb_do (3, vout << states.size () << " reachable states -> " << mapping_bits << " bit(s)\n\n");
      // extending the number of variables in Buddy by the required amount
      bdd_extvarnum (2 * mapping_bits);

      // create atomic propositions
      std::vector<bdd> state_vars, state_vars_prime;
      bdd state_vars_cube = bddtrue;
      bdd state_vars_prime_cube = bddtrue;
      for (unsigned int i = 0; i < mapping_bits; i++) {
        // Note the long and complex prefix of the variables we introduce:
        // we do not want them to clash with existing APs!
        unsigned int v = aut->register_ap ("_ab_enc_y" + std::to_string (i));
        verb_do (2, vout << "_ab_enc_y" << i << " = " << v << std::endl);
        state_vars.push_back (bdd_ithvar (v)); // store v instead of the bdd object itself?
        state_vars_cube &= bdd_ithvar (v);

        v = aut->register_ap ("_ab_enc_z" + std::to_string (i));
        verb_do (2, vout << "_ab_enc_z" << i << " = " << v << std::endl);
        state_vars_prime.push_back (bdd_ithvar (v));
        state_vars_prime_cube &= bdd_ithvar (v);
      }

      bdd encoding = bddfalse;
      bdd enc_states = bddfalse;
      bdd enc_primed_states = bddfalse;

      // Below, while we go, we also compute the encoding
      // of the successor states via bad transitions in terms of vectors (we
      // use little endian with the first bit being reserved for neg/nonneg)
      unsigned int vec_bits = ceil (log2 (Kto)) + 1;
      assert (Kto <= (1 << (vec_bits - 1)));
      verb_do (3, vout << Kto << " as max val -> " << vec_bits << " bit(s)\n\n");
      std::vector<bdd> vec_encoding (states[0].size () * vec_bits);
      std::fill (vec_encoding.begin (), vec_encoding.end (), bddfalse);

      // create BDD encoding using the states & transitions
      for (unsigned int i = 0; i < states.size (); i++) {
        bdd state_encoding = binary_encode (i, state_vars);
        bdd trans_encoding = bddfalse;
        // for every transition from state i
        for (const transition& ts : transitions[i]) {
          trans_encoding |= ts.IO & binary_encode (ts.new_state, state_vars_prime);
        }
        encoding |= state_encoding & trans_encoding;
        enc_states |= state_encoding;
        enc_primed_states |= binary_encode (i, state_vars_prime);
        // for every bad transition from state i
        for (const badtransition& ts : badtrans[i]) {
          for (size_t comp = 0; comp < states[0].size (); comp++) {
            if (ts.new_state[comp] == -1)
              vec_encoding[comp * vec_bits] |= state_encoding & ts.IO;
            else {
              int mask = 1;
              unsigned int cbit = vec_bits - 1;
              while (ts.new_state[comp] >= mask) {
                if (ts.new_state[comp] & mask)
                  vec_encoding[(comp * vec_bits) + cbit] |= state_encoding & ts.IO;
                mask = mask << 1;
                cbit -=1;
                assert (cbit >= 1);
                assert (mask < (1 << vec_bits));
              }
            }
          }
        }
      }
      bdd original_encoding = encoding;

      verb_do (2, vout << "Resulting BDD:\n" << bdd_to_formula (encoding) << "\n\n");

      // turn cube (single bdd) into vector<bdd>
      std::vector<bdd> input_vector = cube_to_vector (input_support & output_support);

      // AIGER
      aiger aig (input_vector, state_vars, states[0].size () * vec_bits, aut);

      // add output based on which transitions are safe (without successor)
      int ith_output = 0;
      aig.add_output(ith_output++, bdd_exist (encoding, state_vars_prime_cube));
      // add output for vector-based representation of successors
      for (size_t comp = 0; comp < states[0].size (); comp++)
        for (unsigned int cbit = 0; cbit < vec_bits; cbit++) {
          verb_do (2, vout << "Adding " << cbit + 1
                           << "-th bit of the " << comp + 1
                           << "-th component, so the "
                           << comp * vec_bits + cbit + 1
                           << "-th bit\n");
          verb_do (3, vout << "Corresponding BDD:\n"
                           << bdd_to_formula (vec_encoding[(comp * vec_bits) + cbit])
                           << "\n\n");
          aig.add_output(ith_output++, 
                         vec_encoding[(comp * vec_bits) + cbit]);

        }

      int i = 0;
      // new state as function(current_state, input, output)
      bdd fixdsucc = encoding;
      for (const bdd& m : state_vars_prime) {
        bdd pos = bdd_restrict (fixdsucc, m);
        pos = bdd_exist (pos, state_vars_prime_cube);
        bdd neg = bdd_restrict (fixdsucc, !m);
        neg = !bdd_exist (neg, state_vars_prime_cube);
        bdd f_l = (bdd_nodecount (pos) < bdd_nodecount (neg)) ? pos : neg;
        verb_do (2, vout << "f_" << bdd_to_formula (m) << ": " << bdd_to_formula (f_l) << "\n");
        aig.add_latch (i++, f_l);
        fixdsucc &= ((!f_l) | m) & (f_l | (!m));
        assert (fixdsucc != bddfalse);
      }
      encoding &= fixdsucc;

      verb_do (2, vout << "BDD after fixing latches:\n" << bdd_to_formula (encoding) << "\n\n");

      if (winreg_fname != "-") {
        std::ofstream f (winreg_fname);
        aig.output (f, false);
        f.close ();
      } else {
        utils::vout << "\n\n\n";
        aig.output (utils::vout, true);
      }

      verb_do (1, vout << "\n\n");
    }

    void synthesis(SetOfStates& F, const std::string& synth_fname, bdd invariant, std::vector<int> init_state) {
      auto inputs_to_ios = ios_precomputers::standard::make (aut, input_support, output_support, invariant) ();
      auto maker = actioners::standard<typename SetOfStates::value_type> ();
      // manually list the two template types so we can set the third (include IOs) to true
      auto actioner = maker.template make <decltype (aut), decltype (inputs_to_ios), true> (aut, inputs_to_ios, Kfrom);

      verb_do (2, vout << "Final F:\n" << F);
      verb_do (1, vout << "F = downset of size " << F.size() << "\n");

#ifndef NDEBUG
      bdd_printorder ();
      bdd_reorder_verbose (5);
      // specialized spot version of bdd_printstats ()
      bddStat s;
      bdd_stats (&s);
      std::cout << "a-bonsai: BDD stats: produced=" << s.produced
                << " nodenum=" << s.nodenum
                << " freenodes=" << s.freenodes
                << " (" << (s.freenodes * 100 / s.nodenum)
                << "%) minfreenodes=" << s.minfreenodes
                << "% varnum=" << s.varnum
                << " cachesize=" << s.cachesize
                << " hashsize=" << s.hashsize
                << " gbcnum=" << s.gbcnum
                << '\n';
#endif


      // Latches in the AIGER file are initialized to zero, so it would be nice if index 0 is the initial state
      // -> create new std::vector of dominating elements, start with only an initial one, and then add
      //    the reachable ones
      std::vector<State> states;

      // initial vector = all -1, and 0 for the initial state
      auto init_vector = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), -1);
      // either the initial state from the automaton, or some given initial
      // configuration
      if (init_state.size () == 0) {
        init_vector[aut->get_init_state_number ()] = 0;
      } else {
        for (size_t i = 0; i < init_state.size (); i++)
          init_vector[i] = init_state[i];
      }
      int init_index = get_dominated_index (F, State (init_vector));
      assert (init_index != -1);
      verb_do (1, vout << "Initial vector: " << State (init_vector) << " (index " << init_index << ")\n");
      states.push_back (get_dominating_element (F, State (init_vector)));
      verb_do (1, vout << "-> states = " << states << "\n\n");

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

          int index = get_dominating_index (states, p.second);
          // ^ returns index of FIRST element that dominates

          if (index == -1) {
            // we didn't know this state was reachable yet: it's not in states
            // -> add it, and add it to states_todo so we also check its successors
            index = states.size ();
            states.push_back (get_dominating_element (F, p.second));
            states_todo.push_back (index);
          }

          transitions[src].push_back ({ p.first, index });

          verb_do (2, vout << "\n");
        }

        verb_do (2, vout << "\n");
      }

      verb_do (2, vout << "-> states = " << states << "\n");

#ifndef NDEBUG
      // Print transitions
      for (unsigned int i = 0; i < states.size (); i++) {
        verb_do (3, vout << "State " << i << ":\n");
        for (const auto& t : transitions[i]) {
          verb_do (3, vout << bdd_to_formula (t.IO) << " -> state " << t.new_state << "\n");
        }
      }
      verb_do (3, vout << "\n");
#endif

      // create APs to encode the mapping of the automaton states to integers
      // number of variables to encode the state
      unsigned int mapping_bits = ceil (log2 (states.size ()));
      assert (states.size () <= (1ull << mapping_bits));
      verb_do (1, vout << states.size () << " reachable states -> " << mapping_bits << " bit(s)\n\n");

      // extending the number of variables in Buddy by the required amount
      bdd_extvarnum (2 * mapping_bits);

      // create atomic propositions
      std::vector<bdd> state_vars, state_vars_prime;
      bdd state_vars_cube = bddtrue;
      bdd state_vars_prime_cube = bddtrue;
      for (unsigned int i = 0; i < mapping_bits; i++) {
        // Note the long and complex prefix of the variables we introduce:
        // we do not want them to clash with existing APs!
        unsigned int v = aut->register_ap ("_ab_enc_y" + std::to_string (i));
        verb_do (2, vout << "_ab_enc_y" << i << " = " << v << std::endl);
        state_vars.push_back (bdd_ithvar (v)); // store v instead of the bdd object itself?
        state_vars_cube &= bdd_ithvar (v);

        v = aut->register_ap ("_ab_enc_z" + std::to_string (i));
        verb_do (2, vout << "_ab_enc_z" << i << " = " << v << std::endl);
        state_vars_prime.push_back (bdd_ithvar (v));
        state_vars_prime_cube &= bdd_ithvar (v);
      }

      bdd encoding = bddfalse;
      bdd enc_states = bddfalse;
      bdd enc_primed_states = bddfalse;

      // create BDD encoding using the states & transitions
      for (unsigned int i = 0; i < states.size (); i++) {
        bdd state_encoding = binary_encode (i, state_vars);
        bdd trans_encoding = bddfalse;
        // for every transition from state i
        for (const transition& ts : transitions[i]) {
          trans_encoding |= ts.IO & binary_encode (ts.new_state, state_vars_prime);
        }
        encoding |= state_encoding & trans_encoding;
        enc_states |= state_encoding;
        enc_primed_states |= binary_encode (i, state_vars_prime);
      }
      bdd original_encoding = encoding;

      verb_do (2, vout << "Resulting BDD:\n" << bdd_to_formula (encoding) << "\n\n");

#ifndef NDEBUG
      // we can now check that the encoded transition relation is inductive:
      // in words, for all transitions it is the case that the target is in
      // the set of reachable states (i.e. the set of all source states)
      bdd indcert = !encoding | enc_primed_states;
      indcert = !bdd_exist (!indcert, state_vars_cube);
      indcert = !bdd_exist (!indcert, input_support);
      indcert = !bdd_exist (!indcert, output_support);
      indcert = !bdd_exist (!indcert, state_vars_prime_cube);
      assert (indcert == bddtrue);

      // we can also check that for all encoded states, and all input
      // valuations, there exists an output transition
      bdd wincert = !enc_states | encoding;
      wincert = bdd_exist (wincert, state_vars_prime_cube);
      wincert = bdd_exist (wincert, output_support);
      wincert = !bdd_exist (!wincert, input_support);
      wincert = !bdd_exist (!wincert, state_vars_cube);
      assert (wincert == bddtrue);

      // we could also check that no state-input has multiple
      // output valuations that enable a transition
      bdd mulsol = bdd_exist (encoding, state_vars_prime_cube);
      mulsol = !bdd_exist (!mulsol, output_support);
      mulsol = bdd_exist (mulsol, input_support);
      mulsol = bdd_exist (mulsol, state_vars_cube);
      assert (mulsol == bddfalse);
#endif

      // turn cube (single bdd) into vector<bdd>
      std::vector<bdd> input_vector = cube_to_vector (input_support);
      std::vector<bdd> output_vector = cube_to_vector (output_support);

      // AIGER
      aiger aig (input_vector, state_vars, output_vector, aut);

      int i = 0;
      // for each output: function(current_state, input) that says whether this output is made true
      bdd wosucc = bdd_exist (encoding, state_vars_prime_cube);
      for (const bdd& o : output_vector) {
        bdd pos = bdd_restrict (wosucc, o);
        pos = bdd_exist (pos, output_support);
        bdd neg = bdd_restrict (wosucc, !o);
        neg = !bdd_exist (neg, output_support);
        bdd g_o = (bdd_nodecount (pos) < bdd_nodecount (neg)) ? pos : neg;
        verb_do (2, vout << "g_" << bdd_to_formula (o) << ": " << bdd_to_formula (g_o) << "\n");
        aig.add_output (i++, g_o);
        // as a last step, we need to update encoding to fix the function of
        // the output we have just chosen
        wosucc &= ((!g_o) | o) & (g_o | (!o));
        assert (wosucc != bddfalse);
      }
      encoding &= wosucc;

      verb_do (2, vout << "BDD after fixing outs:\n" << bdd_to_formula (encoding) << "\n\n");

#ifndef NDEBUG
      // here, we can check whether we refined the encoding instead of adding
      // new things - which would be erroneous; and then some
      // sanity checks
      assert (encoding == (encoding & original_encoding));

      indcert = !encoding | enc_primed_states;
      indcert = !bdd_exist (!indcert, state_vars_cube);
      indcert = !bdd_exist (!indcert, input_support);
      indcert = !bdd_exist (!indcert, output_support);
      indcert = !bdd_exist (!indcert, state_vars_prime_cube);
      assert (indcert == bddtrue);

      mulsol = bdd_exist (encoding, state_vars_prime_cube);
      mulsol = !bdd_exist (!mulsol, output_support);
      mulsol = bdd_exist (mulsol, input_support);
      mulsol = bdd_exist (mulsol, state_vars_cube);
      assert (mulsol == bddfalse);

      wincert = !enc_states | encoding;
      wincert = bdd_exist (wincert, state_vars_prime_cube);
      wincert = bdd_exist (wincert, output_support);
      wincert = !bdd_exist (!wincert, input_support);
      wincert = !bdd_exist (!wincert, state_vars_cube);
      assert (wincert == bddtrue);

      bdd_stats (&s);
      std::cout << "a-bonsai: BDD stats: produced=" << s.produced
                << " nodenum=" << s.nodenum
                << " freenodes=" << s.freenodes
                << " (" << (s.freenodes * 100 / s.nodenum)
                << "%) minfreenodes=" << s.minfreenodes
                << "% varnum=" << s.varnum
                << " cachesize=" << s.cachesize
                << " hashsize=" << s.hashsize
                << " gbcnum=" << s.gbcnum
                << '\n';
#endif

      i = 0;
      // new state as function(current_state, input)
      bdd outless = bdd_exist (encoding, output_support);
      for (const bdd& m : state_vars_prime) {
        bdd pos = bdd_restrict (outless, m);
        pos = bdd_exist (pos, state_vars_prime_cube);
        bdd neg = bdd_restrict (outless, !m);
        neg = !bdd_exist (neg, state_vars_prime_cube);
        bdd f_l = (bdd_nodecount (pos) < bdd_nodecount (neg)) ? pos : neg;
        verb_do (2, vout << "f_" << bdd_to_formula (m) << ": " << bdd_to_formula (f_l) << "\n");
        aig.add_latch (i++, f_l);
        outless &= ((!f_l) | m) & (f_l | (!m));
        assert (outless != bddfalse);
      }
      encoding &= outless;

      verb_do (2, vout << "BDD after fixing latches:\n" << bdd_to_formula (encoding) << "\n\n");

#ifndef NDEBUG
      // same checks here again
      assert (encoding == (encoding & original_encoding));

      indcert = !encoding | enc_primed_states;
      indcert = !bdd_exist (!indcert, state_vars_cube);
      indcert = !bdd_exist (!indcert, input_support);
      indcert = !bdd_exist (!indcert, output_support);
      indcert = !bdd_exist (!indcert, state_vars_prime_cube);
      assert (indcert == bddtrue);

      mulsol = bdd_exist (encoding, state_vars_prime_cube);
      mulsol = !bdd_exist (!mulsol, output_support);
      mulsol = bdd_exist (mulsol, input_support);
      mulsol = bdd_exist (mulsol, state_vars_cube);
      assert (mulsol == bddfalse);

      wincert = !enc_states | encoding;
      wincert = bdd_exist (wincert, state_vars_prime_cube);
      wincert = bdd_exist (wincert, output_support);
      wincert = !bdd_exist (!wincert, input_support);
      wincert = !bdd_exist (!wincert, state_vars_cube);
      assert (wincert == bddtrue);

      bdd_stats (&s);
      std::cout << "a-bonsai: BDD stats: produced=" << s.produced
                << " nodenum=" << s.nodenum
                << " freenodes=" << s.freenodes
                << " (" << (s.freenodes * 100 / s.nodenum)
                << "%) minfreenodes=" << s.minfreenodes
                << "% varnum=" << s.varnum
                << " cachesize=" << s.cachesize
                << " hashsize=" << s.hashsize
                << " gbcnum=" << s.gbcnum
                << '\n';
#endif


      if (synth_fname != "-") {
        std::ofstream f (synth_fname);
        aig.output (f, false);
        f.close ();
      } else {
        utils::vout << "\n\n\n";
        aig.output (utils::vout, true);
      }

      verb_do (1, vout << "\n\n");
    }

  private:

    // return IO + destination state (one IO, one destination state: deterministic)
    template <typename Actions, typename Actioner>
    std::pair<bdd, State> get_transition (const State& elem, const Actions& actions,
                                          Actioner& actioner, const SetOfStates& saferegion) const {
      // action_vec maps each state q to a list of (p, is_q_accepting) tuples (vector<vector<tuple<unsigned int, bool>>>)
      for (const auto& action_vec : actions) {
        // calculate fwd(m, action), see if this is dominated by some element in the safe region
        SetOfStates&& fwd = SetOfStates (elem.copy ()).apply ([this, &action_vec, &actioner] (const auto& _m) {
          auto&& ret = actioner.apply (_m, action_vec, actioners::direction::forward);
          verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
          return ret;
        });

        assert (fwd.size () == 1);

        if (saferegion.contains (*fwd.begin ())) {
          verb_do (2, vout << "dominated with IO = " << bdd_to_formula (action_vec.IO) << ": " << fwd);
          return { action_vec.IO, (*fwd.begin ()).copy () }; // <- for deterministic policy using first IO that is found
        }
      }

      utils::vout << "No transition found from " << elem << " with safe region " << saferegion << "\n";
      assert (false);
      return { bddfalse, elem.copy () };
    }


    ////////////////////////////////////////////////


    template <typename IToActions>
    void io_stats (const IToActions& inputs_to_actions) {
      size_t all_io = 0;
      for (const auto& [inputs, ios] : inputs_to_actions) {
        verb_do (1, vout << "INPUT: " << bdd_to_formula (inputs)
                 /*   */ <<  " #ACTIONS: " << ios.size () << std::endl);
        all_io += ios.size ();
      }
      auto ins = input_support;
      size_t all_inputs_size = 1;
      while (ins != bddtrue) {
        all_inputs_size *= 2;
        ins = bdd_high (ins);
      }

      auto outs = output_support;
      size_t all_outputs_size = 1;
      while (outs != bddtrue) {
        all_outputs_size *= 2;
        outs = bdd_high (outs);
      }

      utils::vout << "INPUT GAIN: " << inputs_to_actions.size () << "/" << all_inputs_size
                  << " = " << (inputs_to_actions.size () * 100 / all_inputs_size) << "%\n"
                  << "IO GAIN: " << all_io << "/" << all_inputs_size * all_outputs_size
                  << " = " << (all_io * 100 / (all_inputs_size * all_outputs_size)) << "%"
                  << std::endl;
    }
};

template <class SetOfStates,
          class IOsPrecomputationMaker,
          class ActionerMaker,
          class InputPickerMaker>
static auto k_bounded_safety_aut_maker (const spot::twa_graph_ptr& aut, int Kfrom, int Kto, int Kinc,
                                        bdd input_support, bdd output_support,
                                        const IOsPrecomputationMaker& ios_precomputer_maker,
                                        const ActionerMaker& actioner_maker,
                                        const InputPickerMaker& input_picker_maker) {
  return k_bounded_safety_aut_detail<SetOfStates, IOsPrecomputationMaker, ActionerMaker, InputPickerMaker>
    (aut, Kfrom, Kto, Kinc, input_support, output_support, ios_precomputer_maker, actioner_maker, input_picker_maker);
}

template <class SetOfStates>
static auto k_bounded_safety_aut (const spot::twa_graph_ptr& aut, int Kfrom, int Kto, int Kinc,
                                  bdd input_support, bdd output_support) {
  return k_bounded_safety_aut_maker<SetOfStates> (aut, Kfrom, Kto, Kinc,
                                                  input_support, output_support,
                                                  IOS_PRECOMPUTER (),
                                                  ACTIONER (),
                                                  INPUT_PICKER ()
    );
}
