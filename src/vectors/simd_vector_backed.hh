#pragma once
#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "utils/simd_traits.hh"

namespace vectors {
  template <typename T>
  class simd_vector_backed {
      using self = simd_vector_backed<T>;
      using traits = utils::simd_traits<T>;
      static const auto simd_size = traits::simd_size;

    public:
      using value_type = T;

    private:
      simd_vector_backed (size_t k) : k {k},
                                      nsimds {traits::nsimds (k)},
                                      vec (nsimds) {
        vec.back () ^= vec.back ();
      }
    public:
      simd_vector_backed (const std::vector<T>& v) : simd_vector_backed (v.size ()) {
        ssize_t i;
        for (i = 0; i < (ssize_t) traits::nsimds (k) - (k % simd_size ? 1 : 0); ++i)
          vec[i].copy_from (&v[i * simd_size], std::experimental::vector_aligned);
        if (k % simd_size != 0) {
          T tail[simd_size] = {0};
          std::copy (&v[i * simd_size], &v[i * simd_size] + (k % simd_size), tail);
          vec[i].copy_from (tail, std::experimental::vector_aligned);
          ++i;
        }
        assert (i > 0);
        for (; i < (ssize_t) nsimds; ++i) // This shouldn't happen if the vector is tight.
          vec[i] ^= vec[i];
      }


      simd_vector_backed () = delete;
      simd_vector_backed (const self& other) = delete;
      simd_vector_backed (self&& other) = default;

      // explicit copy operator
      self copy () const {
        auto res = self (k);
        res.vec = vec;
        return res;
      }

      self& operator= (self&& other) {
        assert (other.k == k and other.nsimds == nsimds);
        vec = std::move (other.vec);
        return *this;
      }

      self& operator= (const self& other) = delete;

      void to_vector (std::vector<T>& v) const {
        v.resize (nsimds * simd_size);
        for (size_t i = 0; i < nsimds; ++i)
          vec[i].copy_to (&v[i * simd_size], std::experimental::vector_aligned);
      }

      class po_res {
        public:
          po_res (const self& lhs, const self& rhs) {
            bgeq = true;
            bleq = true;
            for (size_t i = 0; i < lhs.nsimds; ++i) {
              auto diff = lhs.vec[i] - rhs.vec[i];
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

      // Alternative implementation that stores the pairwise difference but does
      // not reduce each of them.  Actually slower, possibly because the other
      // implementation does not go through the whole vector if it realizes the
      // vectors are incomparable.
      class po_res_ {
        public:
          po_res_ (const self& lhs, const self& rhs) :
            diffs (lhs.nsimds) {
            for (size_t i = 0; i < lhs.nsimds; ++i)
              diffs[i] = lhs.vec[i] - rhs.vec[i];
          }

          inline bool geq () const {
            for (size_t i = 0; i < diffs.size (); ++i)
              if (not (std::experimental::reduce (diffs[i], std::bit_or ()) >= 0))
                return false;
            return true;
          }

          inline bool leq () const {
            for (size_t i = 0; i < diffs.size (); ++i)
              if (not (std::experimental::reduce (-diffs[i], std::bit_or ()) >= 0))
                return false;
            return true;
          }
        private:
          std::vector<typename traits::fssimd> diffs;
      };


      inline auto partial_order (const self& rhs) const {
        return po_res_ (*this, rhs);
      }

      bool operator== (const self& rhs) const {
        for (size_t i = 0; i < nsimds; ++i)
          if (not std::experimental::all_of (vec[i] == rhs.vec[i]))
            return false;
        return true;
      }

      bool operator!= (const self& rhs) const {
        for (size_t i = 0; i < nsimds; ++i)
          if (not std::experimental::any_of (vec[i] != rhs.vec[i]))
            return true;
        return false;
      }

      // Used by Sets, should be a total order.
      bool operator< (const self& rhs) const {
        for (size_t i = 0; i < nsimds; ++i) {
          auto lhs_lt_rhs = vec[i] < rhs.vec[i];
          auto rhs_lt_lhs = rhs.vec[i] < vec[i];
          auto p1 = find_first_set (lhs_lt_rhs);
          auto p2 = find_first_set (rhs_lt_lhs);
          if (p1 == p2)
            continue;
          return (p1 < p2);
        }
        return false;
      }

      T operator[] (size_t i) const {
        return vec[i / simd_size][i % simd_size];
      }

      typename traits::fssimd::reference operator[] (size_t i) {
        return vec[i / simd_size][i % simd_size];
      }


      self meet (const self& rhs) const {
        auto res = self (k);

        for (size_t i = 0; i < nsimds; ++i)
          res.vec[i] = std::experimental::min (vec[i], rhs.vec[i]);
        return res;
      }

      auto size () const {
        return k;
      }

    private:
      const size_t k, nsimds;
      std::vector<typename traits::fssimd> vec;
  };
}

template <typename T>
inline
std::ostream& operator<<(std::ostream& os, const vectors::simd_vector_backed<T>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << (int) v[i] << " ";
  os << "}";
  return os;
}
