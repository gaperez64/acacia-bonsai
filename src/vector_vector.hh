#pragma once

class vector_vector : public std::vector<int> {
  public:
    vector_vector (unsigned int k) : std::vector<int> (k), k {k} {}
    // Disallow creating a vector with no dimension.
    vector_vector () = delete;

    vector_vector (const vector_vector& other) : std::vector<int> (other), k {other.k} {}

    bool partial_leq (const vector_vector& rhs) const {
      for (unsigned i = 0; i < rhs.k; ++i)
        if (not ((*this)[i] <= rhs[i]))
          return false;
      return true;
    }

    bool partial_lt (const vector_vector& rhs) const {
      return rhs != *this && partial_leq (rhs);
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
