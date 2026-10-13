#pragma once

#include <fstream>
#include <spot/twaalgos/aiger.hh>
#include <string>
#include <utility>

namespace acacia::synthesis {
  inline bool write_controller (const std::string& filename, spot::const_aig_ptr controller) {
    std::ofstream synthesis_file (filename);
    if (!synthesis_file || !controller)
      return false;
    spot::print_aiger (synthesis_file, std::move (controller));
    synthesis_file.close ();
    return bool (synthesis_file);
  }
}
