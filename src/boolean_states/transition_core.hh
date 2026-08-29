#pragma once

#include "solver/acceptance_core.hh"
#include "utils/verbose.hh"

#include <cstdint>
#include <spot/twa/acc.hh>
#include <vector>

// Transition-acceptance counterpart of `forward_saturation`.
//
// `forward_saturation` decides which states can carry an unbounded acceptance
// count by asking `aut->state_is_accepting`, which only exists because Acacia
// requests state-based Büchi from Spot.  Under transition acceptance the same
// question is answered by the SCC census in `solver/acceptance_core.hh`: a
// state can count without bound exactly when it is reachable from an SCC whose
// internal transitions cover every acceptance set.
//
// The two obligations of this pass are the ones `forward_saturation` also
// discharges, and both are load-bearing:
//
//   1. Order states so the counting core occupies [0, threshold) and the rest
//      follows.  `k_bounded_safety_aut` splits its vectors at
//      `posets::vectors::bool_threshold` and would otherwise mix the two.
//
//   2. Guarantee the Boolean tail never receives an increment.  A Boolean
//      coordinate's safe value is 0 (`k_bounded_safety_aut.hh:92`) and it is
//      reset to 0 on every `k` step, so a tail state that could be incremented
//      would be reported unsafe spuriously.  `forward_saturation` achieves this
//      by clearing acceptance on bounded states' outgoing edges, which under
//      state acceptance is the same as making those states non-accepting.  The
//      transition-based analogue clears the mark on every edge *entering* a
//      tail state, because `actioners::standard` attributes an edge's increment
//      to its destination.
//
// Both are safe for the same reason as in the state-based pass: a state outside
// the core cannot be reached from any accepting cycle, so no run accumulates an
// unbounded count there, and collapsing it to a Boolean loses nothing the
// k-bounded game can observe.

namespace boolean_states {
  namespace detail {
    template <typename Aut>
    class transition_core {
      public:
        transition_core (Aut aut, int K) : aut {aut}, K {K} {}

        size_t operator() () const {
          const auto census = acacia::acceptance_core::compute (aut, true);
          const uint32_t states = aut->num_states ();
          if (states == 0)
            return 0;

          const auto& counting = census.in_global_core;
          const uint32_t core = (uint32_t) census.global_core;

          // Obligation 2: no edge may increment a Boolean-tail coordinate.
          for (uint32_t p = 0; p < states; ++p)
            for (auto& e : aut->out (p))
              if (not counting[e.dst])
                e.acc = spot::acc_cond::mark_t {};

          // Obligation 1: counting states first, tail after, order otherwise
          // preserved so the layout stays reproducible.
          auto rename = std::vector<uint32_t> (states);
          uint32_t counting_seen = 0, tail_seen = 0;
          for (uint32_t p = 0; p < states; ++p)
            if (counting[p])
              rename[p] = counting_seen++;
            else
              rename[p] = core + tail_seen++;

          verb_do (1, vout << "Boolean states: " << tail_seen << " / " << states << " = "
                           << (tail_seen * 100) / states << "%" << std::endl);

          auto& g = aut->get_graph ();
          g.rename_states_ (rename);
          aut->set_init_state (rename[aut->get_init_state_number ()]);
          g.sort_edges_ ();
          g.chain_edges_ ();
          aut->prop_universal (spot::trival::maybe ());

          return core;
        }

      private:
        const Aut aut;
        const int K;
    };
  }

  struct transition_core {
      template <typename Aut>
      static auto make (Aut aut, int K) {
        return detail::transition_core<Aut> (aut, K);
      }
  };
}
