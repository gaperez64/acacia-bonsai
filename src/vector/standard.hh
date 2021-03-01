#pragma once

namespace vector {

  template <typename T>
  class standard : public std::vector<T> {
    public:
      standard (unsigned int k) : std::vector<T> (k), k {k} {}
      // Disallow creating a vector with no dimension.
      standard () = delete;

    private:
      standard (const standard& other) = default;

    public:
      standard (standard&& other) = default;

      standard& operator= (standard&& other) {
        std::vector<T>::operator= (std::move (other));
        assert (k == other.k);
        return *this;
      }

      standard& operator= (const standard&) = delete;

      standard copy () const {
        return *this;
      }

      class po_res {
        public:
          po_res (const standard& lhs, const standard& rhs) {
            bleq = true;
            bgeq = true;
            for (unsigned i = 0; i < rhs.k; ++i) {
              bleq = bleq and lhs[i] <= rhs[i];
              bgeq = bgeq and lhs[i] >= rhs[i];
              if (not bleq and not bgeq)
                break;
            }
          }

          bool geq () { return bgeq; }
          bool leq () { return bleq; }
        private:
          bool bgeq, bleq;
      };

      auto partial_order (const standard& rhs) const {
        return po_res (*this, rhs);
      }

      standard meet (const standard& rhs) const {
        standard res (this->size ());

        for (unsigned i = 0; i < rhs.k; ++i)
          res[i] = std::min ((*this)[i], rhs[i]);
        return res;
      }
    private:
      const unsigned int k;
  };
}

template <typename T>
inline
std::ostream& operator<<(std::ostream& os, const vector::standard<T>& v)
{
  os << "{ ";
  for (auto el : v)
    os << el << " ";
  os << "}";
  return os;
}
