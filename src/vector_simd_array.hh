#pragma once
#include <cassert>


#ifndef VECTOR_SIMD_SIZE
# define VECTOR_SIMD_SIZE 16
#endif

#include <experimental/simd>
#include <iostream>

using std::experimental::fixed_size_simd;
using fssimd = fixed_size_simd<int, VECTOR_SIMD_SIZE>;

template <unsigned int K>
class vector_simd_array {
    static const unsigned int nsimds = (K + VECTOR_SIMD_SIZE - 1) / VECTOR_SIMD_SIZE;
  public:
    vector_simd_array ([[maybe_unused]] unsigned int k)  {
      assert (k == K);
      ar[nsimds - 1] ^= ar[nsimds - 1];
    }

    vector_simd_array () = delete;
    vector_simd_array (const vector_simd_array<K>& other) : ar {other.ar} {}

    vector_simd_array<K>& operator= (vector_simd_array<K>&& other) {
      ar = std::move (other.ar);
      return *this;
    }

    vector_simd_array<K>& operator= (const vector_simd_array<K>& other) = delete;

    class po_res {
      public:
        po_res (const vector_simd_array<K>& lhs, const vector_simd_array<K>& rhs) {
          bgeq = true;
          bleq = true;
          for (size_t i = 0; i < vector_simd_array<K>::nsimds; ++i) {
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

    inline auto partial_order (const vector_simd_array<K>& rhs) const {
      return po_res (*this, rhs);
    }

    bool operator== (const vector_simd_array<K>& rhs) const {
      for (size_t i = 0; i < nsimds; ++i)
        if (not std::experimental::all_of (ar[i] == rhs.ar[i]))
          return false;
      return true;
    }

    bool operator!= (const vector_simd_array<K>& rhs) const {
      for (size_t i = 0; i < nsimds; ++i)
        if (not std::experimental::any_of (ar[i] != rhs.ar[i]))
          return true;
      return false;
    }

    // Used by Sets, should be a total order.
    bool operator< (const vector_simd_array<K>& rhs) const {
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
      return ar[i / VECTOR_SIMD_SIZE][i % VECTOR_SIMD_SIZE];
    }

    fssimd::reference operator[] (size_t i) {
      return ar[i / VECTOR_SIMD_SIZE][i % VECTOR_SIMD_SIZE];
    }

    vector_simd_array<K> meet (const vector_simd_array<K>& rhs) const {
      auto res = vector_simd_array<K> (K);

      for (size_t i = 0; i < nsimds; ++i)
        res.ar[i] = std::experimental::min (ar[i], rhs.ar[i]);
      return res;
    }

    auto size () const {
      return K;
    }

  private:
    std::array<fssimd, nsimds> ar;
    template <unsigned int _K>
    friend std::ostream& operator<<(std::ostream& os, const vector_simd_array<_K>& v);
};

template <unsigned int K>
inline
std::ostream& operator<<(std::ostream& os, const vector_simd_array<K>& v)
{
  os << "{ ";
  for (size_t i = 0; i < K; ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
