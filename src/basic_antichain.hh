#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

// Forward definitions for the operator<<s.
namespace basic_antichain {
  class vector;
  class set;
}

std::ostream& operator<<(std::ostream& os, const basic_antichain::set& f);
std::ostream& operator<<(std::ostream& os, const basic_antichain::vector& v);

namespace basic_antichain {

  class vector : public std::vector<int> {
    public:
      vector (unsigned int k) : std::vector<int> (k) {}
      // Disallow creating a vector with no dimension.
      vector () = delete;

      vector (const vector& other) : std::vector<int> (other) {}

      bool partial_leq (const vector& rhs) const {
        for (unsigned i = 0; i < rhs.size (); ++i)
          if (not ((*this)[i] <= rhs[i]))
            return false;
        return true;
      }

      bool partial_lt (const vector& rhs) const {
        return rhs != *this && partial_leq (rhs);
      }

      vector meet (const vector& rhs) const {
        vector res (this->size ());

        for (unsigned i = 0; i < rhs.size (); ++i)
          res[i] = std::min ((*this)[i], rhs[i]);
        return res;
      }
  };

  class set {
    public:
      set () {}

      set (const set& other) :
        vector_set{other.vector_set} {}

      set& operator= (set&& other) {
        vector_set = std::move (other.vector_set);

        return *this;
      }

      bool contains (const vector& v) const {
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

      bool insert (const vector& v) {
        std::set<vector> to_remove;
        bool should_be_inserted = true;

        for (const auto& e : vector_set) {
          if (v.partial_leq (e)) {
            should_be_inserted = false;
            break;
          } else if (e.partial_lt (v)) {
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

      const set& max_elements () const {
        return *this;
      }

      bool empty () {
        return vector_set.empty ();
      }

      void union_with (const set& other) {
        for (const auto& e : other.vector_set)
          _updated |= insert (e);
      }

      void intersect_with (const set& other) {
        set intersection;

        for (const vector& x : vector_set)
          for (const vector& y : other.vector_set)
            intersection.insert (x.meet (y));
        if (this->vector_set != intersection.vector_set) {
          *this = std::move (intersection);
          _updated = true;
        }
      }

      template <typename F>
      void apply (const F& lambda) {
        std::set<vector> new_set;
        for (auto el : vector_set) {
          auto&& changed_el = lambda (el);
          if (changed_el != el)
            _updated = true;
          new_set.insert (changed_el);
        }
        vector_set = new_set;
      }

    private:
      std::set<vector> vector_set;
      bool _updated = false;
      friend std::ostream& ::operator<<(std::ostream& os, const set& f);
  };
}
