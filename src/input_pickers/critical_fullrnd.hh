#pragma once

#include <random>
#include <optional>
#include "actioners.hh"

namespace input_pickers {
  namespace detail {
    template <typename FwdActions, typename Actioner>
    struct critical_fullrnd {
      public:
        critical_fullrnd (FwdActions& fwd_actions, Actioner& actioner) :
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
          std::vector<input_and_actions_ref> V (fwd_actions.begin (),
                                                fwd_actions.end ());

          std::shuffle (V.begin (), V.end (), gen);

          std::list<input_and_actions_ref> Cbar (V.begin (), V.end ());

          auto critical_input = Cbar.end ();

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
                critical_input = it;
                break;
              }

              TODO ("Try putting a weight, rather than pushing at the front.");
              if (it_act != actions.begin ())
                actions.splice (actions.begin(), actions, it_act);
          }
            if (critical_input != Cbar.end ())
              break;
          }

          if (critical_input == Cbar.end ()) {
            verb_do (3, vout << "No critical input." << std::endl);
            return std::optional<input_and_actions_ref> ();
          }

          verb_do (2, {
              vout << "Critical input: ";
              const auto& [input, actions] = critical_input->get ();
              vout << "[" << input << "] " << std::endl;
            });

          return std::make_optional (*critical_input);
        }
      private:
        using input_and_actions_ref = std::reference_wrapper<typename FwdActions::value_type>;
        FwdActions& fwd_actions;
        Actioner& actioner;
        std::mt19937 gen;
   };
  }

  struct critical_fullrnd {
      template <typename FwdActions, typename Actioner>
      static auto make (FwdActions& fwd_actions, Actioner& actioner) {
        return detail::critical_fullrnd (fwd_actions, actioner);
      }
  };
}
