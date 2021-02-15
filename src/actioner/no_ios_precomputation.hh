#pragma once

namespace actioner {
  namespace detail {
    template <typename Aut, typename Supports>
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
              auto num_accepting_of = [this] (const action_vecs& x) constexpr {
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
        no_ios_precomputation (const Aut& aut, const Supports& supports, int K, int verbose) :
          aut {aut}, K {K}, verbose {verbose} {
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

        auto& actions () { return input_output_fwd_actions; }

        template <typename State>
        State apply (const State& m, const action_vec& avec, direction dir) const /* __attribute__((pure)) */ {
          State f (m.size ());

          for (size_t p = 0; p < m.size (); ++p)
            if (dir == direction::forward)
              f[p] = -1;
            else
              f[p] = K - 1;

          for (size_t p = 0; p < m.size (); ++p) {
            for (const auto& [q, q_final] : avec[p]) {
              if (dir == direction::forward) {
                if (m[q] != -1)
                  f[p] = (int) std::max ((int) f[p], std::min (K, (int) m[q] + (q_final ? 1 : 0)));
              } else
                if (f[q] != -1)
                  f[q] = (int) std::min ((int) f[q], std::max (-1, (int) m[p] - (q_final ? 1 : 0)));

              // If we reached the extreme value, stop going through states.
              if (dir == direction::forward && f[p] == K)
                break;
            }
          }
          return f;
        }

      private:
        const Aut& aut;
        const int K, verbose;
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

  struct no_ios_precomputation {
      template <typename Aut, typename Supports>
      static auto make (const Aut& aut, const Supports& supports, int K, int verbose) {
        return detail::no_ios_precomputation (aut, supports, K, verbose);
      }
  };
}
