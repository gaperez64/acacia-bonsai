#pragma once

#include <cassert>
#include <iostream>

namespace vectors {
#define BYTES_PER_UNIT 8

  template <typename T, size_t Units>
  class array_backed_;

  template <typename T, size_t K>
  using array_backed = array_backed_<T, (K + BYTES_PER_UNIT - 1) / BYTES_PER_UNIT>;

  template <typename T, size_t Units>
  class array_backed_ : public std::array<T, Units * BYTES_PER_UNIT> {
      size_t k;
      using self = array_backed_<T, Units>;
    public:
      array_backed_ (size_t k) : k {k} {
        assert ((k + BYTES_PER_UNIT - 1) / BYTES_PER_UNIT == Units);
        for (size_t i = k; i < Units * BYTES_PER_UNIT; ++i)
          (*this)[i] = 0;
      }

      size_t size () const { return k; }

      array_backed_ () = delete;

    private:
      array_backed_ (const self& other) = default;

    public:
      array_backed_ (self&& other) = default;

      // explicit copy operator
      array_backed_ copy () const {
        auto res = array_backed_ (*this);
        return res;
      }

      self& operator= (self&& other) = default;
      self& operator= (const self& other) = delete;

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

      inline auto partial_order (const self& rhs) const {
        return po_res (*this, rhs);
      }

      self meet (const self& rhs) const {
        auto res = self (k);

        for (size_t i = 0; i < k; ++i)
          res[i] = std::min ((*this)[i], rhs[i]);
        return res;
      }
  };

}

template <typename T, size_t Units>
inline
std::ostream& operator<<(std::ostream& os, const vectors::array_backed_<T, Units>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
