#pragma once
#include <bitset>

namespace vectors {

  static constexpr auto nbools_to_nbitsets (size_t nbools) {
    return (nbools + sizeof (unsigned long) * 8 - 1) / (sizeof (unsigned long) * 8);
  }

  static constexpr auto nbitsets_to_nbools (size_t nbitsets) {
    return nbitsets * sizeof (unsigned long) * 8;
  }

  static size_t bitset_threshold = 0;

  template <size_t NBitsets, typename X>
  class bitset_and_X {
      using self = bitset_and_X<NBitsets, X>;

      static constexpr auto Bools = nbitsets_to_nbools (NBitsets);
      static auto nbools () { return std::min (Bools, bitset_threshold); }

    public:
      using value_type = X::value_type;

      bitset_and_X (const std::vector<value_type>& v) :
        k {v.size ()},
        x {std::span (v.data () + nbools (),
                      v.size () - nbools ())}
      {
        for (size_t i = 0; i < nbools (); ++i)
          bools[i] = (v[i] + 1);
      }

      size_t size () const { return nbools () + x.size (); }


      bitset_and_X (self&& other) = default;

    private:

      bitset_and_X (size_t k, std::bitset<Bools>&& bools, X&& x) :
        k {k},
        bools {std::move (bools)},
        x {std::move (x)} {}

    public:

      // explicit copy operator
      self copy () const {
        std::bitset<Bools> b = bools;
        return bitset_and_X (k, std::move (b), x.copy ());
      }

      self& operator= (self&& other) {
        assert (other.k == k);
        bools = std::move (other.bools);
        x = std::move (other.x);
        return *this;
      }

      self& operator= (const self& other) = delete;

      static size_t capacity_for (size_t elts) {
        return nbools () + X::capacity_for (elts - nbools ());
      }

      void to_vector (std::span<value_type> v) const {
        for (size_t i = 0; i < nbools (); ++i)
          v[i] = bools[i] - 1;
        x.to_vector (std::span (v.data () + nbools (),
                                v.size () - nbools ()));
      }

      class po_res {
        public:
          po_res (const self& lhs, const self& rhs) {
            auto diff = lhs.bools | rhs.bools;
            bgeq = (diff == lhs.bools);
            bleq = (diff == rhs.bools);

            if (not bgeq and not bleq)
              return;
            auto po = lhs.x.partial_order (rhs.x);
            bgeq = bgeq and po.geq ();
            bleq = bleq and po.leq ();
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
        assert (rhs.k == k);
        return po_res (*this, rhs);
      }

      bool operator== (const self& rhs) const {
        return bools == rhs.bools and x == rhs.x;
      }

      bool operator!= (const self& rhs) const {
        return bools != rhs.bools or x != rhs.x;
      }

      value_type operator[] (size_t i) const {
        if (i < nbools ())
          return bools[i] - 1;
        return x[i - nbools ()];
      }

    public:

      self meet (const self& rhs) const {
        assert (rhs.k == k);
        return self (k, bools & rhs.bools, x.meet (rhs.x));
      }

      bool operator< (const self& rhs) const {
        int cmp = std::memcmp (&bools, &rhs.bools, sizeof (bools));
        if (cmp == 0)
          return (x < rhs.x);
        return (cmp < 0);
      }


    private:
      const size_t k;
      std::bitset<Bools> bools;
      X x;
  };
}

template <size_t Bools, class X>
inline
std::ostream& operator<<(std::ostream& os, const vectors::bitset_and_X<Bools, X>& v)
{
  os << "{ ";
  for (size_t i = 0; i < v.size (); ++i)
    os << (int) v[i] << " ";
  os << "}";
  return os;
}
