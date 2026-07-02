#pragma once

#include "utils/verbose.hh"

#include <cassert>
#include <spot/twa/acc.hh>

// So-called "Optimization 1" in ac+.
// A state is bounded if it cannot carry a counter value of at least k, i.e. it
// is NOT reachable from a Büchi state lying in a nontrivial SCC.
//
// The forward computation below is already EXACT for this definition: by
// pigeonhole, c[q] can exceed nb_accepting_states only if some path to q
// repeats an accepting state, i.e. q is reachable from an accepting state on a
// cycle (a nontrivial SCC). Verified equal to a spot::scc_info-based
// computation on the arbiter3..6 family (unbounded = 7/9/11/13 both ways), so a
// "backward" or SCC-based reformulation would mark the identical set and buy
// nothing (earlier TODOs to that effect dropped). Note this is distinct from
// the `elevator` aut-preprocessor, which collapses whole determined SCCs
// (safe/losing traps) rather than narrowing counter widths.

namespace boolean_states {
  namespace detail {
    template <typename Aut>
    class forward_saturation {
      public:
        forward_saturation (Aut aut, int K) : aut {aut}, K {K} {}

        size_t operator() () const {
          uint32_t nb_accepting_states = 0, nunbounded = 0;

          for (uint32_t src = 0; src < aut->num_states (); ++src)
            if (aut->state_is_accepting (src))
              nb_accepting_states++;

          auto c = std::vector<uint32_t> (aut->num_states ());
          if (aut->state_is_accepting (aut->get_init_state_number ()))
            c[aut->get_init_state_number ()] = 1;

          bool has_changed = true;

          while (has_changed) {
            has_changed = false;

            for (uint32_t src = 0; src < aut->num_states (); ++src) {
              uint32_t c_src_mod = std::min (nb_accepting_states + 1u,
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

          auto rename = std::vector<uint32_t> (aut->num_states ());

          uint32_t bounded = 0, unbounded = 0;
          for (uint32_t src = 0; src < aut->num_states (); ++src)
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

          verb_do (1, vout << "Bounded states: " << bounded
                           << " / "
                           /*   */
                           << aut->num_states ()
                           << " = "
                           /*   */
                           << (bounded * 100) / aut->num_states () << "%" << std::endl);

          // WARNING: Internal Spot
          auto& g = aut->get_graph ();
          g.rename_states_ (rename);
          aut->set_init_state (rename[aut->get_init_state_number ()]);
          g.sort_edges_ ();
          g.chain_edges_ ();
          aut->prop_universal (spot::trival::maybe ());

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
