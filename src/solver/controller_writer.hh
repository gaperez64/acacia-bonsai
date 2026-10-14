#pragma once

#include "solver/synthesis_rendezvous.hh"
#include <fstream>
#include <spot/twaalgos/aiger.hh>
#include <string>
#include <utility>

namespace acacia::synthesis {
  inline bool write_controller (const std::string& filename, spot::const_aig_ptr controller) {
    await_selection (EXIT_CODE_REAL);
    std::ofstream synthesis_file (filename);
    if (!synthesis_file || !controller)
      return false;
    spot::print_aiger (synthesis_file, std::move (controller));
    synthesis_file.close ();
    const bool success = bool (synthesis_file);
    if (success && current_slot)
      current_slot->controller_complete.store (true, std::memory_order_release);
    return success;
  }
}
