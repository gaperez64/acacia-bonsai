#pragma once
#include <cassert>


#ifndef VECTOR_SIMD_SIZE
# define VECTOR_SIMD_SIZE 16
#endif

#include <experimental/simd>
#include <iostream>

using std::experimental::fixed_size_simd;
using fssimd = fixed_size_simd<int, VECTOR_SIMD_SIZE>;

class vector_simd_vector {
  public:
    vector_simd_vector (unsigned int k) : k {k},
                                          nsimds {(k + VECTOR_SIMD_SIZE - 1) / VECTOR_SIMD_SIZE},
                                          vec (nsimds) {
      vec.back () ^= vec.back ();
    }

    vector_simd_vector () = delete;
    vector_simd_vector (const vector_simd_vector& other) : k {other.k}, nsimds {other.nsimds},
                                                           vec {other.vec} {}

    vector_simd_vector& operator= (vector_simd_vector&& other) {
      assert (other.k == k and other.nsimds == nsimds);
      vec = std::move (other.vec);
      return *this;
    }

    vector_simd_vector& operator= (const vector_simd_vector& other) = delete;

    class po_res {
      public:
        po_res (const vector_simd_vector& lhs, const vector_simd_vector& rhs) {
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
        po_res_ (const vector_simd_vector& lhs, const vector_simd_vector& rhs) :
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
        std::vector<fssimd> diffs;
    };


    inline auto partial_order (const vector_simd_vector& rhs) const {
      return po_res_ (*this, rhs);
    }

    bool operator== (const vector_simd_vector& rhs) const {
      for (size_t i = 0; i < nsimds; ++i)
        if (not std::experimental::all_of (vec[i] == rhs.vec[i]))
          return false;
      return true;
    }

    bool operator!= (const vector_simd_vector& rhs) const {
      for (size_t i = 0; i < nsimds; ++i)
        if (not std::experimental::any_of (vec[i] != rhs.vec[i]))
          return true;
      return false;
    }

    // Used by Sets, should be a total order.
    bool operator< (const vector_simd_vector& rhs) const {
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

    int operator[] (size_t i) const {
      return vec[i / VECTOR_SIMD_SIZE][i % VECTOR_SIMD_SIZE];
    }

    fssimd::reference operator[] (size_t i) {
      return vec[i / VECTOR_SIMD_SIZE][i % VECTOR_SIMD_SIZE];
    }


    vector_simd_vector meet (const vector_simd_vector& rhs) const {
      auto res = vector_simd_vector (k);

      for (size_t i = 0; i < nsimds; ++i)
        res.vec[i] = std::experimental::min (vec[i], rhs.vec[i]);
      return res;
    }

    auto size () const {
      return k;
    }

  private:
    const unsigned int k, nsimds;
    std::vector<fssimd> vec;
    friend std::ostream& operator<<(std::ostream& os, const vector_simd_vector& v);
};

inline
std::ostream& operator<<(std::ostream& os, const vector_simd_vector& v)
{
  os << "{ ";
  int k = v.k;
  for (size_t i = 0; i < v.nsimds; ++i)
    for (size_t j = 0; j < VECTOR_SIMD_SIZE; ++j) {
      os << v.vec[i][j] << " ";
      if (--k == 0)
        break;
    }
  os << "}";
  return os;
}
