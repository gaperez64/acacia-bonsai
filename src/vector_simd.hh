#pragma once
#include <cassert>


#ifndef VECTOR_SIMD_SIZE
# define VECTOR_SIMD_SIZE 32
#endif

#include <experimental/simd>
#include <iostream>

using std::experimental::fixed_size_simd;
using fssimd = fixed_size_simd<int, VECTOR_SIMD_SIZE>;

class vector_simd : public fssimd {
  public:
    vector_simd (unsigned int k) : fssimd (), k {k} {
      assert (k <= VECTOR_SIMD_SIZE);
      *this ^= *this;
    }

    vector_simd () = delete;
    vector_simd (const vector_simd& other) : fssimd (other), k {other.k} {}
    vector_simd (const fssimd& other, unsigned int k) : fssimd (other), k {k} {}

    vector_simd& operator= (vector_simd&& other) {
      fssimd::operator= (std::move (other));
      return *this;
    }

    vector_simd& operator= (const vector_simd& other) = delete;

    class po_res : public fssimd {
      public:
        po_res (fssimd&& e) : fssimd (std::move (e)) {}

        inline bool leq () {
          // This tests that the MSB of each component of the difference is 0.
          return std::experimental::reduce (static_cast<fssimd> (*this), std::bit_or ()) >= 0;
        }

        inline bool geq () {
          // Similarly, but with the opposite of the difference.
          auto tmp = -*this;
          return std::experimental::reduce (tmp, std::bit_or ()) >= 0;
        }

        // inline bool eq () not implemented, as it is better to use ==.
    };

    inline auto partial_order (const vector_simd& rhs) const {
      return po_res (rhs - static_cast<fssimd> (*this));
    }

    bool operator== (const vector_simd& rhs) const {
      return std::experimental::all_of (static_cast<fssimd> (*this) == rhs);
    }

    bool operator!= (const vector_simd& rhs) const {
      return std::experimental::any_of (static_cast<fssimd> (*this) != rhs);
    }

    // Used by Sets, should be a total order.
    bool operator< (const vector_simd& rhs) const {
      const fssimd thisfssimd = static_cast<fssimd> (*this);
      auto lhs_lt_rhs = thisfssimd < rhs;
      auto rhs_lt_lhs = rhs < thisfssimd;
      return (find_first_set (lhs_lt_rhs) <
              find_first_set (rhs_lt_lhs));
    }

    vector_simd meet (const vector_simd& rhs) const {
      return vector_simd (std::experimental::min (*this, rhs), k);
    }

    auto size () const {
      return k;
    }

  private:
    const unsigned int k;
    friend std::ostream& operator<<(std::ostream& os, const vector_simd& v);
};

inline
std::ostream& operator<<(std::ostream& os, const vector_simd& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.k; ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
