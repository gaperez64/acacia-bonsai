#pragma once

namespace safe_states {
  namespace detail {
    template <typename SetOfStates, typename Aut>
    class standard {
        typedef typename SetOfStates::value_type State;
      public:
        standard (Aut aut, int K, int verbose) : aut {aut}, K {K}, verbose {verbose} {}

        SetOfStates operator() () const {
          State f (aut->num_states ());

          // So-called "Optimization 1" in ac+.
          int nb_accepting_states = 0;
          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (aut->state_is_accepting (src))
              nb_accepting_states++;

          auto c = std::vector<int> (aut->num_states ());
          if (aut->state_is_accepting (aut->get_init_state_number ()))
            c[aut->get_init_state_number ()] = 1;

          bool has_changed = true;

          while (has_changed) {
            has_changed = false;

            for (unsigned src = 0; src < aut->num_states (); ++src) {
              int c_src_mod = std::min (nb_accepting_states + 1,
                                        c[src] + (aut->state_is_accepting (src) ? 1 : 0));
              for (const auto& e : aut->out (src))
                if (c[e.dst] < c_src_mod) {
                  c[e.dst] = c_src_mod;
                  has_changed = true;
                }
            }
          }

#warning TODO: Remove the 0 states?
          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (c[src] > nb_accepting_states)
              f[src] = K - 1;
            else
              f[src] = 0;

          SetOfStates S (std::move (f));
          S.downward_close ();

          return S;
        }
      private:
        const Aut aut;
        const int K, verbose;
    };
  }

  struct standard {
      template <typename SetOfStates, typename Aut>
      static auto make (Aut aut, int K, int verbose) {
        return detail::standard<SetOfStates, Aut> (aut, K, verbose);
      }
  };
}
