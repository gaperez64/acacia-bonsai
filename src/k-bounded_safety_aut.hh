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
#include "utils/vector_mm.hh"
#include <utils/verbose.hh>
#include "utils/typeinfo.hh"

#include "vectors.hh"

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

    struct solve_ret_t {
      bool solved;
      SetOfStates F; // the antichain

      solve_ret_t(bool s, SetOfStates&& F_): F(std::move(F_)) {
        solved = s;
      }
    };

    solve_ret_t solve (const std::string& synth_fname, SetOfStates* init_safe = nullptr) {
      int K = Kfrom;

      // Precompute the input and output actions.
      verb_do (1, vout << "IOS Precomputer..." << std::endl);
      auto inputs_to_ios = (ios_precomputer_maker.make (aut, input_support, output_support)) ();
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>, vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, K);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      auto input_output_fwd_actions = actioner.actions (); // list<pair<bdd, list<action_vec>>>
      verb_do (1, io_stats (input_output_fwd_actions));

      auto safe_vector = utils::vector_mm<char> (aut->num_states (), K - 1);

      for (size_t i = vectors::bool_threshold; i < aut->num_states (); ++i)
        safe_vector[i] = 0;
      SetOfStates F = SetOfStates (State (safe_vector));

      if (init_safe != nullptr) {
        // use a better starting point than the all-K vector
        F = std::move (*init_safe);
        utils::vout << "using F: " << F << "\n";
      }
      else utils::vout << "using default F: " << F << "\n";

      int loopcount = 0;

      utils::vector_mm<char> init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      init[aut->get_init_state_number ()] = 0;

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);

      do {
        loopcount++;
        verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);

        auto&& input = input_picker (F);
        if (not input.has_value ()) // No more inputs, and we just tested that init was present
        {
          if (!synth_fname.empty ()) {
            synthesis (F, actioner, synth_fname);
          }
          return { true, std::move(F) };
        }
        cpre_inplace (F, *input, actioner);
        if (not F.contains (State (init))) {
          if (K >= Kto)
            return { false, std::move(F) };
          verb_do (1, vout << "Incrementing K from " << K << " to " << K + Kinc << std::endl);
          K += Kinc;
          actioner.setK (K);
          verb_do (1, {vout << "Adding Kinc to every vector..."; vout.flush (); });
          F = F.apply ([&] (const State& s) {
            auto vec = utils::vector_mm<char> (s.size (), 0);
            for (size_t i = 0; i < vectors::bool_threshold; ++i)
              vec[i] = s[i] + Kinc;
            // Other entries are set to 0 by initialization, since they are bool.
            return State (vec);
          });
          verb_do (1, vout << "Done" << std::endl);
          continue;
        }

        verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);
      } while (1);

      std::abort ();
      return { false, std::move(F) };
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
    // UPre(F) = F \cap F2
    // F2 = \cap_{i \in I} F1i
    // F1i = \cup_{o \in O} PreHat (F, i, o)
    template <typename Action, typename Actioner>
    void cpre_inplace (SetOfStates& F, const Action& io_action, Actioner& actioner) {

      verb_do (2, vout << "Computing cpre(F) with F = " << std::endl << F);

      const auto& [input, actions] = io_action.get ();
      utils::vector_mm<char> v (aut->num_states (), -1);
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

      F.intersect_with (std::move (F1i));
      verb_do (2, vout << "F = " << std::endl << F);
    }


    // get index of the first dominating element that dominates the vector v
    // Container can be SetOfStates, or std::vector
    template <class Container>
    int get_dominated_index (const Container& saferegion, const State& v) const {
      int i = 0;
      auto it = saferegion.begin ();
      while (it != saferegion.end ()) {
        if (SetOfStates ((*it).copy ()).contains (v)) return i;
        i++;
        ++it;
      }
      return -1; // not found
    }

    template <class Container>
    State get_dominated_element (const Container& saferegion, const State& v) const {
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

    template <typename Actioner>
    void synthesis(SetOfStates& F, Actioner& actioner, const std::string& synth_fname) {
      verb_do (2, vout << "Final F:\n" << F);
      verb_do (1, vout << "F = downset of size " << F.size() << "\n");


      // Latches in the AIGER file are initialized to zero, so it would be nice if index 0 is the initial state
      // -> create new std::vector of dominating elements, start with only an initial one, and then add
      //    the reachable ones
      std::vector<State> states;

      // initial vector = all -1, and 0 for the initial state
      auto init_vector = utils::vector_mm<char> (aut->num_states (), -1);
      init_vector[aut->get_init_state_number ()] = 0;
      int init_index = get_dominated_index (F, State (init_vector));
      assert (init_index != -1);
      verb_do (1, vout << "Initial vector: " << State (init_vector) << " (index " << init_index << ")\n");
      states.push_back (get_dominated_element (F, State (init_vector)));
      verb_do (1, vout << "-> states = " << states << "\n\n");

      // explore and store transitions
      auto input_output_fwd_actions = actioner.actions ();


      std::vector<std::vector<transition>> transitions; // for every state: a vector of transitions (one per input)
      std::vector<unsigned int> states_todo = { 0 };

      while (!states_todo.empty ()) {
        // pop the last state
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
    }

    // return IO + destination state (one IO, one destination state: deterministic)
    template <typename Actions, typename Actioner>
    std::pair<bdd, State> get_transition (const State& elem, const Actions& actions, Actioner& actioner, const SetOfStates& saferegion) const {
      // action_vec maps each state q to a list of (p, is_q_accepting) tuples (vector<vector<tuple<unsigned int, bool>>>)
      for (const auto& action_vec : actions) {
        // calculate fwd(m, action), see if this is dominated by some element in the safe region
        SetOfStates&& fwd = SetOfStates (elem.copy ()).apply ([this, &action_vec, &actioner] (const auto& _m) {
          auto&& ret = actioner.apply (_m, action_vec, actioners::direction::forward);
          verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
          return std::move (ret);
        });

        assert (fwd.size () == 1);

        if (saferegion.contains (*fwd.begin ())) {
          verb_do (2, vout << "dominated with IO = " << bdd_to_formula (action_vec.IO) << ": " << fwd);
          return { action_vec.IO, (*fwd.begin ()).copy () }; // <- for deterministic policy using first IO that is found
        }
      }

      std::abort ();
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
