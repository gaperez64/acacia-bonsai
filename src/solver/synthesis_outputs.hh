#pragma once

#include <unordered_set>

#include <bddx.h>
#include <spot/twaalgos/game.hh>
#include <stdexcept>
#include <string>
#include <vector>

namespace acacia::synthesis {
  inline void set_projected_outputs (const spot::twa_graph_ptr& machine, const bdd& outputs) {
    spot::set_synthesis_outputs (machine, bdd_existcomp (outputs, machine->ap_vars ()));
  }

  inline void validate_output_partition (
      const std::vector<spot::const_twa_graph_ptr>& machines,
      const std::vector<std::vector<std::string>>& output_groups) {
    const auto fail = [] {
      // Keep this check in release builds: bad metadata can silently corrupt +dc controllers.
      throw std::logic_error ("internal error: invalid synthesis output partition");
    };
    if (machines.size () != output_groups.size ())
      fail ();
    bdd seen = bddtrue;
    for (size_t i = 0; i < machines.size (); ++i) {
      const auto& machine = machines[i];
      if (!machine)
        fail ();
      const auto* outputs = machine->get_named_prop<bdd> ("synthesis-outputs");
      if (!outputs || *outputs != bdd_support (*outputs) ||
          *outputs != bdd_existcomp (*outputs, machine->ap_vars ()) ||
          bdd_existcomp (*outputs, seen) != bddtrue)
        fail ();
      const std::unordered_set<std::string> group (output_groups[i].begin (),
                                                   output_groups[i].end ());
      bdd expected = bddtrue;
      for (const auto& ap : machine->ap ())
        if (group.contains (ap.ap_name ()))
          expected &= bdd_ithvar (machine->get_dict ()->varnum (ap));
      if (*outputs != expected)
        fail ();
      seen &= *outputs;
    }
  }
}
