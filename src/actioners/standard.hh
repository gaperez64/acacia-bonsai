#pragma once

#include "actioners/direction.hh"
#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors/traits.hh"

#include <algorithm>
#include <bddx.h>
#include <list>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

namespace actioners {
  namespace detail {
    struct csr_action_vec {
        std::vector<size_t> offsets;
        std::vector<unsigned> targets;
        std::vector<bool> accepting;

        [[nodiscard]] bool operator< (const csr_action_vec& rhs) const {
          if (offsets != rhs.offsets)
            return offsets < rhs.offsets;
          if (targets != rhs.targets)
            return targets < rhs.targets;
          return accepting < rhs.accepting;
        }
    };

    class csr_action_vecs {
      public:
        void push_back (csr_action_vec&& action) {
          order.push_back (order.size ());
          values.push_back (std::move (action));
        }

        [[nodiscard]] auto begin () { return values.begin (); }
        [[nodiscard]] auto end () { return values.end (); }
        [[nodiscard]] auto begin () const { return values.begin (); }
        [[nodiscard]] auto end () const { return values.end (); }
        [[nodiscard]] size_t size () const { return values.size (); }

        csr_action_vec& ordered (size_t position) { return values[order[position]]; }

        void promote (size_t position) {
          std::rotate (order.begin (), order.begin () + position, order.begin () + position + 1);
        }

        [[nodiscard]] bool operator< (const csr_action_vecs& rhs) const {
          return values < rhs.values;
        }

      private:
        std::vector<csr_action_vec> values;
        std::vector<size_t> order;
    };

    using csr_input_and_actions = std::pair<bdd, csr_action_vecs>;
    struct compare_csr_actions {
        bool operator() (const csr_input_and_actions& x, const csr_input_and_actions& y) const {
          return x.second < y.second;
        }
    };
    using csr_input_and_actions_set = std::list<csr_input_and_actions>;

    template <typename State, typename Aut, typename IToIOs>
    class standard {
      public:  // types
        using action_vec = csr_action_vec;
        using legacy_action = std::vector<std::pair<unsigned, bool>>;
        using legacy_action_vec = std::vector<legacy_action>;
        using action_vecs = csr_action_vecs;
        using input_and_actions = csr_input_and_actions;
        using input_and_actions_set = csr_input_and_actions_set;

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

          std::set<input_and_actions, compare_csr_actions> ioset;

          // inputs_to_ios: a map [input i, set of sets of pairs (p, q)].  Each set of pairs (p, q)
          // corresponds to an i-compatible IO x in the natural way; that is, it is the set
          // of pairs (p, q) such that p -> q is compatible with x.
          // This set of pairs (p, q) also includes a BDD = the IO
          for (const auto& [input, ios] : inputs_to_ios) {
            // input: bdd
            // ios: set of pairs of (sets (p, q) and IO)
            action_vecs fwd_actions;
            for (const auto& transset : ios) {
              // transset: transitions_io_pair (stores vector<pair<p, q>> and IO)
              // turn this into a vector that maps q to a list of tuples (p, is_q_accepting) and
              // keep the IO insert this map for every transset
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
            // where an action maps each state q to a list of (p, is_q_accepting) tuples
            input_output_fwd_actions.push_back (std::move (ioset.extract (it++).value ()));
          }
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
            const bool p_final = avec.accepting[p];
            for (size_t edge = avec.offsets[p]; edge < avec.offsets[p + 1]; ++edge) {
              const unsigned q = avec.targets[edge];
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
              if (dir == direction::forward and apply_out[p] == K)
                break;
            }
          }

          return State (apply_out);
        }

        State apply (const State& m, const legacy_action_vec& avec,
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

              if (dir == direction::forward and apply_out[p] == K)
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
          const size_t num_states = aut->num_states ();
          action_vec ret_fwd {
              .offsets = std::vector<size_t> (num_states + 1),
              .targets = {},
              .accepting = std::vector<bool> (num_states),
          };

          for (const auto& [p, q] : transset) {
            ++ret_fwd.offsets[q + 1];
            ret_fwd.accepting[q] = aut->state_is_accepting (q);
          }
          std::partial_sum (ret_fwd.offsets.begin (), ret_fwd.offsets.end (),
                            ret_fwd.offsets.begin ());

          ret_fwd.targets.resize (ret_fwd.offsets.back ());
          auto cursor = ret_fwd.offsets;
          for (const auto& [p, q] : transset)
            ret_fwd.targets[cursor[q]++] = p;

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
