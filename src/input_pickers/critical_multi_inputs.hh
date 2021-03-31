#pragma once

#include "actioners.hh"

namespace input_pickers {
  namespace detail {
    template <typename FwdActions, typename Actioner>
    struct critical_multi_inputs {
      public:
        critical_multi_inputs (FwdActions& fwd_actions, const Actioner& actioner, int verbose) :
          fwd_actions {fwd_actions}, actioner {actioner}, verbose {verbose} {}

        template <typename SetOfStates, typename State, typename ActionVecList>
        static auto max_bwd_change_of (const SetOfStates& F,
                                       const State& f, const ActionVecList& output_actions,
                                       const Actioner& actioner) {
          SetOfStates pre;

          for (const auto& action : output_actions)
            pre.insert (actioner.apply (f, action,  actioners::direction::backward));

          //pre.intersect_with (F);
          ssize_t max_change = 0;
          for (const auto& vec : pre)
            if (F.contains (vec)) // If vec is not in F, it won't appear in cpre.
              for (size_t p = 0; p < f.size (); ++p)
                if (f[p] > max_change && vec[p] < f[p])
                  max_change = f[p];

          return max_change;
        }

        double v = 10;
        template <typename SetOfStates>
        auto operator() (const SetOfStates& F, ssize_t min_max_change = 9,
                         ssize_t backup_min_max_change = 7) {
          // Def: f is one-step-losing if there is an input i such that for all output o
          //           succ (f, <i,o>) \not\in F
          //      the input i is the *witness* of one-step-loss.
          // Def: A set C of inputs is critical for F if:
          //        \exists f \in F, i \in C, i witnesses one-step-loss of F.
          // Algo: We go through all f in F, find an input i witnessing one-step-loss, add it to C.

          v = v - 0.5;
          if (v < 0) v = 0;
          min_max_change = v;
          backup_min_max_change = v / 2;
          if (verbose) std::cout << "Current K: " << min_max_change  << std::endl;
          // Sort/randomize input_output_fwd_actions
          using input_and_actions_ref = std::reference_wrapper<typename FwdActions::value_type>;
          using input_and_actions_ref_list = std::list<input_and_actions_ref>;
          std::vector<input_and_actions_ref> V (fwd_actions.begin (),
                                                fwd_actions.end ());
          /*auto N = std::min (V.size (), 10ul);
           std::shuffle (V.begin (), V.begin () + N, gen);
           std::shuffle (V.begin () + N / 2, V.end (), gen);*/

          std::list<input_and_actions_ref> Cbar (V.begin (), V.end ());
          input_and_actions_ref_list critical_inputs, backup_critical_inputs, max_change_inputs;
          ssize_t max_change = -1;

          for (const auto& f : F) {
            bool is_witness = false;
            if (verbose > 2)
              std::cout << "Searching for witness of one-step-loss for " << f << std::endl;

            for (auto it = Cbar.begin (); it != Cbar.end (); /* in-body update */) {
              auto& [input, actions] = it->get ();
              is_witness = true;
              auto it_act = actions.begin ();
              for (; it_act != actions.end (); ++it_act)
                if (F.contains (actioner.apply (f, *it_act, actioners::direction::forward))) {
                  is_witness = false;
                  break;
                }

              if (not is_witness) {
                // The action proving nonwitness is likely to do it again, put it at
                // the front.
#warning TODO? Put a weight, rather than pushing at the front.
                if (it_act != actions.begin ())
                  actions.splice (actions.begin(), actions, it_act);
                ++it;
                continue;
              }

              // inputs witness one-step-loss of f
              if (verbose > 2) {
                std::cout << "Input " << input
                          << " witnesses one-step-loss of " << f << std::endl;
              }

              auto max_bwd_change = max_bwd_change_of (F, f, actions, actioner);
              if (max_change < max_bwd_change) {
                max_change = max_bwd_change;
                max_change_inputs.clear ();
              }

              if (max_bwd_change < min_max_change) {
                if (max_change == max_bwd_change && max_change_inputs.size () < MAX_CRITICAL_INPUTS)
                  max_change_inputs.push_back (*it);
                if (max_bwd_change >= backup_min_max_change && backup_critical_inputs.size () < MAX_CRITICAL_INPUTS)
                  backup_critical_inputs.push_back (*it);
                it++;
                continue;
              }

              critical_inputs.push_back (*it);
              it = Cbar.erase (it);
              if (critical_inputs.size () >= MAX_CRITICAL_INPUTS)
                break;
            }

            if (critical_inputs.size () >= MAX_CRITICAL_INPUTS)
              break;
          }

          if (critical_inputs.empty ()) { // No critical signal with a sufficient max_change
            if (backup_critical_inputs.empty ()) // Not even backup min max_change
              critical_inputs = std::move (max_change_inputs);
            else
              critical_inputs = std::move (backup_critical_inputs);
          }
#warning Discussion point
#if 0
          // BUTCHER
          for (const auto& c : critical_inputs) {
            const auto& [input, actions] = c.get ();
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
          }
#endif


          if (verbose > 1) {
            std::cout << "Critical inputs: ";
            for (const auto& c : critical_inputs) {
              const auto& [input, actions] = c.get ();
              std::cout << "[" << input << "] ";
            }
            std::cout << std::endl;
          }

          return std::pair (critical_inputs.front(), not critical_inputs.empty());
         }
      private:
        FwdActions& fwd_actions;
        const Actioner& actioner;
        const int verbose;
    };
  }

  struct critical_multi_inputs {
      template <typename FwdActions, typename Actioner>
      static auto make (FwdActions& fwd_actions, const Actioner& actioner, int verbose) {
        return detail::critical_multi_inputs (fwd_actions, actioner, verbose);
      }
  };
}
