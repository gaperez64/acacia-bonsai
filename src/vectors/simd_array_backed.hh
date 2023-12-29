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
      using value_type = T;

    private:
      simd_array_backed_ (size_t k) : k {k} { }

    public:
      simd_array_backed_ (std::span<const T> v) : k {v.size ()} {
        data.back () ^= data.back ();
        // Trust memcpy to DTRT.
        std::memcpy ((char*) data.data (), (char*) v.data (), v.size ());
      }


      simd_array_backed_ () = delete;
      simd_array_backed_ (const self& other) = delete;
      simd_array_backed_ (self&& other) = default;

      // explicit copy operator
      simd_array_backed_ copy () const {
        auto res = simd_array_backed_ (k);
        res.data = data;
        return res;
      }

      self& operator= (self&& other) {
        data = std::move (other.data);
        return *this;
      }

      self& operator= (const self& other) = delete;

      inline auto partial_order (const self& rhs) const {
        return simd_po_res (*this, rhs);
      }

      bool operator== (const self& rhs) const {
        for (size_t i = 0; i < nsimds; ++i)
          if (not std::experimental::all_of (data[i] == rhs.data[i]))
            return false;
        return true;
      }

      bool operator!= (const self& rhs) const {
        for (size_t i = 0; i < nsimds; ++i)
          if (not std::experimental::any_of (data[i] != rhs.data[i]))
            return true;
        return false;
      }

      // Used by std::sets, should be a total order.  Do not use.
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

      static constexpr size_t capacity_for (size_t elts) {
        return nsimds * simd_size;
      }

      void to_vector (std::span<char> v) const {
        // Sadly, we can't assume that v is aligned, as it could be the values after the bool cut-off.
        for (size_t i = 0; i < nsimds; ++i) {
          data[i].copy_to (&v[i * simd_size], std::experimental::vector_aligned);
        }
      }

      T operator[] (size_t i) const {
        return data[i / simd_size][i % simd_size];
      }

      self meet (const self& rhs) const {
        auto res = self (k);

        for (size_t i = 0; i < nsimds; ++i)
          res.data[i] = std::experimental::min (data[i], rhs.data[i]);
        return res;
      }

      auto size () const {
        return k;
      }

    private:
      friend simd_po_res<self>;
      std::array<typename traits::fssimd, nsimds> data;
      const size_t k;
  };

  template <typename T>
  struct traits<simd_array_backed_, T> {
      static constexpr auto capacity_for (size_t elts) {
        return utils::simd_traits<T>::capacity_for (elts);
      }
  };
template <typename T, size_t nsimds>
inline
std::ostream& operator<<(std::ostream& os, const vectors::simd_array_backed_<T, nsimds>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << (int) v[i] << " ";
  os << "}";
  return os;
}
}

