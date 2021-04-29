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
      simd_array_backed_sum_ (size_t k) : k {k}, sum {0} {
        // assert ((k + traits::simd_size - 1) / traits::simd_size == nsimds);
        for (size_t i = 0; i < nsimds; ++i)
          ar[i] ^= ar[i];
      }

    public:
      simd_array_backed_sum_ (const std::vector<T>& v) : k {v.size ()} {
        sum = 0;
        for (auto&& c : v)
          sum += c;

        ssize_t i;
        for (i = 0; i < (ssize_t) traits::nsimds (k) - (k % simd_size ? 1 : 0); ++i)
          ar[i].copy_from (&v[i * simd_size], std::experimental::vector_aligned);
        if (k % simd_size != 0) {
          T tail[simd_size] = {0};
          std::copy (&v[i * simd_size], &v[i * simd_size] + (k % simd_size), tail);
          ar[i].copy_from (tail, std::experimental::vector_aligned);
          ++i;
        }
        assert (i > 0);
        for (; i < (ssize_t) nsimds; ++i) // This shouldn't happen if the vector is tight.
          ar[i] ^= ar[i];
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

      void to_vector (std::vector<char>& v) const {
        v.resize (nsimds * simd_size);

        for (size_t i = 0; i < nsimds; ++i)
          ar[i].copy_to (&v[i * simd_size], std::experimental::vector_aligned);
        // We don't resize v back; this could be confusing, but we'll see v
        // again and again, so want to be sure it's of sufficient size.
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
              auto diff = lhs.ar[up_to] - rhs.ar[up_to];
              bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
              bleq = bleq and (std::experimental::reduce (-diff, std::bit_or ()) >= 0);
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
              auto diff = lhs.ar[up_to] - rhs.ar[up_to];
              bgeq = bgeq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
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
              auto diff = rhs.ar[up_to] - lhs.ar[up_to];
              bleq = bleq and (std::experimental::reduce (diff, std::bit_or ()) >= 0);
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
        for (size_t i = 0; i < nsimds; ++i)
          if (not std::experimental::all_of (ar[i] == rhs.ar[i]))
            return false;
        return true;
      }

      bool operator!= (const self& rhs) const {
        if (sum != rhs.sum)
          return true;
        for (size_t i = 0; i < nsimds; ++i)
          if (not std::experimental::any_of (ar[i] != rhs.ar[i]))
            return true;
        return false;
      }

      self meet (const self& rhs) const {
#warning Overkill to call the whole constructor
        auto res = self (k);

        for (size_t i = 0; i < nsimds; ++i) {
          res.ar[i] = std::experimental::min (ar[i], rhs.ar[i]);
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
          os << (*this)[i] << " ";
        os << "}";
        return os;
      }

      // Should be used sparingly.
      int operator[] (size_t i) const {
        return ar[i / simd_size][i % simd_size];
      }

    private:
      std::array<typename traits::fssimd, nsimds> ar;
      const size_t k;
#warning also store max?
    public:
      int sum = 0;
  };

}

template <typename T, size_t nsimds>
inline
std::ostream& operator<<(std::ostream& os, const vectors::simd_array_backed_sum_<T, nsimds>& v)
{
  return v.print (os);
}
