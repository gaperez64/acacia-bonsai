#pragma once

#undef MAX_CRITICAL_INPUTS
#define MAX_CRITICAL_INPUTS 1

#include "actioners.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "utils/bdd_helper.hh"
#include "utils/lambda_ptr.hh"
#include "utils/ref_ptr_cmp.hh"
#include "utils/typeinfo.hh"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <random>
#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>
#include <utils/verbose.hh>

#include <posets/utils/vector_mm.hh>
#include <posets/vectors.hh>

// #define debug(A...) do { std::cout << A << std::endl; } while (0)
#define debug(A...)
#define debug_(A...)             \
  do {                           \
    std::cout << A << std::endl; \
  } while (0)
// #define debug_(A...)
// #define ASSERT(A...) assert (A)
#define ASSERT(A...)
/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class SetOfStates, class IOsPrecomputationMaker, class ActionerMaker,
          class InputPickerMaker>
class k_bounded_safety_aut_detail {
    using State = typename SetOfStates::value_type;

  public:
    k_bounded_safety_aut_detail (spot::twa_graph_ptr aut, int Kfrom, int Kto, int Kinc,
                                 bdd input_support, bdd output_support,
                                 const IOsPrecomputationMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker)
      : aut {aut},
        Kfrom {Kfrom},
        Kto {Kto},
        Kinc {Kinc},
        input_support {input_support},
        output_support {output_support},
        gen {0},
        ios_precomputer_maker {ios_precomputer_maker},
        actioner_maker {actioner_maker},
        input_picker_maker {input_picker_maker} {}

    spot::formula bdd_to_formula (bdd f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    auto get_inputs_to_ios () {
      return (ios_precomputer_maker.make (aut, input_support, output_support)) ();
    }

    std::optional<SetOfStates> solve () {
      int K = Kfrom;

      // Precompute the input and output actions.
      auto inputs_to_ios = get_inputs_to_ios ();
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>,
      // vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, K);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      auto input_output_fwd_actions = actioner.actions ();  // list<pair<bdd, list<action_vec>>>
      verb_do (1, io_stats (input_output_fwd_actions));

      // What is the initial state?
      posets::utils::vector_mm<VECTOR_ELT_T> init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      init[aut->get_init_state_number ()] = 0;

      // What are the safe states?
      auto safe_vector = posets::utils::vector_mm<char> (aut->num_states (), K - 1);
      for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
        safe_vector[i] = 0;
      SetOfStates F = SetOfStates (State (safe_vector));

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);
      int loopcount = 0;

      do {
        loopcount++;
        verb_do (1, vout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl);

        auto&& input = input_picker (F);
        if (not input.has_value ())  // No more inputs, and we just tested that init was present
        {
          // if (!synth.empty ()) synthesis (F, synth, actioner);
          return std::make_optional<SetOfStates> (std::move (F));
        }

        cpre_inplace (F, *input, actioner);

        if (not F.contains (State (init))) {
          if (K >= Kto) {
            verb_do (2, vout << "Early exit because the initial state is out\n");
            return std::nullopt;
          }
          verb_do (1, vout << "Incrementing K from " << K << " to " << K + Kinc << std::endl);
          K += Kinc;
          actioner.setK (K);
          verb_do (1, {
            vout << "Adding Kinc to every vector...";
            vout.flush ();
          });
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

      verb_do (2, vout << "Aborting!\n");
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
      // Experimentally, this is not faster:
      //   F1i.intersect_with (std::move (F));
      //   F = std::move (F1i);
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
        if (SetOfStates ((*it).copy ()).contains (v))
          return i;
        i++;
        ++it;
      }
      return -1;  // not found
    }

    template <class Container>
    State get_dominating_element (const Container& saferegion, const State& v) const {
      int i = 0;
      auto it = saferegion.begin ();
      while (it != saferegion.end ()) {
        if (SetOfStates ((*it).copy ()).contains (v))
          return (*it).copy ();
        i++;
        ++it;
      }
      std::abort ();  // element should be found, if we reach this -> bad
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

    // could traverse the BDD instead but this is simpler, this function is only called on
    // input/output support
    std::vector<bdd> cube_to_vector (const bdd& cube) {
      std::vector<bdd> res;
      for (int i = 0; i < bdd_varnum (); i++) {
        bdd var = bdd_ithvar (i);
        if (cube == (cube & var))
          res.push_back (var);
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

    // return IO + destination state (one IO, one destination state: deterministic)
    template <typename Actions, typename Actioner>
    std::pair<bdd, State> get_transition (const State& elem, const Actions& actions,
                                          Actioner& actioner,
                                          const SetOfStates& saferegion) const {
      // action_vec maps each state q to a list of (p, is_q_accepting) tuples
      // (vector<vector<tuple<unsigned int, bool>>>)
      for (const auto& action_vec : actions) {
        // calculate fwd(m, action), see if this is dominated by some element in the safe region
        SetOfStates&& fwd =
            SetOfStates (elem.copy ()).apply ([this, &action_vec, &actioner] (const auto& _m) {
              auto&& ret = actioner.apply (_m, action_vec, actioners::direction::forward);
              verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
              return ret;
            });

        assert (fwd.size () == 1);

        if (saferegion.contains (*fwd.begin ())) {
          verb_do (
              2, vout << "dominated with IO = " << bdd_to_formula (action_vec.IO) << ": " << fwd);
          return {action_vec.IO,
                  (*fwd.begin ())
                      .copy ()};  // <- for deterministic policy using first IO that is found
        }
      }

      utils::vout << "No transition found from " << elem << " with safe region " << saferegion
                  << "\n";
      assert (false);
      return {bddfalse, elem.copy ()};
    }

    ////////////////////////////////////////////////

    template <typename IToActions>
    void io_stats (const IToActions& inputs_to_actions) {
      size_t all_io = 0;
      for (const auto& [inputs, ios] : inputs_to_actions) {
        verb_do (1, vout << "INPUT: "
                         << bdd_to_formula (inputs)
                         /*   */
                         << " #ACTIONS: " << ios.size () << std::endl);
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

      utils::vout << "INPUT GAIN: " << inputs_to_actions.size () << "/" << all_inputs_size << " = "
                  << (inputs_to_actions.size () * 100 / all_inputs_size) << "%\n"
                  << "IO GAIN: " << all_io << "/" << all_inputs_size * all_outputs_size << " = "
                  << (all_io * 100 / (all_inputs_size * all_outputs_size)) << "%" << std::endl;
    }
};


