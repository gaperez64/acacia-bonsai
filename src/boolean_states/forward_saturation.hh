#pragma once

#include "utils/verbose.hh"

#include <algorithm>
#include <cassert>
#include <spot/twa/acc.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <vector>

// So-called "Optimization 1" in ac+.
// States reachable from a Büchi state on a cycle saturate their counters.
// Compute this over the SCC DAG instead of repeating whole-graph scans.
// Process unreachable states too, as the original counter propagation did.
// Preserve its extra initial-state count as well: an accepting initial state
// can saturate descendants even without an accepting cycle.

namespace boolean_states {
  namespace detail {
    inline std::vector<bool> unbounded_states (const spot::const_twa_graph_ptr& aut) {
      std::vector<bool> marked (aut->num_states (), false);
      if (marked.empty ())
        return marked;

      // Spot ignores false guards; counter propagation counted every stored edge.
      spot::const_twa_graph_ptr graph = aut;
      const auto edges = aut->edges ();
      if (std::any_of (edges.begin (), edges.end (),
                       [] (const auto& e) { return e.cond == bddfalse; })) {
        auto structural = spot::make_twa_graph (aut, spot::twa::prop_set::all ());
        for (auto& e : structural->edges ())
          e.cond = bddtrue;
        graph = structural;
      }
      const spot::scc_info scc {graph, spot::scc_info_options::TRACK_SUCCS |
                                      spot::scc_info_options::PROCESS_UNREACHABLE_STATES};
      std::vector<unsigned> accepting (scc.scc_count ()), counters (scc.scc_count ());
      unsigned nb_accepting_states = 0;
      for (unsigned src = 0; src < aut->num_states (); ++src)
        if (aut->state_is_accepting (src)) {
          ++accepting[scc.scc_of (src)];
          ++nb_accepting_states;
        }
      if (aut->state_is_accepting (aut->get_init_state_number ()))
        counters[scc.scc_of (aut->get_init_state_number ())] = 1;

      // SCC numbers are reverse topological. Accepting cycles saturate; other
      // components contribute at most one accepting state along a path.
      for (unsigned component = scc.scc_count (); component-- > 0;) {
        if (accepting[component] and not scc.is_trivial (component))
          counters[component] = nb_accepting_states + 1;
        const unsigned next = std::min (nb_accepting_states + 1,
                                         counters[component] + accepting[component]);
        for (unsigned successor : scc.succ (component))
          counters[successor] = std::max (counters[successor], next);
      }
      for (unsigned src = 0; src < aut->num_states (); ++src)
        marked[src] = counters[scc.scc_of (src)] > nb_accepting_states;
      return marked;
    }

    template <typename Aut>
    class forward_saturation {
      public:
        forward_saturation (Aut aut, int K) : aut {aut}, K {K} {}

        size_t operator() () const {
          if (aut->num_states () == 0)
            return 0;
          const auto marked = unbounded_states (aut);
          const uint32_t nunbounded = std::count (marked.begin (), marked.end (), true);

          auto rename = std::vector<uint32_t> (aut->num_states ());

          uint32_t bounded = 0, unbounded = 0;
          for (uint32_t src = 0; src < aut->num_states (); ++src)
            if (marked[src])
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
