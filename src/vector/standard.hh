#pragma once

class vector_vector : public std::vector<int> {
  public:
    vector_vector (unsigned int k) : std::vector<int> (k), k {k} {}
    // Disallow creating a vector with no dimension.
    vector_vector () = delete;

    vector_vector (const vector_vector& other) : std::vector<int> (other), k {other.k} {}

    vector_vector copy () const {
      return vector_vector (*this);
    }

    vector_vector& operator= (vector_vector&& other) {
      std::vector<int>::operator= (std::move (other));
      assert (k == other.k);
      return *this;
    }

    vector_vector& operator= (const vector_vector&) = delete;

    class po_res {
      public:
        po_res (const vector_vector& lhs, const vector_vector& rhs) {
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

    auto partial_order (const vector_vector& rhs) const {
      return po_res (*this, rhs);
    }

    vector_vector meet (const vector_vector& rhs) const {
      vector_vector res (this->size ());

      for (unsigned i = 0; i < rhs.k; ++i)
        res[i] = std::min ((*this)[i], rhs[i]);
      return res;
    }
  private:
    const unsigned int k;
};

inline
std::ostream& operator<<(std::ostream& os, const vector_vector& v)
{
  os << "{ ";
  for (auto el : v)
    os << el << " ";
  os << "}";
  return os;
}
