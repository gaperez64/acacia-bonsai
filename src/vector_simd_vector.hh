#pragma once
#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "vector_simd_traits.hh"

template <typename T>
class vector_simd_vector {
    using self = vector_simd_vector<T>;
    using traits = vector_simd_traits<T>;
    static const auto simd_size = traits::simd_size;

  public:
    vector_simd_vector (size_t k) : k {k},
                                    nsimds {traits::nsimds (k)},
                                    vec (nsimds) {
      vec.back () ^= vec.back ();
    }

    vector_simd_vector () = delete;
    vector_simd_vector (const self& other) : k {other.k}, nsimds {other.nsimds},
                                                           vec {other.vec} {}

    self& operator= (self&& other) {
      assert (other.k == k and other.nsimds == nsimds);
      vec = std::move (other.vec);
      return *this;
    }

    self& operator= (const self& other) = delete;

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

template <typename T>
inline
std::ostream& operator<<(std::ostream& os, const vector_simd_vector<T>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
