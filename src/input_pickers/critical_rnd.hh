#pragma once

#include <random>
#include "actioners.hh"

namespace input_pickers {
  namespace detail {
    template <typename FwdActions, typename Actioner>
    struct critical_rnd {
      public:
        critical_rnd (FwdActions& fwd_actions, Actioner& actioner) :
          fwd_actions {fwd_actions}, actioner {actioner}, gen {0} {}

        template <typename SetOfStates>
        auto operator() (const SetOfStates& F) {
          // Def: f is one-step-losing if there is an input i such that for all output o
          //           succ (f, <i,o>) \not\in F
          //      the input i is the *witness* of one-step-loss.
          // Def: A set C of inputs is critical for F if:
          //        \exists f \in F, i \in C, i witnesses one-step-loss of F.
          // Algo: We go through all f in F, find an input i witnessing one-step-loss, add it to C.

          // Sort/randomize input_output_fwd_actions
          using input_and_actions_ref = std::reference_wrapper<typename FwdActions::value_type>;
          std::vector<input_and_actions_ref> V (fwd_actions.begin (),
                                                fwd_actions.end ());

          auto N = std::min (V.size (), 5ul);
          std::shuffle (V.begin (), V.begin () + N, gen);
          std::shuffle (V.begin () + N / 2, V.end (), gen);

          std::list<input_and_actions_ref> Cbar (V.begin (), V.end ());

          auto critical_input = input_and_actions_ref (* (typename input_and_actions_ref::type*) NULL);
          bool found_input = false;

          for (const auto& f : F) {
            bool is_witness = false;
            verb_do (3, vout << "Searching for witness of one-step-loss for " << f << std::endl);

            for (auto it = Cbar.begin (); it != Cbar.end (); ++it) {
              auto& [input, actions] = it->get ();
              is_witness = true;
              auto it_act = actions.begin ();
              for (; it_act != actions.end (); ++it_act) {
                auto fwdf = actioner.apply (f, *it_act, actioners::direction::forward);
                verb_do (3, vout << "apply(" << f << ", <" << input << ", ?>) = " << fwdf << ": ");
                if (F.contains (fwdf)) {
                  verb_do (3, vout << " is in F." << std::endl);
                  is_witness = false;
                  break;
                }
                verb_do (3, vout << " is not in F." << std::endl);
              }

              if (is_witness) {
                // inputs witness one-step-loss of f
                verb_do (3, vout << "Input " << input
                         /*   */ << " witnesses one-step-loss of " << f << std::endl);
                critical_input = *it;
                found_input = true;
                break;
              }

              TODO ("Try putting a weight, rather than pushing at the front.");
              if (it_act != actions.begin ())
                actions.splice (actions.begin(), actions, it_act);
          }
            if (found_input)
              break;
          }

          if (not found_input) {
            verb_do (3, vout << "No critical input." << std::endl);
            return std::pair (critical_input, false);
          }

          verb_do (2, {
              vout << "Critical input: ";
              const auto& [input, actions] = critical_input.get ();
              vout << "[" << input << "] " << std::endl;
            });

          return std::pair (critical_input, true);
        }
      private:
        FwdActions& fwd_actions;
        Actioner& actioner;
        const int verbose;
        std::mt19937 gen;
   };
  }

  struct critical_rnd {
      template <typename FwdActions, typename Actioner>
      static auto make (FwdActions& fwd_actions, Actioner& actioner, int verbose) {
        return detail::critical_rnd (fwd_actions, actioner, verbose);
      }
  };
}
