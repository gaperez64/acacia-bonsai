#pragma once

#include "configuration.hh"
#include <experimental/simd>

namespace utils {
  template <typename Elt>
  struct simd_traits {
#if SIMD_IS_MAX
      static constexpr auto simd_size =
        std::experimental::simd_abi::max_fixed_size<Elt>;
#else
      static constexpr auto simd_size =
        std::experimental::simd_size_v<Elt>;
#endif

      static constexpr auto nsimds (size_t k) {
        return (k + simd_size - 1) / simd_size;
      }

      static constexpr auto capacity_for (size_t nelts) {
        return nsimds (nelts) * simd_size;
      }

      static constexpr auto alignment () {
        return simd_size * sizeof (Elt);
      }

      using fssimd = std::experimental::fixed_size_simd<Elt, simd_size>;
  };
}
