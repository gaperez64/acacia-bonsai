#pragma once

// So-called "Optimization 1" in ac+.
// A state is bounded if it cannot carry a counter value of at least k.

namespace bounded_states {
  namespace detail {
    template <typename Aut>
    class forward_saturation {
      public:
        forward_saturation (Aut aut, int K, int verbose) : aut {aut}, K {K}, verbose {verbose} {}

        size_t operator() () const {
          int nb_accepting_states = 0, nbounded = aut->num_states ();

          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (aut->state_is_accepting (src))
              nb_accepting_states++;

          auto c = std::vector<char> (aut->num_states ());
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
                  if (c_src_mod == nb_accepting_states + 1)
                    nbounded--;
                  c[e.dst] = c_src_mod;
                  has_changed = true;
                }
            }
          }

          auto rename = std::vector<unsigned> (aut->num_states ());

          unsigned bounded = 0, unbounded = 0;
          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (c[src] > nb_accepting_states)
              rename[src] = nbounded + unbounded++;
            else
              rename[src] = bounded++;

          assert (bounded == nbounded);

          if (verbose)
            std::cout << "Bounded states: " << bounded << " / "
                      << aut->num_states () << " = "
                      << (bounded * 100) / aut->num_states () << "%" << std::endl;

          // WARNING: Internal Spot
          auto& g = aut->get_graph();
          g.rename_states_(rename);
          aut->set_init_state(rename[aut->get_init_state_number()]);
          g.sort_edges_();
          g.chain_edges_();

          return nbounded;
        }

      private:
        const Aut aut;
        const int K, verbose;
    };
  }

  struct forward_saturation {
      template <typename Aut>
      static auto make (Aut aut, int K, int verbose) {
        return detail::forward_saturation<Aut> (aut, K, verbose);
      }
  };
}
