#pragma once

namespace actioners {
  namespace detail {
    template <typename State, typename Aut, typename IToIOs>
    class standard {
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
        standard (const Aut& aut, const IToIOs& inputs_to_ios, int K, int verbose) :
          aut {aut}, K {(char) K}, verbose {(char) verbose},
          apply_out (aut->num_states ()), mcopy (aut->num_states ()) {

          mcopy.reserve (State::capacity_for (mcopy.size ()));

#warning TODO? Cache compute_action_vec?
          std::set<input_and_actions, compare_actions> ioset;

          for (const auto& [input, ios] : inputs_to_ios) {
            std::list<action_vec> fwd_actions;
            for (const auto& transset : ios) {
              fwd_actions.push_back (compute_action_vec (transset));
            }
            ioset.insert (std::pair (input, std::move (fwd_actions)));
          }

          for (auto it = ioset.begin(); it != ioset.end(); ) {
            input_output_fwd_actions.push_back (std::move (ioset.extract (it++).value ()));
          }
        }

        void setK (int newK) { K = (char) newK; }

        auto& actions () { return input_output_fwd_actions; }

        State apply (const State& m, const action_vec& avec, direction dir) /* __attribute__((pure)) */ {
          if (dir == direction::forward)
            apply_out.assign (m.size (), (char) -1);
          else
#warning Assign 0 to bool states
            apply_out.assign (m.size (), (char) (K - 1));

          m.to_vector (mcopy);

          for (size_t p = 0; p < m.size (); ++p) {
            for (const auto& [q, q_final] : avec[p]) {
              if (dir == direction::forward) {
                if (mcopy[q] != -1)
                  apply_out[p] = std::max (apply_out[p], std::min ((char) K, (char) (mcopy[q] + (char) (q_final ? 1 : 0))));
              } else
                if (apply_out[q] != -1)
                  apply_out[q] = std::min (apply_out[q], std::max ((char) -1, (char) (mcopy[p] - (char) (q_final ? 1 : 0))));

              // If we reached the extreme value, stop going through states.
              if (dir == direction::forward && apply_out[p] == K)
                break;
            }
          }

          return State (apply_out);
        }

       private:
        char K;
        const Aut& aut;
        const char /*  K, */ verbose;
        std::vector<char> apply_out, mcopy;
        input_and_actions_set input_output_fwd_actions;

        template <typename Set>
        auto compute_action_vec (const Set& transset) {
          action_vec ret_fwd (aut->num_states ());
#warning TODO: That seems overkill, why not just have transset by itself?  That seems to have little sense.  Test this.

          for (const auto& [p, q] : transset)
            ret_fwd[q].push_back (std::make_pair (p, aut->state_is_accepting (q)));

          return ret_fwd;
        }
    };
  }

  template <typename State>
  struct standard {
      template <typename Aut, typename IToIOs>
      static auto make (const Aut& aut, const IToIOs& itoios, int K, int verbose) {
        return detail::standard<State, Aut, IToIOs> (aut, itoios, K, verbose);
      }
  };
}
