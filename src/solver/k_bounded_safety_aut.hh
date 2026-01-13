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

/// \brief Wrapper class around a UcB to pass as the deterministic safety
/// automaton S^K_N, for N a given UcB.
template <class SetOfStates, class IOsPrecomputationMaker, class ActionerMaker,
          class InputPickerMaker>
class k_bounded_safety_aut_detail {
    using state = typename SetOfStates::value_type;

  public:
    k_bounded_safety_aut_detail (spot::twa_graph_ptr aut, VECTOR_ELT_T kfrom, VECTOR_ELT_T kto,
                                 VECTOR_ELT_T kinc, bdd input_support, bdd output_support,
                                 const IOsPrecomputationMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker)
      : aut {aut},
        kfrom {kfrom},
        kto {kto},
        kinc {kinc},
        input_support {input_support},
        output_support {output_support},
        gen {0},
        ios_precomputer_maker {ios_precomputer_maker},
        actioner_maker {actioner_maker},
        input_picker_maker {input_picker_maker} {}

    [[nodiscard]] spot::formula bdd_to_formula (const bdd& f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    auto get_inputs_to_ios () {
      return (ios_precomputer_maker.make (aut, input_support, output_support)) ();
    }

    std::optional<SetOfStates> solve () {
      VECTOR_ELT_T k = kfrom;

      // Precompute the input and output actions.
      auto inputs_to_ios = get_inputs_to_ios ();
      // ^ ios_precomputers::detail::standard_container<shared_ptr<spot::twa_graph>,
      // vector<pair<int, int>>>
      verb_do (1, vout << "Make actions..." << std::endl);
      auto actioner = actioner_maker.make (aut, inputs_to_ios, k);
      verb_do (1, vout << "Fetching IO actions" << std::endl);
      auto input_output_fwd_actions = actioner.actions ();  // list<pair<bdd, list<action_vec>>>
      verb_do (1, io_stats (input_output_fwd_actions));

      // What is the initial state?
      posets::utils::vector_mm<VECTOR_ELT_T> init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      init[aut->get_init_state_number ()] = 0;

      // What are the safe states?
      auto safe_vector = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), k - 1);
      for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
        safe_vector[i] = 0;
      SetOfStates f = SetOfStates (state (safe_vector));

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner);
      int loopcount = 0;

      do {
        loopcount++;
        verb_do (1, vout << "Loop# " << loopcount << ", f of size " << f.size () << std::endl);

        auto&& input = input_picker (f);
        if (not input.has_value ())  // No more inputs, and we just tested that init was present
        {
          // if (!synth.empty ()) synthesis (f, synth, actioner);
          return std::make_optional<SetOfStates> (std::move (f));
        }

        cpre_inplace (f, *input, actioner);

        if (not f.contains (state (init))) {
          if (k >= kto) {
            verb_do (2, vout << "Early exit because the initial state is out\n");
            return std::nullopt;
          }
          verb_do (1, vout << "Incrementing k from " << (int)k << " to "
                           << (int)(k + kinc) << std::endl);
          k += kinc;
          actioner.setK (k);
          verb_do (1, {
            vout << "Adding kinc to every vector...";
            vout.flush ();
          });
          f = f.apply ([&] (const state& s) {
            auto vec = posets::utils::vector_mm<VECTOR_ELT_T> (s.size (), 0);
            for (size_t i = 0; i < posets::vectors::bool_threshold; ++i)
              vec[i] = s[i] + kinc;
            // Other entries are set to 0 by initialization, since they are bool.
            return state (vec);
          });
          verb_do (1, vout << "Done" << std::endl);
          continue;
        }

        // verb_do (1, vout << "Loop# " << loopcount << ", f of size " << f.size () << std::endl);
      } while (true);

      verb_do (2, vout << "Aborting!\n");
      std::abort ();
      return std::nullopt;
    }

    // Disallow copies.
    k_bounded_safety_aut_detail (k_bounded_safety_aut_detail&&) = delete;
    k_bounded_safety_aut_detail& operator= (k_bounded_safety_aut_detail&&) = delete;

  private:
    spot::twa_graph_ptr aut;
    const VECTOR_ELT_T kfrom, kto, kinc;
    bdd input_support, output_support;
    std::mt19937 gen { };
    const IOsPrecomputationMaker& ios_precomputer_maker;
    const ActionerMaker& actioner_maker;
    const InputPickerMaker& input_picker_maker;

    // This computes f = CPre(f), in the following way:
    // UPre(f) = f \cap f1i
    // f1i = \cup_{o \in O} f1io
    // f1io = PreHat (f, i, o)
    template <typename Action, typename Actioner>
    void cpre_inplace (SetOfStates& f, const Action& io_action, Actioner& actioner) {
      verb_do (2, vout << "Computing cpre(f) with f = " << std::endl << f);

      const auto& [input, actions] = io_action.get ();
#if CPRE_AVOID_UNIONS == 0
      posets::utils::vector_mm<VECTOR_ELT_T> v (aut->num_states (), -1);
      auto vv = typename SetOfStates::value_type (v);
      SetOfStates f1i (std::move (vv));
      bool first_turn = true;
      for (const auto& action_vec : actions) {
        verb_do (3, vout << "one_output_letter:" << std::endl);

        SetOfStates&& f1io = f.apply ([this, &action_vec, &actioner] (const auto& m) {
          auto&& ret = actioner.apply (m, action_vec, actioners::direction::backward);
          verb_do (3, vout << "  " << m << " -> " << ret << std::endl);
          return std::move (ret);
        });

        if (first_turn) {
          f1i = std::move (f1io);
          first_turn = false;
        }
        else
          f1i.union_with (std::move (f1io));
      }
#elif CPRE_AVOID_UNIONS == 1
      // Compute downset once, before intersection

      std::vector<typename SetOfStates::value_type> f1i_vec;
      f1i_vec.reserve (actions.size () * f.size ());
      for (const auto& action_vec : actions) {
        verb_do (3, vout << "one_output_letter:" << std::endl);

        for (const auto& m : f)
          f1i_vec.push_back (actioner.apply (m, action_vec, actioners::direction::backward));
      }

      SetOfStates f1i (std::move (f1i_vec));
#elif CPRE_AVOID_UNIONS == 2
# error Not implemented yet: Remove unions altogether and have intersect take a list
#endif

      f.intersect_with (std::move (f1i));
      // Experimentally, this is not faster:
      //   f1i.intersect_with (std::move (f));
      //   f = std::move (f1i);
      verb_do (2, vout << "f = " << std::endl << f);
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
