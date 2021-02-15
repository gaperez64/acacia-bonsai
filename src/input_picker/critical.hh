#pragma once

#include "actioner/actioner.hh"

namespace input_picker {
  namespace detail {
    template <typename FwdActions, typename Actioner>
    struct critical {
      public:
        critical (FwdActions& fwd_actions, const Actioner& actioner, int verbose) :
          fwd_actions {fwd_actions}, actioner {actioner}, verbose {verbose} {}

        template <typename SetOfStates>
        auto operator() (const SetOfStates& F) const {
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
          /*auto N = std::min (V.size (), 10ul);
           std::shuffle (V.begin (), V.begin () + N, gen);
           std::shuffle (V.begin () + N / 2, V.end (), gen);*/

          std::list<input_and_actions_ref> Cbar (V.begin (), V.end ());

          auto critical_input = input_and_actions_ref (* (typename input_and_actions_ref::type*) NULL);
          bool found_input = false;

          for (const auto& f : F) {
            bool is_witness = false;
            if (verbose > 2)
              std::cout << "Searching for witness of one-step-loss for " << f << std::endl;

            for (auto it = Cbar.begin (); it != Cbar.end (); ++it) {
              auto& [input, actions] = it->get ();
              is_witness = true;
              auto it_act = actions.begin ();
              for (; it_act != actions.end (); ++it_act)
                if (F.contains (actioner.apply (f, *it_act, actioner::direction::forward))) {
                  is_witness = false;
                  break;
                }

              if (is_witness) {
                // inputs witness one-step-loss of f
                if (verbose > 2) {
                  std::cout << "Input " << input
                            << " witnesses one-step-loss of " << f << std::endl;
                }
                critical_input = *it;
                found_input = true;
                break;
              }

#warning TODO? Put a weight, rather than pushing at the front.
              if (it_act != actions.begin ())
                actions.splice (actions.begin(), actions, it_act);
            }
            if (found_input)
              break;
          }

          if (not found_input)
            return std::pair (critical_input, false);

#warning Discussion point
#if 1
          // BUTCHER
          const auto& [input, actions] = critical_input.get ();
          for (auto it = fwd_actions.begin ();
               it != fwd_actions.end ();
               ++it) {
            if (it->first == input) {
              fwd_actions.splice (fwd_actions.begin(),
                                  fwd_actions,
                                  it);
              break;
            }
          }

#endif


          if (verbose > 1) {
            std::cout << "Critical input: ";
            const auto& [input, actions] = critical_input.get ();
            std::cout << "[" << input << "] " << std::endl;
          }

          return std::pair (critical_input, true);
        }
      private:
        FwdActions& fwd_actions;
        const Actioner& actioner;
        const int verbose;
    };
  }

  struct critical {
      template <typename FwdActions, typename Actioner>
      static auto make (FwdActions& fwd_actions, const Actioner& actioner, int verbose) {
        return detail::critical (fwd_actions, actioner, verbose);
      }
  };
}
