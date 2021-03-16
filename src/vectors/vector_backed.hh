#pragma once

namespace vectors {

  template <typename T>
  class vector_backed : public std::vector<T> {
    public:
      vector_backed (unsigned int k) : std::vector<T> (k), k {k} {}
      // Disallow creating a vector with no dimension.
      vector_backed () = delete;

    private:
      vector_backed (const vector_backed& other) = default;

    public:
      vector_backed (vector_backed&& other) = default;

      vector_backed& operator= (vector_backed&& other) {
        std::vector<T>::operator= (std::move (other));
        assert (k == other.k);
        return *this;
      }

      vector_backed& operator= (const vector_backed&) = delete;

      vector_backed copy () const {
        return *this;
      }

      class po_res {
        public:
          po_res (const vector_backed& lhs, const vector_backed& rhs) {
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

      auto partial_order (const vector_backed& rhs) const {
        return po_res (*this, rhs);
      }

      vector_backed meet (const vector_backed& rhs) const {
        vector_backed res (this->size ());

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
std::ostream& operator<<(std::ostream& os, const vectors::vector_backed<T>& v)
{
  os << "{ ";
  for (auto el : v)
    os << el << " ";
  os << "}";
  return os;
}
