#pragma once

#include "solver/aiger_abc.hh"
#include "solver/synthesis_rendezvous.hh"

#include <fstream>
#include <spot/twaalgos/aiger.hh>
#include <spanstream>
#include <string>
#include <utility>

namespace acacia::synthesis {
  inline bool write_controller (const std::string& filename, std::string aiger) {
    await_selection (EXIT_CODE_REAL);
#if ACACIA_HAVE_ABC
    if (aiger_abc.enabled) {
# if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      shrink_aiger (aiger, aiger_abc, test_before_aiger_abc);
# else
      shrink_aiger (aiger, aiger_abc);
# endif
    }
#endif
    std::ofstream synthesis_file (filename);
    synthesis_file << aiger;
    synthesis_file.close ();
    const bool success = bool (synthesis_file);
    if (success && current_slot)
      current_slot->controller_complete.store (true, std::memory_order_release);
    return success;
  }

  inline bool write_controller (const std::string& filename, spot::const_aig_ptr controller) {
    await_selection (EXIT_CODE_REAL);
    if (!controller)
      return false;
#if ACACIA_HAVE_ABC
    if (aiger_abc.enabled) {
      try {
        const auto payload_bytes = aiger_abc_payload_limit (aiger_abc_memory (aiger_abc));
        std::string buffer (payload_bytes, '\0');
        std::ospanstream encoded (buffer);
        spot::print_aiger (encoded, controller);
        if (encoded) {
          buffer.resize (encoded.span ().size ());
          return write_controller (filename, std::move (buffer));
        }
      } catch (...) {
        // Staging is optional; retain the controller for the streaming writer below.
      }
    }
#endif
    std::ofstream synthesis_file (filename);
    if (!synthesis_file)
      return false;
    spot::print_aiger (synthesis_file, std::move (controller));
    synthesis_file.close ();
    const bool success = bool (synthesis_file);
    if (success && current_slot)
      current_slot->controller_complete.store (true, std::memory_order_release);
    return success;
  }
}
