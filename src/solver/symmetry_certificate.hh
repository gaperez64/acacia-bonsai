#pragma once

#include <string>
#include <utility>
#include <vector>

namespace symmetry {

  // Declaration provenance supplied by the native TLSF frontend when indexed
  // reduction syntax is present.  TLSF syntax does not prove symmetry: this
  // hint only selects otherwise-capped hypotheses, and structural automorphism
  // checking remains the final soundness gate.
  struct indexed_family_hint {
      bool is_input = false;
      long lo = 0;
      long hi = -1;
      std::vector<std::string> members;
  };

  // Objective-level evidence supplies candidates, never automaton certificates.
  // AP names survive worker I/O reversal; the detector checks the actual partition.
  struct ap_transposition_candidate {
      std::pair<long, long> indices;
      std::vector<std::pair<std::string, std::string>> image;
  };

  struct objective_symmetry_hint {
      std::vector<indexed_family_hint> families;
      std::vector<ap_transposition_candidate> generators;
  };

}  // namespace symmetry
