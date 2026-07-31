#pragma once

#include "actioners/direction.hh"
#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors/traits.hh"

#include <bddx.h>
#include <list>
#include <map>
#include <vector>

/**
 * This actioner is different than the other ones and is meant to be used in
 * combination with ios_precomputers::delegate.
 */

namespace actioners {
  namespace detail {
    template <typename State, typename Aut, typename Supports>
    class no_ios_precomputation {
      public:
        // Types/Vocabulary:
        // action             - transitions from a single state (keeping track
        //                      of whether the successor is accepting/final)
        // action_vec         - collection of per_state single_state_trans
        //                      and the bdd representing the output part
        //                      of the IO that enabled the transitions
        // action_vecs        - list of action_vecs compatible with a single
        //                      input letter
        //
        using action = std::vector<std::pair<unsigned, bool>>;  // All these are unique by
                                                                // construction.
        /**
         * Why not just a vector? We want to keep track of the output-only bdd
         * that gave rise to these per-state transitions. For most users, this
         * is hidden by this wrapper exposing the main vector API
         */
        class action_vec {
          private:
            std::vector<action> actions;  // Vector indexed by state number
            bdd output_letter;

          public:
            action_vec () = delete;
            action_vec (const action_vec&) = default;  // static_switch needs
                                                       // this it seems?
            action_vec (action_vec&&) = default;
            action_vec (std::vector<action>&& acts, bdd out)
              : actions {std::move (acts)},
                output_letter {out} {}
            action_vec& operator= (const action_vec&) = delete;
            action_vec& operator= (action_vec&&) = default;
            bdd output () const { return output_letter; }

            // Exposing the vector API
            auto begin () const { return actions.begin (); }
            auto end () const { return actions.end (); }
            auto& operator[] (size_t i) { return actions[i]; }
            const auto& operator[] (size_t i) const { return actions[i]; }
            size_t size () const { return actions.size (); }

            // Important for set and map usage of a vector of action_vec, for
            // instance.
            //
            // Note that we ignore the output bdd; this is on purpose!
            bool operator< (const action_vec& rhs) const { return actions < rhs.actions; }
        };
        using action_vecs = std::list<action_vec>;
        using input_and_actions = std::pair<bdd, action_vecs>;
        using input_and_actions_set = std::list<input_and_actions>;

      public:
        no_ios_precomputation (const Aut& aut, const Supports& supports, VECTOR_ELT_T K)
          : aut {aut},
            K {K},
            apply_out (aut->num_states ()) {
          std::map<action_vecs, bdd> ioset;
          bdd input_letters = bddtrue;
          while (input_letters != bddfalse) {
            bdd one_input_letter = pick_one_letter (input_letters, supports.first);
            action_vecs fwd_actions;
            bdd output_letters = bddtrue;
            while (output_letters != bddfalse) {
              bdd one_output_letter = pick_one_letter (output_letters, supports.second);
              const auto& fwd = compute_action (one_input_letter, one_output_letter);
              fwd_actions.push_back (std::move (fwd));
            }
            if (ioset.find (fwd_actions) == ioset.end ()) {
              ioset[fwd_actions] = one_input_letter;
            } else {
              ioset[fwd_actions] |= one_input_letter;
            }
          }

          for (auto it = ioset.begin (); it != ioset.end ();) {
            auto e = ioset.extract (it++);
            input_output_fwd_actions.emplace_back (e.mapped (), std::move (e.key ()));
          }
        }

        void setK (VECTOR_ELT_T newK) { K = newK; }

        auto& actions () { return input_output_fwd_actions; }

        State apply (const State& m, const action_vec& avec,
                     direction dir) /* __attribute__((pure)) */ {
          if (dir == direction::forward)
            apply_out.assign (m.size (), (VECTOR_ELT_T) -1);
          else {
            // Non boolean
            std::fill_n (apply_out.begin (), posets::vectors::bool_threshold,
                         (VECTOR_ELT_T) (K - 1));
            // Boolean
            std::fill_n (apply_out.begin () + posets::vectors::bool_threshold,
                         m.size () - posets::vectors::bool_threshold, (VECTOR_ELT_T) 0);
          }

          for (size_t p = 0; p < m.size (); ++p) {
            for (const auto& [q, q_final] : avec[p]) {
              if (dir == direction::forward) {
                if (m[q] != -1)
                  apply_out[p] = std::max (
                      apply_out[p],
                      std::min ((VECTOR_ELT_T) K,
                                (VECTOR_ELT_T) (m[q] + (VECTOR_ELT_T) (q_final ? 1 : 0))));
              }
              else if (apply_out[q] != -1)
                apply_out[q] =
                    std::min (apply_out[q],
                              std::max ((VECTOR_ELT_T) -1,
                                        (VECTOR_ELT_T) (m[p] - (VECTOR_ELT_T) (q_final ? 1 : 0))));

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
        posets::utils::vector_mm<VECTOR_ELT_T> apply_out;
        input_and_actions_set input_output_fwd_actions;

        auto compute_action (bdd input_letter, bdd output_letter) {
          std::vector<action> ret_fwd (aut->num_states ());
          bdd letter = input_letter & output_letter;
          for (size_t p = 0; p < aut->num_states (); ++p) {
            for (const auto& e : aut->out (p)) {
              unsigned q = e.dst;
              if ((e.cond & letter) != bddfalse)
                ret_fwd[q].emplace_back (p, aut->state_is_accepting (q));
            }
          }
          return action_vec (std::move (ret_fwd), output_letter);
        }
        static bdd pick_one_letter (bdd& letter_set, const bdd& support) {
          bdd one_letter = bdd_satoneset (letter_set, support, bddtrue);
          letter_set -= one_letter;
          return one_letter;
        }
    };
  }

  template <typename State>
  struct no_ios_precomputation {
      template <typename Aut, typename Supports>
      static auto make (const Aut& aut, const Supports& supports, VECTOR_ELT_T K) {
        return detail::no_ios_precomputation<State, Aut, Supports> (aut, supports, K);
      }
  };
}
