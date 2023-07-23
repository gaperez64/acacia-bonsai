#pragma once

namespace ios_precomputers {
  struct delegate {
    static const bool supports_invariant = false;

      template <typename Aut, typename TransSet = std::vector<std::pair<int, int>>>
      static auto make (Aut aut,
                        bdd input_support, bdd output_support) {
        return [&] () {
          return std::pair (input_support, output_support);
        };
      }
  };
}
