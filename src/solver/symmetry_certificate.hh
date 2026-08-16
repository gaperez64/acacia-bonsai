#pragma once

#include <string>
#include <vector>

namespace symmetry {

  // Syntax-derived indexed-family evidence supplied by the native TLSF
  // frontend.  Structural automorphism checking remains the final soundness
  // gate; this certificate only selects otherwise-capped hypotheses.
  struct indexed_family_certificate {
      bool is_input = false;
      long lo = 0;
      long hi = -1;
      std::vector<std::string> members;
  };

}  // namespace symmetry
