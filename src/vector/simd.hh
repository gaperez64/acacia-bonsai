#pragma once

#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "vector/simd_traits.hh"

// Biggest SIMD vector natively implemented by the library.
namespace vector {
  template <typename T>
  class simd : public simd_traits<T>::maxfssimd {
      using self = simd<T>;
      static const auto max_simd_size = simd_traits<T>::max_fixed_size;
      using fssimd = typename simd_traits<T>::maxfssimd;
    public:
      simd (size_t k) : fssimd (), k {k} {
        assert (k <= max_simd_size);
        *this ^= *this;
      }

      simd () = delete;

    private:
      simd (const self& other) = default;
      simd (const fssimd& other, size_t k) : fssimd (other), k {k} {}

    public:
      simd (self&& other) = default;

      self& operator= (self&& other) {
        fssimd::operator= (std::move (other));
        return *this;
      }

      self copy () const {
        return *this;
      }

      self& operator= (const self& other) = delete;

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

      inline auto partial_order (const self& rhs) const {
        return po_res (rhs - static_cast<fssimd> (*this));
      }

      bool operator== (const self& rhs) const {
        return std::experimental::all_of (static_cast<fssimd> (*this) == rhs);
      }

      bool operator!= (const self& rhs) const {
        return std::experimental::any_of (static_cast<fssimd> (*this) != rhs);
      }

      // Used by Sets, should be a total order.
      bool operator< (const self& rhs) const {
        const fssimd thisfssimd = static_cast<fssimd> (*this);
        auto lhs_lt_rhs = thisfssimd < rhs;
        auto rhs_lt_lhs = rhs < thisfssimd;
        return (find_first_set (lhs_lt_rhs) <
                find_first_set (rhs_lt_lhs));
      }

      self meet (const self& rhs) const {
        return self (std::experimental::min (*this, rhs), k);
      }

      auto size () const {
        return k;
      }

    private:
      const size_t k;
  };
}

template <typename T>
inline
std::ostream& operator<<(std::ostream& os, const vector::simd<T>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.k; ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
