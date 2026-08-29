#pragma once

#include "solver/transition_payload.hh"

#include <bddx.h>

#include <utility>
#include <vector>

namespace ios_precomputers {
  struct delegate {
      template <typename Aut,
                typename TransSet = std::vector<acacia::transitions::element>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return [=] () { return std::pair (input_support, output_support); };
      }
  };
}
