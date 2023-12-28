#pragma once

#include <algorithm>
#include <vector>
#include <iostream>
#include <cassert>

namespace downsets {
  // A forward definition to allow for friend status
  template <typename Vector>
  class vector_or_kdtree_backed;

  template <typename Vector>
  class vector_backed {
    public:
      typedef Vector value_type;

      vector_backed (Vector&& v) {
        insert (std::move (v));
      }

      vector_backed (std::vector<Vector>&& elements) noexcept {
        assert (elements.size() > 0);
        for (auto&& e : elements)
          insert (std::move (e));
      }

    private:
      vector_backed () = default;
      std::vector<Vector> vector_set;

    public:
      vector_backed (const vector_backed&) = delete;
      vector_backed (vector_backed&&) = default;
      vector_backed& operator= (vector_backed&&) = default;
      vector_backed& operator= (const vector_backed&) = delete;

      bool operator== (const vector_backed& other) = delete;

      bool contains (const Vector& v) const {
        for (const auto& e : vector_set)
          if (v.partial_order (e).leq ())
            return true;
        return false;
      }

      auto size () const {
        return vector_set.size ();
      }

      bool insert (Vector&& v) {
        bool must_remove = false;

        // This is like remove_if, but allows breaking.
        auto result = vector_set.begin ();
        auto end = vector_set.end ();

        for (auto it = result; it != end; ++it) {
	  auto res = v.partial_order (*it);
	  if (not must_remove and res.leq ()) { // v is dominated.
	    // if must_remove is true, since we started with an antichain,
	    // it's not possible that res.leq () holds.  Hence we don't check for
	    // leq if must_remove is true.
	    return false;
	  } else if (res.geq ()) { // v dominates *it
	    must_remove = true; /* *it should be removed */
	  } else { // *it needs to be kept
	    if (result != it) // This can be false only on the first element.
	      *result = std::move (*it);
	    ++result;
	  }
        }

        if (result != vector_set.end ())
	  vector_set.erase (result, vector_set.end ());
        vector_set.push_back (std::move (v));
        return true;
      }

      void union_with (vector_backed&& other) {
        for (auto&& e : other.vector_set)
          insert (std::move (e));
      }

      void intersect_with (const vector_backed& other) {
        vector_backed intersection;
        bool smaller_set = false;

        for (const auto& x : vector_set) {
          bool dominated = false;

          for (auto& y : other.vector_set) {
            Vector v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.insert (std::move (v));
            if (dominated)
              break;
          };
          // If x wasn't <= an element in other, then x is not in the
          // intersection, thus the set is updated.
          smaller_set |= not dominated;
        }

        if (smaller_set)
          this->vector_set = std::move (intersection.vector_set);
      }

      template <typename F>
      vector_backed apply (const F& lambda) const {
        vector_backed res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      auto        begin ()      { return vector_set.begin (); }
      const auto  begin() const { return vector_set.begin (); }
      auto        end()         { return vector_set.end (); }
      const auto  end() const   { return vector_set.end (); }

      friend class vector_or_kdtree_backed<Vector>;
  };


  template <typename Vector>
  inline
  std::ostream& operator<<(std::ostream& os, const vector_backed<Vector>& f)
  {
    for (auto&& el : f)
      os << el << std::endl;

    return os;
  }
}
