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
    typedef typename SetOfStates::value_type State;

  public:
    k_bounded_safety_aut_detail (spot::twa_graph_ptr aut, int K,
                                 bdd input_support, bdd output_support,
                                 int verbose,
                                 const IOsPrecomputationMaker& ios_precomputer_maker,
                                 const ActionerMaker& actioner_maker,
                                 const InputPickerMaker& input_picker_maker) :
      aut {aut}, K {K},
      input_support {input_support}, output_support {output_support}, verbose {verbose},
      gen {0},
      ios_precomputer_maker {ios_precomputer_maker},
      actioner_maker {actioner_maker},
      input_picker_maker {input_picker_maker}
    { }

    spot::formula bdd_to_formula (bdd f) const {
      return spot::bdd_to_formula (f, aut->get_dict ());
    }

    bool solve () {
      std::cout << "SOLVE" << std::endl;
      debug_ ("#STATES " << aut->num_states ());

      // Precompute the input and output actions.
      auto inputs_to_ios = (ios_precomputer_maker.make (aut, input_support, output_support, verbose)) ();
      auto actioner = actioner_maker.make (aut, inputs_to_ios, K, verbose);
      auto input_output_fwd_actions = actioner.actions ();
      if (verbose)
        io_stats (input_output_fwd_actions);

      auto safe_vector = std::vector<char> (aut->num_states (), K - 1);

      for (size_t i = 0; i < vectors::bitset_threshold; ++i)
        safe_vector[i] = 0;

      SetOfStates F = SetOfStates (State (safe_vector));

      int loopcount = 0;

      auto input_picker = input_picker_maker.make (input_output_fwd_actions, actioner, verbose);

      do {
        loopcount++;
        if (verbose)
          std::cout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl;

        auto&& [input, valid] = input_picker (F);
        if (not valid) { // No valid input found.
          std::vector<char> init (aut->num_states ());
          init.assign (aut->num_states (), -1);
          init[aut->get_init_state_number ()] = 0;
          return F.contains (State (init));
        }
        cpre_inplace (F, input, actioner);

        if (verbose)
          std::cout << "Loop# " << loopcount << ", F of size " << F.size () << std::endl;
      } while (1);

      std::abort ();
      return false;
    }

    // Disallow copies.
    k_bounded_safety_aut_detail (k_bounded_safety_aut_detail&&) = delete;
    k_bounded_safety_aut_detail& operator= (k_bounded_safety_aut_detail&&) = delete;

  private:
    spot::twa_graph_ptr aut;
    int K;
    bdd input_support, output_support;
    const int verbose;
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

      if (verbose > 1)
        std::cout << "Computing cpre(F) with F = " << std::endl
                  << F;

      const auto& [input, actions] = io_action.get ();
      std::vector<char> v (aut->num_states (), -1);
      auto vv = typename SetOfStates::value_type (v);
      SetOfStates F1i (std::move (vv));
      bool first_turn = true;
      for (const auto& action_vec : actions) {
        if (verbose > 2)
          std::cout << "one_output_letter:" << std::endl;

        SetOfStates&& F1io = F.apply ([this, &action_vec, &actioner] (const auto& m) {
          auto&& ret = actioner.apply (m, action_vec, actioners::direction::backward);
          if (verbose > 2)
            std::cout << "  " << m << " -> " << ret << std::endl;
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
      if (verbose > 1)
        std::cout << "F = " << std::endl << F;
    }

    template <typename IToActions>
    void io_stats (const IToActions& inputs_to_actions) {
      size_t all_io = 0;
      for (const auto& [inputs, ios] : inputs_to_actions) {
        if (verbose)
          std::cout << "INPUT: " << bdd_to_formula (inputs)
                    <<  " #ACTIONS: " << ios.size () << std::endl;
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

      std::cout << "INPUT GAIN: " << inputs_to_actions.size () << "/" << all_inputs_size
                << " = " << (inputs_to_actions.size () * 100 / all_inputs_size) << "%\n"
                << "IO GAIN: " << all_io << "/" << all_inputs_size * all_outputs_size
                << " = " << (all_io * 100 / (all_inputs_size * all_outputs_size)) << "%"
                << std::endl;
    }

    /*
     std::map<const action_vec*, action_vec> rev;

     State trans_ (const State& m, const action_vec& vec, trans::direction dir) {
     if (dir == trans::direction::forward)
     return trans_orig (m, vec, dir);
     auto it = rev.find (&vec);
     if (it != rev.end ())
     return trans_orig (m, it->second, dir);
     action_vec v (aut->num_states ());

     for (size_t p = 0; p < aut->num_states (); ++p)
     for (const auto& [q, q_final] : vec[p])
     v[q].push_back (std::pair (p, q_final));
     return trans_orig (m,
     rev.emplace (&vec, std::move (v)).first->second,
     dir);
     }


     State trans_orig (const State& m, const action_vec& action_vec, trans::direction dir) {
     State f (aut->num_states ());
     using trans::direction;

     for (size_t p = 0; p < aut->num_states (); ++p) {
     if (dir == direction::forward)
     f[p] = -1;
     else
     f[p] = K - 1;

     for (const auto& [q, q_final] : action_vec[p]) {
     if (dir == direction::forward) {
     if (m[q] != -1)
     f[p] = (int) std::max ((int) f[p], std::min (K, (int) m[q] + (q_final ? 1 : 0)));
     } else
     f[p] = (int) std::min ((int) f[p], std::max (-1, (int) m[q] - (q_final ? 1 : 0)));

     // If we reached the extreme values, stop going through states.
     if ((dir == direction::forward  && f[p] == K) ||
     (dir == direction::backward && f[p] == -1))
     break;
     }
     }
     return f;
     }*/

};

template <class SetOfStates,
          class IOsPrecomputationMaker,
          class ActionerMaker,
          class InputPickerMaker>
static auto k_bounded_safety_aut_maker (const spot::twa_graph_ptr& aut, int K,
                                        bdd input_support, bdd output_support,
                                        int verbose,
                                        const IOsPrecomputationMaker& ios_precomputer_maker,
                                        const ActionerMaker& actioner_maker,
                                        const InputPickerMaker& input_picker_maker) {
  return k_bounded_safety_aut_detail<SetOfStates, IOsPrecomputationMaker, ActionerMaker, InputPickerMaker>
    (aut, K, input_support, output_support, verbose, ios_precomputer_maker, actioner_maker, input_picker_maker);
}

template <class State, // TODO To be removed, deduced from SetOfStates,
          class SetOfStates>
static auto k_bounded_safety_aut (const spot::twa_graph_ptr& aut, int K,
                                  bdd input_support, bdd output_support,
                                  int verbose) {
  return k_bounded_safety_aut_maker<SetOfStates> (aut, K, input_support, output_support, verbose,
                                                  ios_precomputers::fake_vars (),
                                                  actioners::standard<State> (),
                                                  input_pickers::critical ()
    );
}
