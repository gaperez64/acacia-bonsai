#pragma once

/// \brief Wrapper class around bdd to provide an operator< with bool value.
/// The original type returns a bdd.  This is needed to implement a set<bdd>, or
/// a map<> with this as (part of a) key, since map<>s are key-sorted.
class bdd_t : public bdd {
  public:
    bdd_t (bdd b) : bdd {b} {}

    bdd_t (bdd&& b) : bdd {b} {}

    bool operator< (const bdd_t& other) const {
      return id () < other.id ();
    }

    bool operator== (const bdd_t& other) const {
      return id () == other.id ();
    }
};

using bdd_set = std::set <bdd_t>;

template <class T>
inline
bool empty_intersection(const std::set<T>& x, const std::set<T>& y)
{
    auto i = x.begin();
    auto j = y.begin();
    while (i != x.end() && j != y.end())
    {
      if (*i < *j)
        ++i;
      else if (*j < *i)
        ++j;
      else
        return false;
    }
    return true;
}
