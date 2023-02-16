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
                synthesis(F, actioner, K);
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



    bdd binary_encode(unsigned int s, const std::vector<bdd>& src)
    {
        // turn the value into a BDD e.g. with 4 states so 2 variables:
        // state 0: !x1 & !x2
        // state 1:  x1 & !x2
        // state 2: !x1 &  x2
        // state 3:  x1 &  x2

        bdd res = bddtrue;
        for(const bdd& var: src)
        {
            // use least significant bit for first variable, next bit for second variable, and so on
            bool negate = (s & 1) == 0;
            s >>= 1;
            res &= negate ? (!var) : var;
        }
        assert(s == 0);
        return res;
    }

    bdd encode_full_state(const SetOfStates& m, const std::vector<std::vector<bdd>>& state_aps)
    {
        // combine binary_encode for each automaton state's value into one BDD
        assert(m.size() == 1); // m is a single state in a set
        bdd res = bddtrue;

        auto& state = *m.begin();

        for(unsigned int i = 0; i < state.size(); i++)
        {
            int value = state[i]; // should be between -1 and k
            assert(value >= -1);
            res &= binary_encode((unsigned int)(value+1), state_aps[i]);
        }

        return res;
    }

    template<typename Antichain, typename Actioner>
    void synthesis(Antichain& F, Actioner& actioner, int K)
    {
        utils::vout << "Final F:\n" << F;
        utils::vout << "= antichain of size " << F.size() << "\n\n";

        // create APs to encode the mapping of the automaton states to integers
        // number of variables to encode one entry
        unsigned int mapping_bits = ceil(log2(K + 2));
        assert((K + 2) <= (1 << mapping_bits));
        utils::vout << "K = " << K << " -> " << mapping_bits << " bits\n";
        utils::vout << "Number of automaton states = " << aut->num_states() << "\n";


        // create atomic propositions
        std::vector<std::vector<bdd>> state_aps(aut->num_states());
        for(unsigned int s = 0; s < aut->num_states(); s++)
        {
            for(unsigned int i = 0; i < mapping_bits; i++)
            {
                unsigned int v = aut->register_ap(spot::formula::ap("S"+std::to_string(s)+"_"+std::to_string(i)));
                state_aps[s].push_back(bdd_ithvar(v)); // store v instead of the bdd object itself?
            }
        }

        bdd encoding = bddfalse;

        auto input_output_fwd_actions = actioner.actions();

        int k = 1;
        // for every dominating element m
        for(auto& m: F)
        {
            utils::vout << "Elem " << k++ << "\n";
            int j = 1;
            SetOfStates m_in_set = SetOfStates(m.copy());
            bdd states_encoding = encode_full_state(m_in_set, state_aps);
            bdd IO_encoding = bddfalse;

            // for every input i
            for(auto& tuple: input_output_fwd_actions)
            {
                // .first = input (BDD)
                // .second = list<action_vec>
                //  -> for this input, a list (one per compatible IO) of actions
                //  where an action maps each state q to a list of (p, is_q_accepting) tuples
                //  + the action includes the IO
                //  these actions are used in act_cpre
                utils::vout << "Input " << j++ << ": " << bdd_to_formula(tuple.first) << "\n";

                // add all IOs that are good and are compatible with this input
                IO_encoding |= act_cpre(m_in_set, tuple.second, actioner, F);
                utils::vout << "\n";
            }
            // add all the IOs that are good in this state
            encoding |= states_encoding & IO_encoding;
            utils::vout << "\n\n";
        }

        utils::vout << "Resulting BDD:\n" << bdd_to_formula(encoding) << "\n";
    }

    template <typename Actions, typename Actioner>
    bdd act_cpre(const SetOfStates& m, const Actions& actions, Actioner& actioner, const SetOfStates& antichain)
    {
        assert(m.size() == 1); // m is a single state in a set

        bool dominated = false;
        utils::vout << "m = " << m;
        bdd good_IOs = bddfalse;

        // action_vec maps each state q to a list of (p, is_q_accepting) tuples (vector<vector<tuple<unsigned int, bool>>>)
        for(const auto& action_vec: actions)
        {
            // calculate fwd(m, action), see if this is dominated by some element in the antichain
            SetOfStates&& fwd = m.apply ([this, &action_vec, &actioner] (const auto& _m) {
                auto&& ret = actioner.apply (_m, action_vec, actioners::direction::forward);
                verb_do (3, vout << "  " << _m << " -> " << ret << std::endl);
                return std::move (ret);
            });

            assert(fwd.size() == 1);

            //utils::vout << "IO = " << bdd_to_formula(action_vec.IO) << ": -> " << fwd;
            // antichain = type downsets::vector_backed_bin<vectors::X_and_bitset<vectors::simd_array_backed_sum_<char, 1ul>, 0ul>>
            if (antichain.contains(*fwd.begin()))
            {
                dominated = true;
                //utils::vout << "-> dominated\n";
                utils::vout << "dominated with IO = " << bdd_to_formula(action_vec.IO) << ": " << fwd;
                good_IOs |= action_vec.IO;
            }
        }
        if (!dominated)
        {
            // not good, shouldn´t happen
            utils::vout << "-> NO action exists\n";
        }
        else
        {
            utils::vout << "-> action exists\n";
        }

        return good_IOs;
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
