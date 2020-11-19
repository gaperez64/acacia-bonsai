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
      set (unsigned vector_size) : vector_size {vector_size} {}

      set (const set& other) :
        vector_size{other.vector_size},
        vector_set{other.vector_set} {}

      set& operator= (set&& other) {
        vector_set = std::move (other.vector_set);
        vector_size = other.vector_size;

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

      void insert (const vector& v) {
        if (vector_set.insert (v).second)
          _updated = true;
      }

      // Extraordinarily wasteful.  This computes the closure by taking
      // vector_set, then everything at distance 1
      void downward_close (int max) {
        std::set<vector> closure (vector_set);

        while (1) {
          std::set<vector> newelts;

          for (auto el : closure) { // Iterate while making copies.
            for (size_t i = 0; i < vector_size; ++i)
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

      // Extraordinarily wasteful.  This checks that for every member of
      // vector_set, all vectors at Hamming distance 1 are also in the set.
      bool is_downward_closed (int max) const {
        for (auto el : vector_set) { // Iterate while making copies.
          for (size_t i = 0; i < vector_size; ++i)
            if (el[i] > -1) {
              --el[i];
              if (not contains (el))
                return false;
              ++el[i];
            }
        }
        return true;
      }

      set max_elements (int max) const {
        assert (is_downward_closed (max));
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
            maxelts.insert (vector (el));
        }
        set set_of_maxelts (vector_size);
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
      unsigned vector_size;
      std::set<vector> vector_set;
      bool _updated = false;
      friend std::ostream& ::operator<<(std::ostream& os, const set& f);
  };
}
