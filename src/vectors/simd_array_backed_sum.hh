#pragma once

#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "utils/simd_traits.hh"

namespace vectors {
  template <typename T, size_t nsimds>
  class simd_array_backed_sum_;

  template <typename T, size_t K>
  using simd_array_backed_sum = simd_array_backed_sum_<T, utils::simd_traits<T>::nsimds (K)>;

  template <typename T, size_t nsimds>
  class simd_array_backed_sum_ {
      using self = simd_array_backed_sum_<T, nsimds>;
      using traits = utils::simd_traits<T>;
      static const auto simd_size = traits::simd_size;

    public:
      using value_type = T;

    private:
      simd_array_backed_sum_ (size_t k) : k {k}, sum {0} { }

    public:
      simd_array_backed_sum_ (std::span<const T> v) : k {v.size ()} {
        sum = 0;
        for (auto&& c : v)
          sum += c;
        ar.back () ^= ar.back ();
        // Trust memcpy to DTRT.
        std::memcpy ((char*) ar.data (), (char*) v.data (), v.size ());
      }

      simd_array_backed_sum_ () = delete;
      simd_array_backed_sum_ (const self& other) = delete;
      simd_array_backed_sum_ (self&& other) = default;

      // explicit copy operator
      simd_array_backed_sum_ copy () const {
        auto res = simd_array_backed_sum_ (k);
        res.ar = ar;
        res.sum = sum;
        return res;
      }

      self& operator= (self&& other) {
        ar = std::move (other.ar);
        sum = other.sum;
        return *this;
      }

      self& operator= (const self& other) = delete;

      static constexpr size_t capacity_for (size_t elts) {
        return nsimds * simd_size;
      }

      void to_vector (std::span<char> v) const {
        for (size_t i = 0; i < nsimds; ++i)
          ar[i].copy_to (&v[i * simd_size], std::experimental::vector_aligned);
      }

      class po_res {
        public:
          po_res (const self& lhs, const self& rhs) : lhs {lhs}, rhs {rhs} {
            bgeq = (lhs.sum >= rhs.sum);
            if (not bgeq)
              has_bgeq = true;

            bleq = (lhs.sum <= rhs.sum);
            if (not bleq)
              has_bleq = true;

            if (has_bgeq or has_bleq)
              return;

            for (up_to = 0; up_to < nsimds; ++up_to) {
              //auto diff = lhs.ar[up_to] - rhs.ar[up_to];
              //bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
              //bleq = bleq and (std::experimental::reduce (-diff, std::bit_or ()) >= 0);
	      bgeq = bgeq and (std::experimental::all_of (lhs.ar[up_to] >= rhs.ar[up_to]));
	      bleq = bleq and (std::experimental::all_of (lhs.ar[up_to] <= rhs.ar[up_to]));
              if (not bgeq)
                has_bgeq = true;
              if (not bleq)
                has_bleq = true;
              if (has_bgeq or has_bgeq)
                return;
            }
            has_bgeq = true;
            has_bleq = true;
          }

          inline bool geq () {
            if (has_bgeq)
              return bgeq;
            assert (has_bleq);
            has_bgeq = true;
            for (; up_to < nsimds; ++up_to) {
              //auto diff = lhs.ar[up_to] - rhs.ar[up_to];
	      bgeq = bgeq and (std::experimental::all_of (lhs.ar[up_to] >= rhs.ar[up_to]));
              //bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
              if (not bgeq)
                break;
            }
            return bgeq;
          }

          inline bool leq () {
            if (has_bleq)
              return bleq;
            assert (has_bgeq);
            has_bleq = true;
            for (; up_to < nsimds; ++up_to) {
              bleq = bleq and (std::experimental::all_of (lhs.ar[up_to] <= rhs.ar[up_to]));
	      //auto diff = rhs.ar[up_to] - lhs.ar[up_to];
              //bleq = bleq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
              if (not bleq)
                break;
            }
            return bleq;
          }

        private:
          const self& lhs;
          const self& rhs;
          bool bgeq, bleq;
          bool has_bgeq = false,
            has_bleq = false;
          size_t up_to = 0;
      };

      inline auto partial_order (const self& rhs) const {
        return po_res (*this, rhs);
      }

      // Used by Sets, should be a total order.  Do not use.
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

      bool operator== (const self& rhs) const {
        if (sum != rhs.sum)
          return false;
        // Trust memcmp to DTRT
        return std::memcmp ((char*) rhs.ar.data (), (char*) ar.data (), nsimds * simd_size) == 0;
      }

      bool operator!= (const self& rhs) const {
        if (sum != rhs.sum)
          return true;
        // Trust memcmp to DTRT
        return std::memcmp ((char*) rhs.ar.data (), (char*) ar.data (), nsimds * simd_size) != 0;
      }

      self meet (const self& rhs) const {
        auto res = self (k);

        for (size_t i = 0; i < nsimds; ++i) {
          res.ar[i] = std::experimental::min (ar[i], rhs.ar[i]);
          // This should NOT be used since this can lead to overflows over char
          //   res.sum += std::experimental::reduce (res.ar[i]);
          // instead, we manually loop through:
          for (size_t j = 0; j < simd_size; ++j)
            res.sum += res.ar[i][j];
        }

        return res;
      }

      auto size () const {
        return k;
      }

      auto& print (std::ostream& os) const
      {
        os << "{ ";
        for (size_t i = 0; i < k; ++i)
          os << (int) (*this)[i] << " ";
        os << "}";
        return os;
      }

      // Should be used sparingly.
      int operator[] (size_t i) const {
        return ar[i / simd_size][i % simd_size];
      }

      auto bin () const {
        return (sum + k) / k;
      }

    private:
      std::array<typename traits::fssimd, nsimds> ar;
      const size_t k;
      int sum = 0;
  };

  template <typename T>
  struct traits<simd_array_backed_sum_, T> {
      static constexpr auto capacity_for (size_t elts) {
        return utils::simd_traits<T>::capacity_for (elts);
      }
  };

}

template <typename T, size_t nsimds>
inline
std::ostream& operator<<(std::ostream& os, const vectors::simd_array_backed_sum_<T, nsimds>& v)
{
  return v.print (os);
}
