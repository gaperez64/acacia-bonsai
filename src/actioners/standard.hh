#pragma once

#include "actioners/direction.hh"
#include "actioners/compact_payload.hh"
#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors/traits.hh"
#include "solver/diagnostics.hh"
#include "solver/equivariance_budget.hh"
#include "solver/phase_observation.hh"
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
        using action_vec = compact_payload;
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
          const bool observed = acacia::phase_records_enabled ();
          acacia::legacy_phase constructing ("action-construction");
          // Non boolean
          std::fill_n (backward_reset.begin (), posets::vectors::bool_threshold,
                       (VECTOR_ELT_T) (K - 1));
          // Boolean
          std::fill_n (backward_reset.begin () + posets::vectors::bool_threshold,
                       aut->num_states () - posets::vectors::bool_threshold, (VECTOR_ELT_T) 0);

          std::set<input_and_actions, compare_actions> ioset;
          size_t observed_retained = 0, observed_peak = 0;
          size_t observed_rows = 0, observed_nonempty = 0, observed_payloads = 0;
          size_t observed_endpoints = 0, observed_endpoint_bytes = 0;
          size_t next_observation = 1;
          const auto publish_density = [&] () {
            acacia::legacy_count ("action_prefix_payloads", observed_payloads);
            acacia::legacy_count ("action_prefix_rows", observed_rows);
            acacia::legacy_count ("action_prefix_nonempty_rows", observed_nonempty);
            acacia::legacy_count ("action_prefix_endpoints", observed_endpoints);
            acacia::legacy_count ("action_prefix_endpoint_bytes", observed_endpoint_bytes);
            acacia::legacy_count ("action_prefix_payload_bytes", observed_retained);
          };

          // inputs_to_ios maps each input i to transition sets.  Each set
          // corresponds to an i-compatible IO x and contains every transition
          // p -> q compatible with x.  The IO's BDD is stored alongside it.
          for (const auto& [input, ios] : inputs_to_ios) {
            acacia::equivariance_budget::checkpoint ();
            // input: bdd
            // ios: transition sets and their IOs
            std::list<action_vec> fwd_actions;
            for (const auto& transset : ios) {
              acacia::equivariance_budget::checkpoint ();
              // transset: transitions compatible with one IO
              // Turn this into a vector that maps q to a list of tuples
              // (p, increment).  The increment comes from the edge when
              // transition acceptance is enabled, and from q otherwise.
              fwd_actions.push_back (compute_action_vec (transset));
            }
            // per input: list (one element per compatible IO) of actions
            size_t pending_bytes = 0, pending_rows = 0, pending_nonempty = 0;
            size_t pending_endpoints = 0, pending_endpoint_bytes = 0;
            if (observed) {
              pending_bytes = sizeof (input_and_actions);
              for (const auto& action : fwd_actions) {
                pending_bytes += action.payload_bytes ();
                pending_rows += action.size ();
                pending_nonempty += action.nonempty_size ();
                pending_endpoints += action.endpoint_count ();
                pending_endpoint_bytes += action.endpoint_bytes ();
              }
              observed_peak = std::max (observed_peak, observed_retained + pending_bytes);
            }
            const auto inserted = ioset.insert (std::pair (input, std::move (fwd_actions)));
            if (observed && inserted.second) {
              observed_retained += pending_bytes;
              observed_rows += pending_rows;
              observed_nonempty += pending_nonempty;
              observed_endpoints += pending_endpoints;
              observed_endpoint_bytes += pending_endpoint_bytes;
              observed_payloads += inserted.first->second.size ();
              if (observed_payloads >= next_observation) {
                publish_density ();
                next_observation = observed_payloads * 2;
              }
            }
          }

          for (auto it = ioset.begin (); it != ioset.end ();) {
            // -> for every input, a list (one per compatible IO) of actions
            // where an action maps each state q to a list of (p, increment) tuples
            input_output_fwd_actions.push_back (std::move (ioset.extract (it++).value ()));
          }

          if (observed) {
            publish_density ();
            acacia::legacy_count ("action_construction_peak_payload_bytes_estimate",
                                  observed_peak);
            acacia::legacy_count ("action_table_payload_bytes_estimate",
                                  acacia::legacy_action_bytes (input_output_fwd_actions));
            size_t payloads = 0, endpoints = 0;
            for (const auto& [input, actions] : input_output_fwd_actions) {
              (void) input;
              payloads += actions.size ();
              for (const auto& action : actions)
                endpoints += action.endpoint_count ();
            }
            acacia::legacy_count ("action_payloads", payloads);
            acacia::legacy_count ("action_endpoints", endpoints);
          }
        }

        void setK (VECTOR_ELT_T newK) {
          K = (VECTOR_ELT_T) newK;
          std::fill_n (backward_reset.begin (), posets::vectors::bool_threshold,
                       (VECTOR_ELT_T) (K - 1));
        }

        auto& actions () { return input_output_fwd_actions; }

        template <typename Payload>
        State apply (const State& m, const Payload& avec,
                     direction dir) /* __attribute__((pure)) */ {
          acacia::equivariance_budget::checkpoint ();
          if (dir == direction::forward)
            apply_out.assign (m.size (), (VECTOR_ELT_T) -1);
          else
            apply_out = backward_reset;

          const auto apply_row = [&] (size_t p, const auto& endpoints) {
            for (const auto& [q, p_final] : endpoints) {
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
          };

          if constexpr (requires { avec.nonempty_size (); }) {
            for (size_t row = 0; row < avec.nonempty_size (); ++row)
              apply_row (avec.destination (row), avec.nonempty_row (row));
          }
          else {
            for (size_t p = 0; p < m.size (); ++p)
              apply_row (p, avec[p]);
          }

          query_progress.completed (m);
          return State (apply_out);
        }

      private:
        const Aut& aut;
        VECTOR_ELT_T K;
        posets::utils::vector_mm<VECTOR_ELT_T> apply_out, backward_reset;
        input_and_actions_set input_output_fwd_actions;
        acacia::legacy_query_progress query_progress;

        template <typename Set>
        auto compute_action_vec (const Set& transset) {
          return compact_payload::make (aut->num_states (), transset, aut);
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
