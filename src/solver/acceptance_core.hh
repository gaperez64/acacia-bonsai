#pragma once

#include <cstddef>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <vector>

namespace acacia::acceptance_core {

  struct census {
      size_t local_core = 0;
      size_t global_core = 0;
      size_t accepting_sccs = 0;
      size_t states = 0;
      /// in_global_core[q] iff state q is reachable from an accepting SCC,
      /// i.e. iff q can carry an unbounded acceptance count.  Empty unless
      /// compute() was asked for it.
      std::vector<bool> in_global_core;
  };

  /// \param want_membership also fill census::in_global_core, which the
  ///        transition-based booleanization needs to order states.
  inline census compute (const spot::const_twa_graph_ptr& aut,
                         bool want_membership = false) {
    census result;
    if (not aut)
      return result;

    result.states = aut->num_states ();
    if (want_membership)
      result.in_global_core.assign (result.states, false);
    if (result.states == 0 or aut->num_sets () == 0)
      return result;

    const auto options = spot::scc_info_options::ALL |
                         spot::scc_info_options::PROCESS_UNREACHABLE_STATES;
    const spot::scc_info scc {aut, options};
    const spot::acc_cond::mark_t all_sets = aut->acc ().all_sets ();
    std::vector<bool> in_core (scc.scc_count (), false);

    for (unsigned number = 0; number < scc.scc_count (); ++number) {
      bool has_internal_transition = false;
      spot::acc_cond::mark_t internal_marks {};
      for (const auto& edge : scc.inner_edges_of (number)) {
        has_internal_transition = true;
        internal_marks |= edge.acc;
      }
      if (has_internal_transition and
          (internal_marks & all_sets) == all_sets) {
        in_core[number] = true;
        ++result.accepting_sccs;
        result.local_core += scc.states_of (number).size ();
      }
    }

    std::vector<unsigned> worklist;
    worklist.reserve (scc.scc_count ());
    for (unsigned number = 0; number < scc.scc_count (); ++number)
      if (in_core[number])
        worklist.push_back (number);

    while (not worklist.empty ()) {
      const unsigned number = worklist.back ();
      worklist.pop_back ();
      for (unsigned successor : scc.succ (number))
        if (not in_core[successor]) {
          in_core[successor] = true;
          worklist.push_back (successor);
        }
    }

    for (unsigned number = 0; number < scc.scc_count (); ++number)
      if (in_core[number]) {
        result.global_core += scc.states_of (number).size ();
        if (want_membership)
          for (unsigned state : scc.states_of (number))
            result.in_global_core[state] = true;
      }
    return result;
  }

}  // namespace acacia::acceptance_core
