#pragma once

namespace vectors {
  // This is the position at and after which all state counters are boolean.
  // Set to the vector size to disable this.
  static size_t bool_threshold = 0;


  // Vectors implementing bin() should satisfy:
  //       if u.bin () > v.bin (), then v can't dominate u.
  template<class T, class = void>
  struct has_bin : std::false_type {};

  template <class T>
  struct has_bin<T, std::void_t<decltype (std::declval<T> ().bin ())>> : std::true_type {};
}

#include "vectors/vector_backed.hh"
#include "vectors/array_backed.hh"
#include "vectors/array_backed_sum.hh"
#include "vectors/simd_vector_backed.hh"
#include "vectors/simd_array_backed.hh"
#include "vectors/simd_array_backed_sum.hh"

#include "vectors/X_and_bitset.hh"
