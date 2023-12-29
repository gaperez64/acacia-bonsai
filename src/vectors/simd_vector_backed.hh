#pragma once
#include <cassert>
#include <experimental/simd>
#include <iostream>
#include <span>

#include "vectors/simd_po_res.hh"
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
                                      data (nsimds) {
        assert (nsimds >= 1);
        data.back () ^= data.back ();
      }
    public:
      simd_vector_backed (std::span<const T> v) : simd_vector_backed (v.size ()) {
        sum = 0;
        for (auto&& c : v)
          sum += c;
        data.back () ^= data.back ();
        // Trust memcpy to DTRT.
        std::memcpy ((char*) data.data (), (char*) v.data (), v.size ());
      }


      simd_vector_backed () = delete;
      simd_vector_backed (const self& other) = delete;
      simd_vector_backed (self&& other) = default;

      self copy () const {
        auto res = self (k);
        res.data = data;
        res.sum = sum;
        return res;
      }

      self& operator= (self&& other) {
        assert (other.k == k and other.nsimds == nsimds);
        data = std::move (other.data);
        sum = other.sum;
        return *this;
      }

      self& operator= (const self& other) = delete;

      static constexpr size_t capacity_for (size_t elts) {
        return traits::capacity_for (elts);
      }

      void to_vector (std::span<T> v) const {
        memcpy ((char*) v.data (), (char*) data.data (), data.size () * simd_size);
      }

      inline auto partial_order (const self& rhs) const {
        return simd_po_res (*this, rhs);
      }

      bool operator== (const self& rhs) const {
        if (sum != rhs.sum)
          return false;
        // Trust memcmp to DTRT
        return std::memcmp ((char*) rhs.data.data (), (char*) data.data (), nsimds * simd_size) == 0;
      }

      bool operator!= (const self& rhs) const {
        if (sum != rhs.sum)
          return true;
        // Trust memcmp to DTRT
        return std::memcmp ((char*) rhs.data.data (), (char*) data.data (), nsimds * simd_size) != 0;
      }

      // Used by Sets, should be a total order.  Do not use.
      bool operator< (const self& rhs) const {
        for (size_t i = 0; i < nsimds; ++i) {
          auto lhs_lt_rhs = data[i] < rhs.data[i];
          auto rhs_lt_lhs = rhs.data[i] < data[i];
          auto p1 = find_first_set (lhs_lt_rhs);
          auto p2 = find_first_set (rhs_lt_lhs);
          if (p1 == p2)
            continue;
          return (p1 < p2);
        }
        return false;
      }

      T operator[] (size_t i) const {
        return data[i / simd_size][i % simd_size];
      }

      typename traits::fssimd::reference operator[] (size_t i) {
        return data[i / simd_size][i % simd_size];
      }


      self meet (const self& rhs) const {
        auto res = self (k);

        for (size_t i = 0; i < nsimds; ++i) {
          res.data[i] = std::experimental::min (data[i], rhs.data[i]);
          // This should NOT be used since this can lead to overflows over char
          //   res.sum += std::experimental::reduce (res.data[i]);
          // instead, we manually loop through:
          for (size_t j = 0; j < simd_size; ++j)
            res.sum += res.data[i][j];
        }

        return res;
      }

      auto size () const {
        return k;
      }

      auto bin () const {
        return (sum + k) / k;
      }

    private:
      friend simd_po_res<self>;
      const size_t k, nsimds;
      std::vector<typename traits::fssimd> data;
      int sum = 0;
  };

  template <typename T>
  struct traits<simd_vector_backed, T> {
      static constexpr auto capacity_for (size_t elts) {
        return utils::simd_traits<T>::capacity_for (elts);
      }
  };
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
}

