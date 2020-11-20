#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

// Forward definitions for the operator<<s.
namespace set_of_vectors {
  class vector;
  class set;
}

std::ostream& operator<<(std::ostream& os, const set_of_vectors::set& f);
std::ostream& operator<<(std::ostream& os, const set_of_vectors::vector& v);

namespace set_of_vectors {

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

      bool insert (const vector& v) {
        if (vector_set.insert (v).second) {
          _updated = true;
          return true;
        }
        return false;
      }

      // Extraordinarily wasteful.  This computes the closure by taking
      // vector_set, then everything at distance 1
      void downward_close () {
        std::set<vector> closure (vector_set);

        while (1) {
          std::set<vector> newelts;

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

      set max_elements () const {
        assert (is_downward_closed ());
        std::set<vector> maxelts;

        for (const auto& el : vector_set) {
          std::set<vector> to_remove;
          bool should_be_inserted = true;

          for (const auto& maxelt : maxelts) {
            if (el.partial_lt (maxelt)) {
              should_be_inserted = false;
              break;
            } else if (maxelt.partial_lt (el)) {
              should_be_inserted = true;
              to_remove.insert (maxelt);
            }
          }
          for (const auto& elt : to_remove)
            maxelts.erase (elt);
          if (should_be_inserted)
            maxelts.insert (el);
        }
        set set_of_maxelts;
        set_of_maxelts.vector_set = maxelts;

        return set_of_maxelts;
      }

      bool empty () {
        return vector_set.empty ();
      }

      void union_with (const set& other) {
        size_t size = vector_set.size ();
        vector_set.insert(other.vector_set.cbegin (), other.vector_set.cend ());
        _updated = _updated or size != vector_set.size ();
      }

      void intersect_with (const set& other) {
        std::set<vector> intersection;
        std::set_intersection(vector_set.cbegin (), vector_set.cend (),
                              other.vector_set.cbegin (), other.vector_set.cend (),
                              std::inserter (intersection, intersection.begin()));
        if (intersection.size () != vector_set.size ()) {
          _updated = true;
          vector_set = intersection;
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
