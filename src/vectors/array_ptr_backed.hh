#pragma once

#include <memory>
#include <cstring>
#include <cassert>
#include <iostream>
#include <span>

#include <boost/pool/object_pool.hpp>

namespace vectors {
  // What's the multiple of T's we store.  This is used to speed up compilation
  // and reduce program size.
#define T_PER_UNIT 8

  template <typename T, size_t Units>
  class array_ptr_backed_;

  template <typename T, size_t K>
  using array_ptr_backed = array_ptr_backed_<T, (K + T_PER_UNIT - 1) / T_PER_UNIT>;

  template <typename T, size_t Units>
  class array_ptr_backed_ {
      using self = array_ptr_backed_<T, Units>;
    public:
      using array_t = std::array<T, Units * T_PER_UNIT>;
    private:
      using array_ptr_t = array_t*;
      array_ptr_t data = nullptr;
      static inline boost::object_pool<array_t> malloc {8192};

    public:
      using value_type = T;

      array_ptr_backed_ (std::span<const T> v) : data (malloc.construct ()), k {v.size ()} {
        assert (k <= Units * T_PER_UNIT);
        std::copy (v.begin (), v.end (), data->data ());
        if (Units * T_PER_UNIT > k)
          std::fill (&(*data)[k], data->end (), 0);
      }

      ~array_ptr_backed_ () {
        if (data) malloc.destroy (data);
      }

      size_t size () const { return k; }

    private:
      array_ptr_backed_ (size_t k) : k {k} {}
      array_ptr_backed_ (const self& other) = default;

    public:
      array_ptr_backed_ (self&& other) : data (other.data), k (other.k) {
        assert (data);
        other.data = nullptr;
      }

      // explicit copy operator
      array_ptr_backed_ copy () const {
        auto res = array_ptr_backed_ (std::span (data->begin (), k));
        return res;
      }

      bool operator== (const self& rhs) const {
        return std::memcmp (data->data (), rhs.data->data (), k * sizeof (T)) == 0;
      }

      self& operator= (self&& other) {
        assert (this != &other);
        assert (k == other.k);
        assert (other.data);
        if (data)
          malloc.destroy (data);
        data = other.data;
        other.data = nullptr;
        return *this;
      }

      self& operator= (const self& other) = delete;

      static constexpr size_t capacity_for (size_t elts) {
        return elts;
      }

      void to_vector (std::span<char> v) const {
        assert (v.size () >= k);
        std::copy (data->begin (), &(*data)[k], v.data ());
      }

      class po_res {
        public:
          po_res (const self& lhs, const self& rhs) {
            bgeq = true;
            bleq = true;
            for (size_t i = 0; i < lhs.k; ++i) {
              auto diff = lhs[i] - rhs[i];
              if (bgeq)
                bgeq = (diff >= 0);
              if (bleq)
                bleq = (diff <= 0);
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

      const T& operator[] (size_t i) const {
        return (*data)[i];
      }

      inline auto partial_order (const self& rhs) const {
        assert (rhs.k == k);

        return po_res (*this, rhs);
      }

      self meet (const self& rhs) const {
        auto res = self (k);
        res.data = malloc.construct ();
        assert (rhs.k == k);

        for (size_t i = 0; i < k; ++i)
          (*res.data)[i] = std::min ((*data)[i], rhs[i]);

        if (k < Units * T_PER_UNIT)
          std::fill (&(*res.data)[k], res.data->end (), 0);

        return res;
      }

    private:
      const size_t k;
  };

  template <typename T, size_t Units>
  inline
  std::ostream& operator<<(std::ostream& os, const vectors::array_ptr_backed_<T, Units>& v)
  {
    os << "{ ";
    for (size_t i = 0; i < v.size (); ++i)
      os << v[i] << " ";
    os << "}";
    return os;
  }
}
