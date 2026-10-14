// Adapted from vanessas-oink 872be945, src/solvers/strpm_simd.{hpp,cpp}
// (Strahler-tree progress measures by V. Flügel and G. A. Pérez), relicensed by its
// authors under this project's GPL-3.0.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
# include <emmintrin.h>
#endif

namespace acacia::parity::detail {
  // Eight unsigned 16-bit lanes, using baseline SSE2 or standard C++.
  // Bits remain eight-bit strings; 0xffff is reserved for Top levels.
  struct alignas (16) lanes {
      std::array<uint16_t, 8> data {};
      lanes () = default;
      lanes (uint16_t value) { data.fill (value); }
      uint16_t& operator[] (unsigned i) { return data[i]; }
      uint16_t operator[] (unsigned i) const { return data[i]; }
      void copy_from (const uint16_t* p) { std::copy_n (p, 8, data.begin ()); }
      void copy_to (uint16_t* p) const { std::copy_n (data.begin (), 8, p); }
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
      __m128i native () const {
        return _mm_load_si128 (reinterpret_cast<const __m128i*> (data.data ()));
      }
      static lanes from (__m128i value) {
        lanes result;
        _mm_store_si128 (reinterpret_cast<__m128i*> (result.data.data ()), value);
        return result;
      }
#endif
  };
  using lane_mask = lanes;

#define ACACIA_LANE_BINARY(OP, INTRINSIC)                     \
  inline lanes operator OP (const lanes& a, const lanes& b) { \
    ACACIA_LANE_BINARY_BODY (OP, INTRINSIC)                   \
  }
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
# define ACACIA_LANE_BINARY_BODY(OP, INTRINSIC) \
   return lanes::from (INTRINSIC (a.native (), b.native ()));
#else
# define ACACIA_LANE_BINARY_BODY(OP, INTRINSIC) \
   lanes result;                                \
   for (unsigned i = 0; i < 8; ++i)             \
     result[i] = a[i] OP b[i];                  \
   return result;
#endif
  ACACIA_LANE_BINARY (+, _mm_add_epi16)
  ACACIA_LANE_BINARY (-, _mm_sub_epi16)
  ACACIA_LANE_BINARY (&, _mm_and_si128)
  ACACIA_LANE_BINARY (|, _mm_or_si128)
  ACACIA_LANE_BINARY (^, _mm_xor_si128)
#undef ACACIA_LANE_BINARY_BODY
#undef ACACIA_LANE_BINARY

  inline lanes operator~(const lanes& a) { return a ^ lanes (0xffff); }
  inline lanes operator<< (const lanes& a, unsigned shift) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    return lanes::from (_mm_sll_epi16 (a.native (), _mm_cvtsi32_si128 (shift)));
#else
    lanes result;
    for (unsigned i = 0; i < 8; ++i)
      result[i] = a[i] << shift;
    return result;
#endif
  }
  inline lanes operator>> (const lanes& a, unsigned shift) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    return lanes::from (_mm_srl_epi16 (a.native (), _mm_cvtsi32_si128 (shift)));
#else
    lanes result;
    for (unsigned i = 0; i < 8; ++i)
      result[i] = a[i] >> shift;
    return result;
#endif
  }
  inline lane_mask operator== (const lanes& a, const lanes& b) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    return lanes::from (_mm_cmpeq_epi16 (a.native (), b.native ()));
#else
    lanes result;
    for (unsigned i = 0; i < 8; ++i)
      result[i] = a[i] == b[i] ? 0xffff : 0;
    return result;
#endif
  }
  inline lane_mask operator> (const lanes& a, const lanes& b) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    const auto sign = _mm_set1_epi16 (static_cast<short> (0x8000));
    return lanes::from (
        _mm_cmpgt_epi16 (_mm_xor_si128 (a.native (), sign), _mm_xor_si128 (b.native (), sign)));
#else
    lanes result;
    for (unsigned i = 0; i < 8; ++i)
      result[i] = a[i] > b[i] ? 0xffff : 0;
    return result;
