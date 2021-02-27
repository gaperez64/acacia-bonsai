#pragma once

#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "vector/simd_traits.hh"

template <typename T, size_t nsimds>
class vector_simd_array_;

template <typename T, size_t K>
using vector_simd_array = vector_simd_array_<T, vector_simd_traits<T>::nsimds (K)>;

template <typename T, size_t nsimds>
class vector_simd_array_ {
    using self = vector_simd_array_<T, nsimds>;
    using traits = vector_simd_traits<T>;
    static const auto simd_size = traits::simd_size;

  public:
    vector_simd_array_ (size_t k) : k {k} {
      assert ((k + traits::simd_size - 1) / traits::simd_size == nsimds);
      ar[nsimds - 1] ^= ar[nsimds - 1];
    }

    vector_simd_array_ () = delete;
    vector_simd_array_ (const self& other) = delete;
    vector_simd_array_ (self&& other) = default;

    // explicit copy operator
    vector_simd_array_ copy () const {
      auto res = vector_simd_array_ (k);
      res.ar = ar;
      return res;
    }

    self& operator= (self&& other) {
      ar = std::move (other.ar);
      return *this;
    }

    self& operator= (const self& other) = delete;

    class po_res {
      public:
        po_res (const self& lhs, const self& rhs) {
          bgeq = true;
          bleq = true;
          for (size_t i = 0; i < nsimds; ++i) {
            auto diff = lhs.ar[i] - rhs.ar[i];
            if (bgeq)
              bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
            if (bleq)
              bleq = bleq and (std::experimental::reduce (-diff, std::bit_or ()) >= 0);
            if (not bgeq and not bleq)
              break;
          }
        }

        inline bool geq () {
          return bgeq;
        }

        inline bool leq () {
          return bleq;
        }
      private:
        bool bgeq, bleq;
    };

    inline auto partial_order (const self& rhs) const {
      return po_res (*this, rhs);
    }

    bool operator== (const self& rhs) const {
      for (size_t i = 0; i < nsimds; ++i)
        if (not std::experimental::all_of (ar[i] == rhs.ar[i]))
          return false;
      return true;
    }

    bool operator!= (const self& rhs) const {
      for (size_t i = 0; i < nsimds; ++i)
        if (not std::experimental::any_of (ar[i] != rhs.ar[i]))
          return true;
      return false;
    }

    // Used by Sets, should be a total order.
    bool operator< (const self& rhs) const {
      for (size_t i = 0; i < nsimds; ++i) {
        auto lhs_lt_rhs = ar[i] < rhs.ar[i];
        auto rhs_lt_lhs = rhs.ar[i] < ar[i];
        auto p1 = find_first_set (lhs_lt_rhs);
        auto p2 = find_first_set (rhs_lt_lhs);
        if (p1 == p2)
          continue;
        return (p1 < p2);
      }
      return false;
    }

    int operator[] (size_t i) const {
      return ar[i / simd_size][i % simd_size];
    }

    typename traits::fssimd::reference operator[] (size_t i) {
      return ar[i / simd_size][i % simd_size];
    }

    self meet (const self& rhs) const {
      auto res = self (k);

      for (size_t i = 0; i < nsimds; ++i)
        res.ar[i] = std::experimental::min (ar[i], rhs.ar[i]);
      return res;
    }

    auto size () const {
      return k;
    }

  private:
    std::array<typename traits::fssimd, nsimds> ar;
    const size_t k;
};

template <typename T, size_t nsimds>
inline
std::ostream& operator<<(std::ostream& os, const vector_simd_array_<T, nsimds>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
