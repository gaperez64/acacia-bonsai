#pragma once

#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "utils/simd_traits.hh"

namespace vectors {
  template <typename T, size_t nsimds>
  class simd_array_backed_;

  template <typename T, size_t K>
  using simd_array_backed = simd_array_backed_<T, utils::simd_traits<T>::nsimds (K)>;

  template <typename T, size_t nsimds>
  class simd_array_backed_ {
      using self = simd_array_backed_<T, nsimds>;
      using traits = utils::simd_traits<T>;
      static const auto simd_size = traits::simd_size;

    public:
      simd_array_backed_ (size_t k) : k {k} {
        assert ((k + traits::simd_size - 1) / traits::simd_size == nsimds);
        ar[nsimds - 1] ^= ar[nsimds - 1];
      }

      simd_array_backed_ () = delete;
      simd_array_backed_ (const self& other) = delete;
      simd_array_backed_ (self&& other) = default;

      // explicit copy operator
      simd_array_backed_ copy () const {
        auto res = simd_array_backed_ (k);
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

}

template <typename T, size_t nsimds>
inline
std::ostream& operator<<(std::ostream& os, const vectors::simd_array_backed_<T, nsimds>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
