#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

namespace antichains {
  template <typename Vector>
  class vector_backed {
    public:
      typedef Vector value_type;

      vector_backed (Vector&& v) {
        insert (std::move (v));
      }

    private:
      vector_backed () = default;

    public:
      vector_backed (const vector_backed&) = delete;
      vector_backed (vector_backed&&) = default;
      vector_backed& operator= (vector_backed&&) = default;

      bool operator== (const vector_backed& other) = delete;

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

      bool insert (Vector&& v) {
        bool should_be_inserted = true;

        // This is like remove_if, but allows breaking.
        auto result = vector_set.begin ();
        auto end = vector_set.end ();
        for (auto it = result; it != end; ++it) {
          auto res = v.partial_order (*it);
          if (res.leq ()) {
            should_be_inserted = false;
            break;
          } else if (res.geq ()) {
            should_be_inserted = true;
          } else { // Element needs to be kept
            if (result != it) // This can be false only on the first element.
              *result = std::move (*it);
            ++result;
          }
        }

        if (should_be_inserted) {
          vector_set.erase (result, vector_set.end ());
          vector_set.push_back (std::move (v));
          _updated = true;
        }

        return should_be_inserted;
      }

      void downward_close () {
        // nil
      }

      const vector_backed& max_elements () const {
        return *this;
      }

      bool empty () {
        return vector_set.empty ();
      }

      void union_with (vector_backed&& other) {
        for (auto&& e : other.vector_set)
          _updated |= insert (std::move (e));
      }

      template <typename V>
      class disregard_first_component {
        public:
          bool operator() (const V& v1, const V& v2) const {
            auto v2prime = v2.get ().copy ();
            v2prime[0] = v1.get ()[0];
            return v1.get () < v2prime;
          }
      };

      void intersect_with (const vector_backed& other) {
        vector_backed intersection;
        bool smaller_set = false;

        // split_cache maps first-dim p to:
        //   - the set of vectors that have a bigger or equal to p
        //   - the set of other vectors
        // then we go through all vectors in vector_set:
        //    for all vectors in split_cache[x[0]].first, compute the meets, and
        //              put them intersection; if x is dominated, leave.
        //    if x is not dominated, do the same for split_cache[x[0]].second

        // Different wording: we compare x in vector_set with all of
        // other.vector_set, prioritazing the vectors in other.vector_set that
        // have a larger first component (hence can potentially dominate x).

#warning vector_set should simply be a vector of size K with vs[k] == all the elements which start with k.

        using cache_red_dim = std::set<std::reference_wrapper<const Vector>,
                                       disregard_first_component<std::reference_wrapper<const Vector>>>;
        using vector_of_vectors = std::vector<std::reference_wrapper<const Vector>>;
        std::map<int, std::pair<cache_red_dim, vector_of_vectors>> split_cache;

        for (const auto& x : vector_set) {
          bool dominated = false;

          auto& cv = ([&x, &split_cache, &other] () -> auto& {
            try {
              return split_cache.at (x[0]);
            } catch (...) {
              auto& cv = split_cache[x[0]];
              for (const auto& y : other.vector_set) {
                if (y[0] >= x[0])
                  cv.first.insert (std::ref (y));
                else
                  cv.second.push_back (std::ref (y));
              }
              return cv;
            }
          }) ();

          auto meet = [&] (std::reference_wrapper<const Vector> y) {
            Vector &&v = x.meet (y.get ());
            if (v == x)
              dominated = true;
            intersection.insert (std::move (v));
            return not dominated;
          };

          std::all_of (cv.first.begin (), cv.first.end (), meet);
          if (!dominated)
            std::all_of (cv.second.begin (), cv.second.end (), meet);

          // If x wasn't <= an element in other, then x is not in the
          // intersection, thus the set is updated.
          smaller_set |= not dominated;
        }

        if (smaller_set) {
          this->vector_set = std::move (intersection.vector_set);
          _updated = true;
        }
      }

      template <typename F>
      vector_backed apply (const F& lambda) const {
        vector_backed res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      template <typename F>
      void apply_inplace (const F& lambda) {
        std::vector<Vector> new_set;
        for (const auto& el : vector_set) {
          auto&& changed_el = lambda (el);
          if (changed_el != el)
            _updated = true;
          new_set.push_back (std::move (changed_el)); // May not be an antichain, but speeds up.
        }
        vector_set = std::move (new_set);
      }

      void bump (const Vector& v) {
        auto it = std::find (vector_set.begin (), vector_set.end (), v);
        std::rotate (it, std::next (it), vector_set.end ());
      }

      auto        begin ()      { return vector_set.begin (); }
      const auto  begin() const { return vector_set.begin (); }
      auto        end()         { return vector_set.end (); }
      const auto  end() const   { return vector_set.end (); }

    private:
      std::vector<Vector> vector_set;
      bool _updated = false;
  };

}

template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os, const antichains::vector_backed<Vector>& f)
{
  for (auto&& el : f)
    os << el << std::endl;

  return os;
}
