#pragma once

#include <experimental/simd>

namespace utils {
  template <typename Elt>
  struct simd_traits {
      static constexpr auto simd_size = std::experimental::simd_size_v<Elt>;
      static constexpr auto max_fixed_size =
        std::experimental::simd_abi::max_fixed_size<Elt>;
      static constexpr auto nsimds (size_t k) {
        return (k + simd_size - 1) / simd_size;
      }
      using fssimd = std::experimental::fixed_size_simd<Elt, simd_size>;
      using maxfssimd =
        std::experimental::fixed_size_simd<Elt, max_fixed_size>;
  };
}
