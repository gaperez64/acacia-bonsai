#pragma once

#include "configuration.hh"

namespace vectors {
  // This is the position at and after which all state counters are boolean.
  // Set to the vector size to disable this.
  static size_t bool_threshold = 0;

  // The boolean threshold may lay at an inconvenient position.  For instance,
  // it may be at 7, in which case a vector type that implements a split between
  // boolean and nonboolean may want to use 8 dimensions as nonbools, and the
  // rest as bool.  This is this threshold:
  static size_t bitset_threshold = 0;

  // Vectors implementing bin() should satisfy:
  //       if u.bin () < v.bin (), then u can't dominate v.
  // or equivalently:
  //       if u dominates v, then u.bin () >= v.bin ()
  template<class T, class = void>
  struct has_bin : std::false_type {};

  template <class T>
  struct has_bin<T, std::void_t<decltype (std::declval<T> ().bin ())>> : std::true_type {};

  template <template <typename T, auto...> typename T, typename Elt>
  struct traits {
      static constexpr auto capacity_for (size_t nelts) { return nelts; }
  };
}

#include "vectors/vector_backed.hh"
#include "vectors/array_backed.hh"
#include "vectors/array_ptr_backed.hh"
#include "vectors/array_backed_sum.hh"
#include "vectors/simd_vector_backed.hh"
#include "vectors/simd_array_backed.hh"
#include "vectors/simd_array_ptr_backed.hh"
#include "vectors/simd_array_backed_sum.hh"

#include "vectors/X_and_bitset.hh"
