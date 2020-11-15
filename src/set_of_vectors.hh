#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>

// Forward definitions for the operator<<s.
namespace set_of_vectors {
  typedef std::vector<int> vector;
  class set;
}

std::ostream& operator<<(std::ostream& os, const set_of_vectors::set& f);
std::ostream& operator<<(std::ostream& os, const set_of_vectors::vector& v);

namespace set_of_vectors {
  class set {
    public:
      set (unsigned vector_size) : vector_size {vector_size} {}

      bool contains (const vector& v) {
        return vector_set.find(v) != vector_set.end();
      }

      void clear_update_flag () {
        _updated = false;
      }

      bool updated () {
        return _updated;
      }

      auto size () {
        return vector_set.size ();
      }

      void insert (const vector& v) {
        if (vector_set.insert (v).second)
          _updated = true;
      }

      // Extraordinarily wasteful, but only called once.
      void upward_close (int max) {
        std::set<vector> closure;

        for (auto el : vector_set) { // Iterate while making copies.
          closure.insert (el);
          while (1) {
            size_t i;
            for (i = 0; i < vector_size; ++i)
              if (el[i] < max) {
                ++el[i];
                break;
              }
            if (i == vector_size)
              break;
            closure.insert (el);
          }
        }
        if (vector_set.size () != closure.size ()) {
          _updated = true;
          vector_set = closure;
        }
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
                              std::inserter (intersection,intersection.begin()));
        if (intersection.size () != vector_set.size ()) {
          _updated = true;
          vector_set = intersection;
        }
      }

      template <typename F>
      void apply (const F& lambda) {
        std::set<vector> new_set;
        for (auto el : vector_set)
          new_set.insert (lambda (el));
        _updated = true;
        vector_set = new_set;
      }



    private:
      const unsigned vector_size;
      std::set<vector> vector_set;
      bool _updated = false;
      friend std::ostream& ::operator<<(std::ostream& os, const set& f);
  };
}
