#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

// Forward definitions for the operator<<s.
template <typename Vector>
class set_set;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set_set<Vector>& v);

template <typename Vector>
class set_set {
  public:
    set_set () {}

    set_set (const set_set& other) :
      vector_set{other.vector_set} {}

    set_set& operator= (set_set&& other) {
      vector_set = std::move (other.vector_set);

      return *this;
    }

    bool contains (const Vector& v) const {
      return vector_set.find(v) != vector_set.end();
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
      if (vector_set.insert (v).second) {
        _updated = true;
        return true;
      }
      return false;
    }

    // Extraordinarily wasteful.  This computes the closure by taking
    // vector_set, then everything at distance 1
    void downward_close () {
      std::set<Vector> closure (vector_set);

      while (1) {
        std::set<Vector> newelts;

        for (auto el : closure) { // Iterate while making copies.
          for (size_t i = 0; i < el.size (); ++i)
            if (el[i] > -1) {
              --el[i];
              if (closure.find (el) == closure.end ())
                newelts.insert (el);
              ++el[i];
            }
        }
        if (newelts.empty ())
          break;
        closure.insert (newelts.cbegin (), newelts.cend ());
      }

      if (vector_set.size () != closure.size ()) {
        _updated = true;
        vector_set = closure;
      }
    }

  private:
    // Extraordinarily wasteful.  This checks that for every member of
    // vector_set, all vectors at Hamming distance 1 are also in the set.
    bool is_downward_closed () const {
      for (auto el : vector_set) { // Iterate while making copies.
        for (size_t i = 0; i < el.size (); ++i)
          if (el[i] > -1) {
            --el[i];
            if (not contains (el))
              return false;
            ++el[i];
          }
      }
      return true;
    }

  public:

    set_set max_elements () const {
      assert (is_downward_closed ());
      std::set<Vector> maxelts;

      for (const auto& el : vector_set) {
        std::set<Vector> to_remove;
        bool should_be_inserted = true;

        for (const auto& maxelt : maxelts) {
          auto po = el.partial_order (maxelt);
          if (po.leq ()) {
            should_be_inserted = false;
            break;
          } else if (po.geq ()) {
            should_be_inserted = true;
            to_remove.insert (maxelt);
          }
        }
        for (const auto& elt : to_remove)
          maxelts.erase (elt);
        if (should_be_inserted)
          maxelts.insert (el);
      }
      set_set set_of_maxelts;
      set_of_maxelts.vector_set = std::move (maxelts);

      return set_of_maxelts;
    }

    bool empty () {
      return vector_set.empty ();
    }

    void union_with (const set_set& other) {
      size_t size = vector_set.size ();
      vector_set.insert(other.vector_set.cbegin (), other.vector_set.cend ());
      _updated = _updated or size != vector_set.size ();
    }

    void intersect_with (const set_set& other) {
      std::set<Vector> intersection;
      std::set_intersection(vector_set.cbegin (), vector_set.cend (),
                            other.vector_set.cbegin (), other.vector_set.cend (),
                            std::inserter (intersection, intersection.begin()));
      if (intersection.size () != vector_set.size ()) {
        _updated = true;
        vector_set = std::move (intersection);
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
    friend std::ostream& ::operator<<(std::ostream& os, const set_set<V>& f);
};

template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os, const set_set<Vector>& f)
{
  for (auto el : f.vector_set)
    os << el << std::endl;

  return os;
}
