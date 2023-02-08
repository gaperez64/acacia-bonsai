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

    bool solve () {
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
            if (true) // TODO command line argument
            {
                synthesis(F, actioner);
            }
            return true;
        }
        cpre_inplace (F, *input, actioner);
        if (not F.contains (State (init))) {
          if (K >= Kto)
            return false;
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
      return false;
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
          // action_vec : vector<vector<pair<unsigned int, bool>>>
          // -> map each state q to a list of (p, is_q_accepting) tuples
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

    template<typename Antichain, typename Actioner>
    void synthesis(Antichain& F, Actioner& actioner)
    {
        utils::vout << "Final F:\n" << F;
        utils::vout << "= antichain of size " << F.size() << "\n\n";

        auto input_output_fwd_actions = actioner.actions();

        int k = 1;
        // for every dominating element m
        for(auto& m: F)
        {
            // for every input i
            utils::vout << "Elem " << k++ << "\n";
            int j = 1;
            for(auto& tuple: input_output_fwd_actions)
            {
                // .first = input (BDD)
                // .second = list<action_vec>
                //  -> for this input, a list (one per compatible IO) of actions
                //  where an action maps each state q to a list of (p, is_q_accepting) tuples
                //  + the action includes the IO
                //  these actions are used in act_cpre
                utils::vout << "Input " << j++ << ": " << bdd_to_formula(tuple.first) << "\n";
                act_cpre(SetOfStates(m.copy()), tuple.second, actioner, F); // return set of IOs
                utils::vout << "\n";
            }
            utils::vout << "\n\n";
        }
    }

    template <typename Actions, typename Actioner>
    void act_cpre(const SetOfStates& m, const Actions& actions, Actioner& actioner, const SetOfStates& antichain)
    {
        assert(m.size() == 1); // m is a single state in a set

        bool dominated = false;
        utils::vout << "m = " << m;
        // action_vec maps each state q to a list of (p, is_q_accepting) tuples (vector<vector<tuple<unsigned int, bool>>>)
        for(const auto& action_vec: actions)
        {
            // calculate bwd(m, action), see if this is dominated by some element in the antichain
            SetOfStates&& bwd = m.apply ([this, &action_vec, &actioner] (const auto& _m) {
                auto&& ret = actioner.apply (_m, action_vec, actioners::direction::backward);
                verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
                return std::move (ret);
            });

            assert(bwd.size() == 1);

            utils::vout << "IO = " << bdd_to_formula(action_vec.IO) << ": -> " << bwd;
            // antichain = type downsets::vector_backed_bin<vectors::X_and_bitset<vectors::simd_array_backed_sum_<char, 1ul>, 0ul> >
            if (antichain.contains(*bwd.begin()))
            {
                dominated = true;
                utils::vout << "-> dominated\n";
                //utils::vout << "dominated with IO = " << bdd_to_formula(action_vec.IO) << "\n";
            }
        }
        if (!dominated)
        {
            // not good, shouldn´t happen
            utils::vout << "-> no action exists\n";
        }
        else
        {
            utils::vout << "-> action exists\n";
        }
    }


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
