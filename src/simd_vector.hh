#pragma once
#include <cassert>


#ifndef SIMD_VECTOR_SIZE
# define SIMD_VECTOR_SIZE 16
#endif

#include <experimental/simd>
#include <iostream>

using std::experimental::fixed_size_simd;
using fssimd = fixed_size_simd<int, SIMD_VECTOR_SIZE>;

class simd_vector : public fssimd {
  public:
    simd_vector (unsigned int k) : fssimd (), k {k} {
      assert (k <= SIMD_VECTOR_SIZE);
      *this ^= *this;
    }

    simd_vector () = delete;
    simd_vector (const simd_vector& other) : fssimd (other), k {other.k} {}
    simd_vector (const fssimd& other, unsigned int k) : fssimd (other), k {k} {}

    bool partial_leq (const simd_vector& rhs) const {
      return std::experimental::all_of (static_cast<fssimd>(*this) <= rhs);
    }

    bool partial_lt (const simd_vector& rhs) const {
      return std::experimental::none_of (static_cast<fssimd> (*this) > rhs);
    }

    bool operator== (const simd_vector& rhs) const {
      return std::experimental::all_of (static_cast<fssimd> (*this) == rhs);
    }

    bool operator!= (const simd_vector& rhs) const {
      return std::experimental::any_of (static_cast<fssimd> (*this) != rhs);
    }

    // Used by Sets, should be a total order.
    bool operator< (const simd_vector& rhs) const {
      const fssimd thisfssimd = static_cast<fssimd> (*this);
      auto lhs_lt_rhs = thisfssimd < rhs;
      auto rhs_lt_lhs = rhs < thisfssimd;
      return (find_first_set (lhs_lt_rhs) <
              find_first_set (rhs_lt_lhs));
    }

    simd_vector meet (const simd_vector& rhs) const {
      return simd_vector (std::experimental::min (*this, rhs), k);
    }

  private:
    const unsigned int k;
    friend std::ostream& operator<<(std::ostream& os, const simd_vector& v);
};

inline std::ostream& operator<<(std::ostream& os, const simd_vector& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.k; ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
