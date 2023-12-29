#pragma once

#include <cstring>
#include <cassert>
#include <experimental/simd>
#include <iostream>

#include "vectors/simd_po_res.hh"
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
        data.back () ^= data.back ();
        // Trust memcpy to DTRT.
        std::memcpy ((char*) data.data (), (char*) v.data (), v.size ());
      }

      simd_array_backed_sum_ () = delete;
      simd_array_backed_sum_ (const self& other) = delete;
      simd_array_backed_sum_ (self&& other) = default;

      // explicit copy operator
      simd_array_backed_sum_ copy () const {
        auto res = simd_array_backed_sum_ (k);
        res.data = data;
        res.sum = sum;
        return res;
      }

      self& operator= (self&& other) {
        data = std::move (other.data);
        sum = other.sum;
        return *this;
      }

      self& operator= (const self& other) = delete;

      static constexpr size_t capacity_for (size_t elts) {
        return nsimds * simd_size;
      }

      void to_vector (std::span<char> v) const {
        memcpy ((char*) v.data (), (char*) data.data (), data.size () * simd_size);
      }


      inline auto partial_order (const self& rhs) const {
        return simd_po_res (*this, rhs);
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
        return data[i / simd_size][i % simd_size];
      }

      auto bin () const {
        return (sum + k) / k;
      }

    private:
      friend simd_po_res<self>;
      std::array<typename traits::fssimd, nsimds> data;
      const size_t k;
      int sum = 0;
  };

  template <typename T>
  struct traits<simd_array_backed_sum, T> {
      static constexpr auto capacity_for (size_t elts) {
        return utils::simd_traits<T>::capacity_for (elts);
      }
  };

  template <typename T, size_t nsimds>
  inline
  std::ostream& operator<<(std::ostream& os, const vectors::simd_array_backed_sum_<T, nsimds>& v)
  {
    return v.print (os);
  }
}

