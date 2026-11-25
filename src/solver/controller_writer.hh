#pragma once

#include "solver/aiger_abc.hh"
#include "solver/synthesis_rendezvous.hh"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <spot/twaalgos/aiger.hh>
#include <spanstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

namespace acacia::synthesis {
#if ACACIA_HAVE_ABC
  inline void shrink_controller (const std::string& filename, std::string aiger) {
    try {
      const auto target = std::filesystem::canonical (filename).string ();
      struct stat metadata;
      if (stat (target.c_str (), &metadata) != 0 || !S_ISREG (metadata.st_mode) ||
          metadata.st_nlink != 1)
        return;
      const auto original = aiger;
# if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
      shrink_aiger (aiger, aiger_abc, test_before_aiger_abc);
# else
      shrink_aiger (aiger, aiger_abc);
# endif
      if (aiger == original)
        return;
      auto temporary = target + ".abc-XXXXXX";
      const int fd = mkstemp (temporary.data ());
      if (fd < 0)
        return;
      if (FILE* output = fdopen (fd, "w")) {
        const bool written = std::fwrite (aiger.data (), 1, aiger.size (), output) == aiger.size ()
          && fchmod (fd, metadata.st_mode & 0777) == 0;
        if (std::fclose (output) == 0 && written)
          std::rename (temporary.c_str (), target.c_str ());
      } else
        close (fd);
      std::remove (temporary.c_str ());
    } catch (...) {
      // The closed Spot controller remains valid even if optional work fails or is killed.
    }
  }
#endif

  inline bool write_controller (const std::string& filename, std::string aiger) {
    await_selection (EXIT_CODE_REAL);
    std::ofstream synthesis_file (filename);
    synthesis_file << aiger;
    synthesis_file.close ();
    const bool success = bool (synthesis_file);
    if (success && current_slot)
      current_slot->controller_complete.store (true, std::memory_order_release);
#if ACACIA_HAVE_ABC
    if (success && aiger_abc.enabled)
      shrink_controller (filename, std::move (aiger));
#endif
    return success;
  }

  inline bool write_controller (const std::string& filename, spot::const_aig_ptr controller) {
    await_selection (EXIT_CODE_REAL);
    if (!controller)
      return false;
    std::ofstream synthesis_file (filename);
    if (!synthesis_file)
      return false;
    spot::print_aiger (synthesis_file, controller);
    synthesis_file.close ();
    const bool success = bool (synthesis_file);
    if (success && current_slot)
      current_slot->controller_complete.store (true, std::memory_order_release);
#if ACACIA_HAVE_ABC
    if (success && aiger_abc.enabled) {
      try {
        const auto payload_bytes = aiger_abc_payload_limit (aiger_abc_memory (aiger_abc));
        std::string buffer (payload_bytes, '\0');
        std::ospanstream encoded (buffer);
        spot::print_aiger (encoded, controller);
        if (encoded) {
          buffer.resize (encoded.span ().size ());
          shrink_controller (filename, std::move (buffer));
        }
      } catch (...) {
        // Staging is optional; the Spot controller is already on disk.
      }
    }
#endif
    return success;
  }
}
