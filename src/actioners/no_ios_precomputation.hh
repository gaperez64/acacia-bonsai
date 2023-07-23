#pragma once

namespace actioners {
  namespace detail {
    template <typename State, typename Aut, typename Supports>
    class no_ios_precomputation {
      public: // types
        using action = std::vector<std::pair<unsigned, bool>>; // All these pairs are unique by construction.
        using action_vec = std::vector<action>;          // Vector indexed by state number
        using action_vecs = std::list<action_vec>;
        using input_and_actions = std::pair<bdd, action_vecs>;
        struct compare_actions {
            // WORST: all code with not
            bool operator() (const input_and_actions& x, const input_and_actions& y) const {
              return (x.second < y.second);
              // Let's favor the actions with the most potential for -1.
              auto num_accepting_of = [this] (const action_vecs& x) {
                int num_accepting = 0;
                for (const auto& tav : x)
                  for (const auto& ta : tav)
                    for (const auto& [_, accepting] : ta)
                      if (accepting) num_accepting++;
                return num_accepting;
              };

              auto x_acc = num_accepting_of (x.second),
                y_acc = num_accepting_of (y.second);

              if (x_acc < y_acc)
                return not true;
              if (x_acc > y_acc)
                return not false;
              return not (x.second < y.second);
            }
        };
        using input_and_actions_set = std::list<input_and_actions>;
      public:
        no_ios_precomputation (const Aut& aut, const Supports& supports, int K) :
          aut {aut}, K {K},
          apply_out (aut->num_states ()), mcopy (aut->num_states ()) {

          mcopy.reserve (State::capacity_for (mcopy.size ()));

          std::map<action_vecs, bdd> ioset;
          bdd input_letters = bddtrue;
          while (input_letters != bddfalse) {
            bdd one_input_letter = pick_one_letter (input_letters, supports.first);
            action_vecs fwd_actions;
            bdd output_letters = bddtrue;
            while (output_letters != bddfalse) {
              bdd one_output_letter = pick_one_letter (output_letters, supports.second);
              const auto& fwd = compute_action (one_input_letter & one_output_letter);
              fwd_actions.push_back (std::move (fwd));
            }
            ioset[fwd_actions] = one_input_letter;
          }

          for (auto it = ioset.begin(); it != ioset.end(); ) {
            auto e = ioset.extract (it++);
            input_output_fwd_actions.emplace_back (e.mapped (), std::move (e.key ()));
          }
        }

        void setK (int newK) { K = newK; }

        auto& actions () { return input_output_fwd_actions; }

        State apply (const State& m, const action_vec& avec, direction dir) /* __attribute__((pure)) */ {
          if (dir == direction::forward)
            apply_out.assign (m.size (), (VECTOR_ELT_T) -1);
          else {
            // Non boolean
            std::fill_n (apply_out.begin (),
                         vectors::bool_threshold,
                         (VECTOR_ELT_T) (K - 1));
            // Boolean
            std::fill_n (apply_out.begin () + vectors::bool_threshold,
                         m.size () - vectors::bool_threshold,
                         (VECTOR_ELT_T) 0);
          }

          m.to_vector (mcopy);

          for (size_t p = 0; p < m.size (); ++p) {
            for (const auto& [q, q_final] : avec[p]) {
              if (dir == direction::forward) {
                if (mcopy[q] != -1)
                  apply_out[p] = std::max (apply_out[p], std::min ((VECTOR_ELT_T) K, (VECTOR_ELT_T) (mcopy[q] + (VECTOR_ELT_T) (q_final ? 1 : 0))));
              } else
                if (apply_out[q] != -1)
                  apply_out[q] = std::min (apply_out[q], std::max ((VECTOR_ELT_T) -1, (VECTOR_ELT_T) (mcopy[p] - (VECTOR_ELT_T) (q_final ? 1 : 0))));

              // If we reached the extreme value, stop going through states.
              if (dir == direction::forward && apply_out[p] == K)
                break;
            }
          }

          return State (apply_out);
        }

      private:
        const Aut& aut;
        int K;
        utils::vector_mm<VECTOR_ELT_T> apply_out, mcopy;
        input_and_actions_set input_output_fwd_actions;

        auto compute_action (bdd letter) {
          action_vec ret_fwd (aut->num_states ());

          for (size_t p = 0; p < aut->num_states (); ++p) {
            for (const auto& e : aut->out (p)) {
              unsigned q = e.dst;
              if ((e.cond & letter) != bddfalse) {
                ret_fwd[q].push_back (std::make_pair (p, aut->state_is_accepting (q)));
              }
            }
          }
          return ret_fwd;
        }
        static bdd pick_one_letter (bdd& letter_set, const bdd& support) {
          bdd one_letter = bdd_satoneset (letter_set,
                                          support,
                                          bddtrue);
          letter_set -= one_letter;
          return one_letter;
        }
    };
  }

  template <typename State>
  struct no_ios_precomputation {
      template <typename Aut, typename Supports>
      static auto make (const Aut& aut, const Supports& supports, int K) {
        return detail::no_ios_precomputation<State, Aut, Supports> (aut, supports, K);
      }
  };
}
