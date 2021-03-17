#pragma once

#include <cassert>
#include <iostream>

namespace vectors {
  template <typename T, size_t K>
  class array_backed : public std::array<T, K> {
      using self = array_backed<T, K>;
    public:
      array_backed (size_t k) {
        assert (K == k);
      }

      array_backed () = delete;

    private:
      array_backed (const self& other) = default;

    public:
      array_backed (self&& other) = default;

      // explicit copy operator
      array_backed copy () const {
        auto res = array_backed (*this);
        return res;
      }

      self& operator= (self&& other) = default;
      self& operator= (const self& other) = delete;

      class po_res {
        public:
          po_res (const self& lhs, const self& rhs) {
            bgeq = true;
            bleq = true;
            for (size_t i = 0; i < K; ++i) {
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
        auto res = self (K);

        for (size_t i = 0; i < K; ++i)
          res[i] = std::min ((*this)[i], rhs[i]);
        return res;
      }
  };

}

template <typename T, size_t K>
inline
std::ostream& operator<<(std::ostream& os, const vectors::array_backed<T, K>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
