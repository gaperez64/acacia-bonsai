#pragma once

#include "actioners/direction.hh"
#include "actioners/profile_dominance.hh"
#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors/traits.hh"
#include "solver/diagnostics.hh"
#include "solver/transition_payload.hh"

#include <algorithm>
#include <bddx.h>
#include <list>
#include <set>
#include <utility>
#include <vector>

namespace actioners {
  namespace detail {
    template <typename State, typename Aut, typename IToIOs>
    class standard {
      public:  // types
        using action =
            std::vector<std::pair<unsigned, bool>>;  // All these pairs are unique by construction.
        using action_vec = std::vector<action>;      // Vector indexed by state number
        using action_vecs = std::list<action_vec>;
        using input_and_actions = std::pair<bdd, action_vecs>;
        /**
         * Later, we'll be using an std::set of input_and_actions. We DO NOT
         * want to end up comparing bdds using the default std::less because
         * that's an actual bdd operation (not a comparison). This is why we
         * have a comparison functor which ignores the bdd below.
         */
        struct compare_actions {
            bool operator() (const input_and_actions& x, const input_and_actions& y) const {
              return (x.second < y.second);
            }
        };
        using input_and_actions_set = std::list<input_and_actions>;

      public:
        standard (const Aut& aut, const IToIOs& inputs_to_ios, VECTOR_ELT_T K)
          : aut {aut},
            K {(VECTOR_ELT_T) K},
            apply_out (aut->num_states ()),
            backward_reset (aut->num_states ()) {
          // Non boolean
          std::fill_n (backward_reset.begin (), posets::vectors::bool_threshold,
                       (VECTOR_ELT_T) (K - 1));
          // Boolean
          std::fill_n (backward_reset.begin () + posets::vectors::bool_threshold,
                       aut->num_states () - posets::vectors::bool_threshold, (VECTOR_ELT_T) 0);

          std::set<input_and_actions, compare_actions> ioset;

          // inputs_to_ios maps each input i to transition sets.  Each set
          // corresponds to an i-compatible IO x and contains every transition
          // p -> q compatible with x.  The IO's BDD is stored alongside it.
          for (const auto& [input, ios] : inputs_to_ios) {
            // input: bdd
            // ios: transition sets and their IOs
            std::list<action_vec> fwd_actions;
            // action_vec : vector<vector<pair<unsigned int, bool>>>
            for (const auto& transset : ios) {
              // transset: transitions compatible with one IO
              // Turn this into a vector that maps q to a list of tuples
              // (p, increment).  The increment comes from the edge when
              // transition acceptance is enabled, and from q otherwise.
              fwd_actions.push_back (compute_action_vec (transset));
              // type that is being inserted: action_vec (ios_precomputers/standard.hh)
              // with current configuration.hh at the time of writing
            }
            // per input: list (one element per compatible IO) of actions
            // what is being inserted = pair<bdd, action_vec> with current configuration.hh at the
            // time of writing
            ioset.insert (std::pair (input, std::move (fwd_actions)));
          }

          for (auto it = ioset.begin (); it != ioset.end ();) {
            // what is being inserted:
            // pair<bdd, list<vector<vector<pair<unsigned int, bool>>>>>
            // -> for every input, a list (one per compatible IO) of actions
            // where an action maps each state q to a list of (p, increment) tuples
            input_output_fwd_actions.push_back (std::move (ioset.extract (it++).value ()));
          }

#if ACACIA_PROFILE_DOMINANCE
          // Keep pruning after extraction: the set's compare_actions ordering and
          // merging are part of the measured input order.  Pruning earlier would
          // change its sort key and confound dominance gains with order changes.
          for (auto& input_and_actions : input_output_fwd_actions) {
            [[maybe_unused]] const auto stats =
                actioners::profile_dominance::prune (input_and_actions.second);
# if ACACIA_ENABLE_DIAGNOSTICS
            if (auto* diag = acacia::diagnostics::current ()) {
              diag->profile_actions_before += stats.actions_before;
              diag->profile_actions_after += stats.actions_after;
              diag->profile_dominance_tests += stats.pair_tests;
              diag->profile_dominance_endpoint_visits += stats.endpoint_visits;
              diag->profile_dominance_declined += stats.declined ? 1 : 0;
              diag->profile_dominance_ms += stats.elapsed_ms;
            }
# endif
          }
#endif
        }

        void setK (VECTOR_ELT_T newK) {
          K = (VECTOR_ELT_T) newK;
          std::fill_n (backward_reset.begin (), posets::vectors::bool_threshold,
                       (VECTOR_ELT_T) (K - 1));
        }

        auto& actions () { return input_output_fwd_actions; }

        State apply (const State& m, const action_vec& avec,
                     direction dir) /* __attribute__((pure)) */ {
          if (dir == direction::forward)
            apply_out.assign (m.size (), (VECTOR_ELT_T) -1);
          else
            apply_out = backward_reset;

          for (size_t p = 0; p < m.size (); ++p) {
            for (const auto& [q, p_final] : avec[p]) {
              if (dir == direction::forward) {
                if (m[q] != -1)
                  apply_out[p] = std::max (
                      apply_out[p],
                      std::min (K, (VECTOR_ELT_T) (m[q] + (VECTOR_ELT_T) (p_final ? 1 : 0))));
              }
              else if (apply_out[q] != -1)
                apply_out[q] =
                    std::min (apply_out[q],
                              std::max ((VECTOR_ELT_T) -1,
                                        (VECTOR_ELT_T) (m[p] - (VECTOR_ELT_T) (p_final ? 1 : 0))));

              // If we reached the extreme value, stop going through states.
              if (dir == direction::forward && apply_out[p] == K)
                break;
            }
          }

          return State (apply_out);
        }

      private:
        const Aut& aut;
        VECTOR_ELT_T K;
        posets::utils::vector_mm<VECTOR_ELT_T> apply_out, backward_reset;
        input_and_actions_set input_output_fwd_actions;

        template <typename Set>
        auto compute_action_vec (const Set& transset) {
          // create action_vec and include transset.second = the IO if needed
          action_vec ret_fwd (aut->num_states ());

          TODO (
              "We have two representations of the same thing here; "
              "see if we can narrow it down to one.");

          // ret_fwd: vector<vector<pair<unsigned int, bool>>>
          // first index = state q, map each state q to a list of tuples (p, increment)

          for (const auto& t : transset)
            ret_fwd[acacia::transitions::dest (t)].push_back (
                std::make_pair (acacia::transitions::source (t),
                                acacia::transitions::increment (t, aut)));

          return ret_fwd;
        }
    };
  }

  template <typename State>
  struct standard {
      template <typename Aut, typename IToIOs>
      static auto make (const Aut& aut, const IToIOs& itoios, VECTOR_ELT_T K) {
        return detail::standard<State, Aut, IToIOs> (aut, itoios, K);
      }
  };
}
