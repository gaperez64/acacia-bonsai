#pragma once

#include <random>
#include <optional>
#include "actioners.hh"

namespace input_pickers {
  namespace detail {
    template <typename FwdActions, typename Actioner>
    struct critical_pq {
      public:
        critical_pq (FwdActions& fwd_actions, Actioner& actioner) :
          actioner {actioner}, gen {0} {
          int priority = 0;
          for (auto& el : fwd_actions)
            fwd_actions_pq.emplace (priority++, std::ref (el));
        }

        template <typename SetOfStates>
        auto operator() (const SetOfStates& F) {
          // Def: f is one-step-losing if there is an input i such that for all output o
          //           succ (f, <i,o>) \not\in F
          //      the input i is the *witness* of one-step-loss.
          // Def: A set C of inputs is critical for F if:
          //        \exists f \in F, i \in C, i witnesses one-step-loss of F.
          // Algo: We go through all f in F, find an input i witnessing one-step-loss, add it to C.

          auto critical_input = fwd_actions_pq.end ();

          for (const auto& f : F) {
            bool is_witness = false;
            verb_do (3, vout << "Searching for witness of one-step-loss for " << f << std::endl);

            for (auto it = fwd_actions_pq.begin (); it != fwd_actions_pq.end (); ++it) {
              auto& [input, actions] = it->second.get ();
              is_witness = true;
              auto it_act = actions.begin ();
              for (/* */; it_act != actions.end (); ++it_act) {
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

              TODO ("Try also putting a weight, rather than pushing at the front.");
              if (it_act != actions.begin ())
                actions.splice (actions.begin(), actions, it_act);
            }
            if (critical_input != fwd_actions_pq.end ())
              break;
          }

          if (critical_input == fwd_actions_pq.end ()) {
            verb_do (3, vout << "No critical input." << std::endl);
            return std::optional<input_and_actions_ref> ();
          }

          // Update the hit count of that critical input.
          {
            auto [priority, ref] = *critical_input;
            fwd_actions_pq.erase (critical_input);
            fwd_actions_pq.emplace (priority - 1, ref);
          }

          verb_do (2, {
              vout << "Critical input: ";
              const auto& [input, actions] = critical_input->second.get ();
              vout << "[" << input << "] " << std::endl;
            });

          return std::make_optional (critical_input->second);
        }
      private:
        using input_and_actions_ref = std::reference_wrapper<typename FwdActions::value_type>;
        using fwd_actions_pq_t = std::multimap<int, input_and_actions_ref>; // needs to be signed
        fwd_actions_pq_t fwd_actions_pq;
        Actioner& actioner;
        std::mt19937 gen;
   };
  }

  struct critical_pq {
      template <typename FwdActions, typename Actioner>
      static auto make (FwdActions& fwd_actions, Actioner& actioner) {
        return detail::critical_pq (fwd_actions, actioner);
      }
  };
}
