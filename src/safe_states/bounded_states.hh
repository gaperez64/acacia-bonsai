#pragma once

// So-called "Optimization 1" in ac+.
// A state is bounded if it cannot carry a counter value of at least k.

namespace safe_states {
  namespace detail {
    template <typename SetOfStates, typename Aut>
    class bounded_states {
        typedef typename SetOfStates::value_type State;
      public:
        bounded_states (Aut aut, int K, int verbose) : aut {aut}, K {K}, verbose {verbose} {}

        SetOfStates operator() () const {
          State f (aut->num_states ());

          int nb_accepting_states = 0;
          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (aut->state_is_accepting (src))
              nb_accepting_states++;

          auto c = std::vector<int> (aut->num_states ());
          if (aut->state_is_accepting (aut->get_init_state_number ()))
            c[aut->get_init_state_number ()] = 1;

          bool has_changed = true;

          #warning Set accepting states to nb_accepting_states + 1?
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
          unsigned bounded = 0;
          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (c[src] > nb_accepting_states)
              f[src] = K - 1;
            else {
              bounded++;
              f[src] = 0;
            }

          if (verbose)
            std::cout << "Bounded states: " << bounded << " / "
                      << aut->num_states () << " = "
                      << (bounded * 100) / aut->num_states () << "%" << std::endl;

          SetOfStates S (std::move (f));

          return S;
        }

      private:
        const Aut aut;
        const int K, verbose;
    };
  }

  struct bounded_states {
      template <typename SetOfStates, typename Aut>
      static auto make (Aut aut, int K, int verbose) {
        return detail::bounded_states<SetOfStates, Aut> (aut, K, verbose);
      }
  };
}
