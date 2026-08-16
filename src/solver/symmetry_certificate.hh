#pragma once

#include <string>
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

}  // namespace symmetry
