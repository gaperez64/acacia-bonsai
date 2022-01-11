#pragma once

// So-called "Optimization 1" in ac+.
// A state is bounded if it cannot carry a counter value of at least k.
/* Note: In ac+, this is computed backward:
   while (has_changed)
    for dst in aut
      c_dst = c[dst]
      for src s.t. (src, dst) in aut
        t = min (nb_accepting_states + 1, c[src] + (accepting(src)?1:0))
        if (t > c_dst) { c_dst = t; has_changed = true}
      c[dst] = c_dst;
   ... and uses a copy of c in each loop.  Not sure why. */

TODO ("Implement backward saturation.");
TODO ("Use Spot's SCC implementation.");

namespace boolean_states {
  namespace detail {
    template <typename Aut>
    class forward_saturation {
      public:
        forward_saturation (Aut aut, int K) : aut {aut}, K {K} {}

        size_t operator() () const {
          unsigned nb_accepting_states = 0, nunbounded = 0;

          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (aut->state_is_accepting (src))
              nb_accepting_states++;

          assert (nb_accepting_states < UCHAR_MAX);
          auto c = std::vector<unsigned char> (aut->num_states ());
          if (aut->state_is_accepting (aut->get_init_state_number ()))
            c[aut->get_init_state_number ()] = 1;

          bool has_changed = true;

          while (has_changed) {
            has_changed = false;

            for (unsigned src = 0; src < aut->num_states (); ++src) {
              unsigned c_src_mod = std::min (nb_accepting_states + 1u,
                                             c[src] + (aut->state_is_accepting (src) ? 1u : 0u));
              for (const auto& e : aut->out (src))
                if (c[e.dst] < c_src_mod) {
                  if (c_src_mod == nb_accepting_states + 1)
                    nunbounded++;
                  c[e.dst] = c_src_mod;
                  has_changed = true;
                }
            }
          }

          auto rename = std::vector<unsigned> (aut->num_states ());

          unsigned bounded = 0, unbounded = 0;
          for (unsigned src = 0; src < aut->num_states (); ++src)
            if (c[src] > nb_accepting_states)
              rename[src] = unbounded++;
            else {
              verb_do (2, vout << "Found bounded state: " << src << std::endl);
              // Make it not accepting
              for (auto& e : aut->out (src))
                e.acc = spot::acc_cond::mark_t {};
              rename[src] = nunbounded + bounded++;
            }

          assert (unbounded == nunbounded);

          verb_do (1, vout << "Bounded states: " << bounded << " / "
                   /*   */ << aut->num_states () << " = "
                   /*   */ << (bounded * 100) / aut->num_states () << "%" << std::endl);

          // WARNING: Internal Spot
          auto& g = aut->get_graph();
          g.rename_states_(rename);
          aut->set_init_state(rename[aut->get_init_state_number()]);
          g.sort_edges_();
          g.chain_edges_();
          aut->prop_universal(spot::trival::maybe ());

          return nunbounded;
        }

      private:
        const Aut aut;
        const int K;
    };
  }

  struct forward_saturation {
      template <typename Aut>
      static auto make (Aut aut, int K) {
        return detail::forward_saturation<Aut> (aut, K);
      }
  };
}
