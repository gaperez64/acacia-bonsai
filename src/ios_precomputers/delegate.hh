#pragma once

namespace ios_precomputers {
  struct delegate {
      template <typename Aut, typename TransSet = std::vector<std::pair<int, int>>>
      static auto make (Aut aut,
                        bdd input_support, bdd output_support, int verbose) {
        return [&] () {
          return std::pair (input_support, output_support);
        };
      }
  };
}