#endif
  }
  inline lane_mask operator!(const lane_mask& a) { return ~a; }
  inline lane_mask operator< (const lanes& a, const lanes& b) { return b > a; }
  inline lane_mask operator<= (const lanes& a, const lanes& b) { return !(a > b); }
  inline lane_mask operator>= (const lanes& a, const lanes& b) { return !(a < b); }
  inline lane_mask operator&& (const lane_mask& a, const lane_mask& b) { return a & b; }
  inline lane_mask operator|| (const lane_mask& a, const lane_mask& b) { return a | b; }

  struct masked_lanes {
      const lane_mask& mask;
      lanes& value;
      void operator= (const lanes& replacement) { value = (mask & replacement) | (~mask & value); }
  };
  inline masked_lanes where (const lane_mask& mask, lanes& value) { return {mask, value}; }
  inline int find_first_set (const lane_mask& mask) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    unsigned bits = _mm_movemask_epi8 (mask.native ());
    return bits ? std::countr_zero (bits) / 2 : 8;
#else
    for (unsigned i = 0; i < 8; ++i)
      if (mask[i])
        return i;
    return 8;
#endif
  }
  inline int find_last_set (const lane_mask& mask) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    unsigned bits = _mm_movemask_epi8 (mask.native ());
    return bits ? (std::bit_width (bits) - 1) / 2 : -1;
#else
    for (int i = 7; i >= 0; --i)
      if (mask[i])
        return i;
    return -1;
#endif
  }
  inline lanes simd_popcount_u16x8 (lanes x) {
    x = x - ((x >> 1) & lanes (0x5555));
    x = (x & lanes (0x3333)) + ((x >> 2) & lanes (0x3333));
    x = (x + (x >> 4)) & lanes (0x0f0f);
    return (x + (x >> 8)) & lanes (0xff);
  }
  inline lanes simd_prefix_sum_inclusive_u16x8 (lanes x) {
#if defined(__SSE2__) && !defined(ACACIA_PARITY_PORTABLE)
    auto v = x.native ();
    v = _mm_add_epi16 (v, _mm_slli_si128 (v, 2));
    v = _mm_add_epi16 (v, _mm_slli_si128 (v, 4));
    v = _mm_add_epi16 (v, _mm_slli_si128 (v, 8));
    return lanes::from (v);
#else
    for (unsigned i = 1; i < 8; ++i)
      x[i] += x[i - 1];
    return x;
#endif
  }
  inline const lanes LANE_INDICES = [] {
    lanes result;
    for (unsigned i = 0; i < 8; ++i)
      result[i] = i;
    return result;
  }();
  inline constexpr uint16_t TOP_SENTINEL = 0xffff;

  struct node_measure {
      uint16_t bits[8] {};
      uint16_t masks[8] {};
      uint16_t levels[8] {};
      uint8_t nlanes = 0;
  };

  class lifting {
    public:
      int k, t, h;
      lanes tmp_bits, tmp_masks, tmp_levels;
      uint8_t tmp_nlanes = 0;
      lanes best_bits, best_masks, best_levels;
      uint8_t best_nlanes = 0;

      void to_tmp (const node_measure& pm) {
        tmp_bits.copy_from (pm.bits);
        tmp_masks.copy_from (pm.masks);
        tmp_levels.copy_from (pm.levels);
        tmp_nlanes = pm.nlanes;
      }
      void from_tmp (node_measure& pm) const {
        tmp_bits.copy_to (pm.bits);
        tmp_masks.copy_to (pm.masks);
        tmp_levels.copy_to (pm.levels);
        pm.nlanes = tmp_nlanes;
      }
      void to_best (const node_measure& pm) {
        best_bits.copy_from (pm.bits);
        best_masks.copy_from (pm.masks);
        best_levels.copy_from (pm.levels);
        best_nlanes = pm.nlanes;
      }
      void from_best (node_measure& pm) const {
        best_bits.copy_to (pm.bits);
        best_masks.copy_to (pm.masks);
        best_levels.copy_to (pm.levels);
        pm.nlanes = best_nlanes;
      }
      void tmp_to_best () {
        best_bits = tmp_bits;
        best_masks = tmp_masks;
        best_levels = tmp_levels;
        best_nlanes = tmp_nlanes;
      }
      void fill_inactive_tmp () {
        lane_mask inactive = LANE_INDICES >= lanes (tmp_nlanes);
        where (inactive, tmp_bits) = lanes (0);
        where (inactive, tmp_masks) = lanes (0);
      }
      void prog_tmp (int pindex, int height);
      int compare (int pindex);
  };
}
