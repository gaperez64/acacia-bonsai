#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <vector>

namespace aut_preprocessors {

  struct future_visit_cap_report {
    std::vector<unsigned> caps;
    std::size_t states_at_k = 0;
    std::size_t finite_states = 0;
    std::size_t zero_states = 0;
    std::size_t counting_states = 0;
    std::size_t finite_counting_states = 0;
  };

  // Compute an upper bound on the number of accepting states that can still
  // be visited from each state.  An accepting state on a cycle makes the bound
  // K for that SCC and every SCC that can reach it.  All other SCCs form a DAG
  // for this purpose: non-accepting cycles add nothing, while a trivial
  // accepting SCC adds one to the largest successor bound.
  //
  // counting_states mirrors forward_saturation's full-counter band: states
  // reachable from an accepting cycle.  finite_counting_states is the subset
  // for which the future bound is below K, i.e. the potential three-band win.
  inline future_visit_cap_report
  future_visit_cap_census (const spot::const_twa_graph_ptr& aut, unsigned K) {
    assert (aut != nullptr);
    assert (K > 0);
    assert (aut->prop_state_acc ().is_true ());

    future_visit_cap_report report;
    report.caps.resize (aut->num_states ());
    if (aut->num_states () == 0)
      return report;

    spot::scc_info si {aut};
    const unsigned scc_count = si.scc_count ();
    std::vector<bool> accepting_cycle (scc_count, false);

    for (unsigned scc = 0; scc < scc_count; ++scc) {
      if (si.is_trivial (scc))
        continue;
      accepting_cycle[scc] = std::ranges::any_of (
          si.states_of (scc), [&] (unsigned state) {
            return aut->state_is_accepting (state);
          });
    }

    // Spot numbers SCCs in reverse topological order, so every successor has
    // already been evaluated when this loop reaches its predecessor.
    std::vector<unsigned> scc_caps (scc_count, 0);
    for (unsigned scc = 0; scc < scc_count; ++scc) {
      if (accepting_cycle[scc]) {
        scc_caps[scc] = K;
        continue;
      }

      unsigned cap = 0;
      for (unsigned successor : si.succ (scc))
        cap = std::max (cap, scc_caps[successor]);

      // A nontrivial SCC reaching this branch contains no accepting state.
      // A trivial SCC contains exactly one state and no self-loop.
      if (si.is_trivial (scc)
          and aut->state_is_accepting (si.one_state_of (scc))
          and cap < K)
        ++cap;
      scc_caps[scc] = cap;
    }

    // Mark the states that currently need full counters: those reachable from
    // an accepting cycle.  This is the direction used by forward_saturation.
    std::vector<bool> counting_scc (scc_count, false);
    std::vector<unsigned> worklist;
    worklist.reserve (scc_count);
    for (unsigned scc = 0; scc < scc_count; ++scc)
      if (accepting_cycle[scc]) {
        counting_scc[scc] = true;
        worklist.push_back (scc);
      }
    while (not worklist.empty ()) {
      const unsigned scc = worklist.back ();
      worklist.pop_back ();
      for (unsigned successor : si.succ (scc))
        if (not counting_scc[successor]) {
          counting_scc[successor] = true;
          worklist.push_back (successor);
        }
    }

    for (unsigned state = 0; state < aut->num_states (); ++state) {
      const unsigned scc = si.scc_of (state);
      if (scc == std::numeric_limits<unsigned>::max ())
        continue;

      const unsigned cap = scc_caps[scc];
      report.caps[state] = cap;
      if (cap == K)
        ++report.states_at_k;
      else if (cap == 0)
        ++report.zero_states;
      else
        ++report.finite_states;

      if (counting_scc[scc]) {
        ++report.counting_states;
        if (cap < K)
          ++report.finite_counting_states;
      }
    }
    return report;
  }

}  // namespace aut_preprocessors
