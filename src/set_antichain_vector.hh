#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

// Forward definition for the operator<<s.
template <typename Vector>
class set_antichain_vector;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set_antichain_vector<Vector>& f);

template <typename Vector>
class set_antichain_vector {
  public:
    set_antichain_vector () {}

    set_antichain_vector (const set_antichain_vector& other) :
      vector_set{other.vector_set} {}

    set_antichain_vector& operator= (set_antichain_vector&& other) {
      vector_set = std::move (other.vector_set);

      return *this;
    }

    bool contains (const Vector& v) const {
      for (const auto& e : vector_set)
        if (v.partial_leq (e))
          return true;
      return false;
    }

    void clear_update_flag () {
      _updated = false;
    }

    bool updated () {
      return _updated;
    }

    auto size () const {
      return vector_set.size ();
    }

    bool insert (const Vector& v) {
      std::vector<Vector> to_remove;
      bool should_be_inserted = true;

      // This is like remove_if, but allows breaking.
      auto result = vector_set.begin ();
      auto end = vector_set.end ();
      for (auto it = result; it != end; ++it) {
        if (v.partial_leq (*it)) {
          should_be_inserted = false;
          break;
        } else if (it->partial_lt (v)) {
          should_be_inserted = true;
        } else { // Element needs to be kept
          *result = std::move (*it);
          ++result;
        }
      }

      if (should_be_inserted) {
        vector_set.erase (result, vector_set.end ());
        vector_set.push_back (v);
        _updated = true;
      }

      return should_be_inserted;
    }

    void downward_close () {
      // nil
    }

    const set_antichain_vector& max_elements () const {
      return *this;
    }

    bool empty () {
      return vector_set.empty ();
    }

    void union_with (const set_antichain_vector& other) {
      for (const auto& e : other.vector_set)
        _updated |= insert (e);
    }

    void intersect_with (const set_antichain_vector& other) {
      set_antichain_vector intersection;

      for (const auto& x : vector_set)
        for (const auto& y : other.vector_set)
          intersection.insert (x.meet (y));
      if (this->vector_set != intersection.vector_set) {
        *this = std::move (intersection);
        _updated = true;
      }
    }

    template <typename F>
    void apply (const F& lambda) {
      std::vector<Vector> new_set;
      for (auto el : vector_set) {
        auto&& changed_el = lambda (el);
        if (changed_el != el)
          _updated = true;
        new_set.push_back (changed_el);
      }
      vector_set = std::move (new_set);
    }

  private:
    std::vector<Vector> vector_set;
    bool _updated = false;
    template <typename V>
    friend std::ostream& ::operator<<(std::ostream& os, const set_antichain_vector<V>& f);
};


template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os, const set_antichain_vector<Vector>& f)
{
  for (auto el : f.vector_set)
    os << el << std::endl;

  return os;
}
