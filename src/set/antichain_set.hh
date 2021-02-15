#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

// Forward definition for the operator<<s.
template <typename Vector>
class set_antichain_set;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set_antichain_set<Vector>& f);

template <typename Vector>
class set_antichain_set {
  public:
    set_antichain_set () {}

    set_antichain_set (const set_antichain_set& other) :
      vector_set{other.vector_set} {}

    set_antichain_set& operator= (set_antichain_set&& other) {
      vector_set = std::move (other.vector_set);

      return *this;
    }

    bool contains (const Vector& v) const {
      for (const auto& e : vector_set)
        if (v.partial_order (e).leq ())
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
      std::set<Vector> to_remove;
      bool should_be_inserted = true;

      for (const auto& e : vector_set) {
        auto po = v.partial_order (e);
        if (po.leq ()) {
          should_be_inserted = false;
          break;
        } else if (po.geq ()) {
          should_be_inserted = true;
          to_remove.insert (e);
        }
      }

      for (const auto& elt : to_remove)
        vector_set.erase (elt);

      if (should_be_inserted) {
        vector_set.insert (v);
        _updated = true;
      }

      return should_be_inserted;
    }

    void downward_close () {
      // nil
    }

    const set_antichain_set& max_elements () const {
      return *this;
    }

    bool empty () {
      return vector_set.empty ();
    }

    void union_with (const set_antichain_set& other) {
      for (const auto& e : other.vector_set)
        _updated |= insert (e);
    }

    void intersect_with (const set_antichain_set& other) {
      set_antichain_set intersection;
      bool smaller_set = false;

      for (const auto& x : vector_set) {
        bool dominated = false;

        for (const auto& y : other.vector_set) {
          Vector &&v = x.meet (y);
          intersection.insert (v);
          if (v == x) {
            dominated = true;
            break;
          }
        }
        // If x wasn't <= an element in other, then x is not in the
        // intersection, thus the set is updated.
        smaller_set |= not dominated;
      }

      if (smaller_set) {
        *this = std::move (intersection);
        _updated = true;
      }
    }

    template <typename F>
    void apply_inplace (const F& lambda) {
      std::set<Vector> new_set;
      for (auto el : vector_set) {
        auto&& changed_el = lambda (el);
        if (changed_el != el)
          _updated = true;
        new_set.insert (changed_el);
      }
      vector_set = std::move (new_set);
    }

  private:
    std::set<Vector> vector_set;
    bool _updated = false;
    template <typename V>
    friend std::ostream& ::operator<<(std::ostream& os, const set_antichain_set<V>& f);
};

template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os, const set_antichain_set<Vector>& f)
{
  for (auto el : f.vector_set)
    os << el << std::endl;

  return os;
}
