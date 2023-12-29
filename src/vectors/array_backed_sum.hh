#pragma once

#include <cassert>
#include <iostream>

namespace vectors {
  // What's the multiple of T's we store.  This is used to speed up compilation
  // and reduce program size.
#define T_PER_UNIT 8

  template <typename T, size_t Units>
  class array_backed_sum_;

  template <typename T, size_t K>
  using array_backed_sum = array_backed_sum_<T, (K + T_PER_UNIT - 1) / T_PER_UNIT>;

  template <typename T, size_t Units>
  class array_backed_sum_ : public std::array<T, Units * T_PER_UNIT> {
      using self = array_backed_sum_<T, Units>;
      using base = std::array<T, Units * T_PER_UNIT>;

    public:
      array_backed_sum_ (std::span<const T> v) : k {v.size ()} {
        sum = 0;
        for (auto&& c : v)
          sum += c;

        assert (k <= Units * T_PER_UNIT);
        std::copy (v.begin (), v.end (), this->data ());
        if (Units * T_PER_UNIT > k)
          std::fill (&(*this)[k], this->end (), 0);
      }

      size_t size () const { return k; }

    private:
      array_backed_sum_ (size_t k) : k {k} {}
      array_backed_sum_ (const self& other) = default;

    public:
      array_backed_sum_ (self&& other) = default;

      // explicit copy operator
      array_backed_sum_ copy () const {
        auto res = array_backed_sum_ (*this);
        return res;
      }

      self& operator= (self&& other) {
        assert (other.k == k);
        base::operator= (std::move (other));
        sum = other.sum;
        return *this;
      }

      self& operator= (const self& other) = delete;

      static constexpr size_t capacity_for (size_t elts) {
        return elts;
      }

      void to_vector (std::span<char> v) const {
        assert (v.size () >= k);
        std::copy (this->begin (), &(*this)[k], v.data ());
      }

      class po_res {
        public:
          po_res (const self& lhs, const self& rhs) : lhs {lhs}, rhs {rhs} {
            bgeq = (lhs.sum >= rhs.sum);
            if (not bgeq)
              has_bgeq = true;

            bleq = (lhs.sum <= rhs.sum);
            if (not bleq)
              has_bleq = true;

            if (has_bgeq or has_bleq)
              return;

            for (up_to = 0; up_to < lhs.k; ++up_to) {
              auto diff = lhs[up_to] - rhs[up_to];
              bgeq = bgeq and (diff >= 0);
              bleq = bleq and (diff <= 0);
              if (not bgeq)
                has_bgeq = true;
              if (not bleq)
                has_bleq = true;
              if (has_bgeq or has_bgeq)
                return;
            }
            has_bgeq = true;
            has_bleq = true;
          }

          inline bool geq () {
            if (has_bgeq)
              return bgeq;
            assert (has_bleq);
            has_bgeq = true;
            for (; up_to < lhs.k; ++up_to) {
              auto diff = lhs[up_to] - rhs[up_to];
              bgeq = bgeq and (diff >= 0);
              if (not bgeq)
                break;
            }
            return bgeq;
          }

          inline bool leq () {
            if (has_bleq)
              return bleq;
            assert (has_bgeq);
            has_bleq = true;
            for (; up_to < lhs.k; ++up_to) {
              auto diff = rhs[up_to] - lhs[up_to];
              bleq = bleq and (diff >= 0);
              if (not bleq)
                break;
            }
            return bleq;
          }

        private:
          const self& lhs;
          const self& rhs;
          bool bgeq, bleq;
          bool has_bgeq = false,
            has_bleq = false;
          size_t up_to = 0;
      };

      inline auto partial_order (const self& rhs) const {
        assert (rhs.k == k);
        return po_res (*this, rhs);
      }

      bool operator== (const self& rhs) const {
        return sum == rhs.sum and (static_cast<base> (*this) == static_cast<base> (rhs));
      }

      bool operator!= (const self& rhs) const {
        return sum != rhs.sum or (static_cast<base> (*this) != static_cast<base> (rhs));
      }

      self meet (const self& rhs) const {
        auto res = self (k);
        assert (rhs.k == k);

        res.sum = 0;
        for (size_t i = 0; i < k; ++i) {
          res[i] = std::min ((*this)[i], rhs[i]);
          res.sum += res[i];
        }

        if (k < Units * T_PER_UNIT)
          std::fill (&res[k], res.end (), 0);

        return res;
      }

      auto bin () const {
        return (sum + k) / k;
      }

    private:
      const size_t k;
      int sum = 0;
  };

template <typename T, size_t Units>
inline
std::ostream& operator<<(std::ostream& os, const vectors::array_backed_sum_<T, Units>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << v[i] << " ";
  os << "}";
  return os;
}
}

