#pragma once

// https://stackoverflow.com/questions/8456236/how-is-a-vectors-data-aligned

#include <vector>
#include <boost/align/aligned_allocator.hpp>
#include <utils/simd_traits.hh>

namespace utils {
  template <typename T>
  using vector_mm = std::vector<T, boost::alignment::aligned_allocator<T, simd_traits<T>::alignment ()>>;
}
