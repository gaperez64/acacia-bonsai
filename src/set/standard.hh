#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

namespace set {
  template <typename Vector>
  class standard {
    public:
      typedef Vector value_type;

      standard () {}

      standard (const standard&) = delete;
      standard (standard&&) = default;

      standard (Vector&& v) {
        insert (std::move (v));
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

      bool insert (Vector&& v) {
        if (vector_set.insert (std::move (v)).second) {
          _updated = true;
          return true;
        }
        return false;
      }

      // Extraordinarily wasteful.  This computes the closure by taking
      // vector_set, then everything at distance 1
      void downward_close () {
        auto old_size = vector_set.size ();

        while (1) {
          std::list<Vector> newelts;

          for (auto& el : vector_set) { // Iterate while making copies.
            for (size_t i = 0; i < el.size (); ++i)
              if (el[i] > -1) {
                Vector v = el.copy ();
                v[i] -= 1;
                if (vector_set.find (el) == vector_set.end ())
                  newelts.push_back (std::move (v));
              }
          }
          if (newelts.empty ())
            break;
          for (auto& el : newelts)
            vector_set.insert (std::move (el));
        }

        if (vector_set.size () != old_size)
          _updated = true;
      }

      standard max_elements () const {
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
              to_remove.insert (maxelt.copy ());
            }
          }
          for (const auto& elt : to_remove)
            maxelts.erase (elt);
          if (should_be_inserted)
            maxelts.insert (el.copy ());
        }
        standard of_maxelts;
        of_maxelts.vector_set = std::move (maxelts);

        return of_maxelts;
      }

      bool empty () {
        return vector_set.empty ();
      }

      void union_with (const standard& other) {
        size_t size = vector_set.size ();
        for (auto&& el : other)
          vector_set.insert (el.copy ());
        _updated = _updated or size != vector_set.size ();
      }

      void intersect_with (const standard& other) {
        std::list<std::reference_wrapper<const Vector>> intersection;
        std::set_intersection(vector_set.cbegin (), vector_set.cend (),
                              other.vector_set.cbegin (), other.vector_set.cend (),
                              std::inserter (intersection, intersection.begin()));
        if (intersection.size () != vector_set.size ()) {
          _updated = true;
          vector_set.clear ();
          for (auto& r : intersection)
            vector_set.insert (r.get ().copy ());
        }
      }

      template <typename F>
      void apply_inplace (const F& lambda) {
        std::set<Vector> new_set;
        for (auto&& el : vector_set) {
          auto&& changed_el = lambda (el);
          if (changed_el != el)
            _updated = true;
          new_set.insert (changed_el);
        }
        vector_set = std::move (new_set);
      }

      template <typename F>
      standard apply (const F& lambda) const {
        standard res;
        for (const auto& el : vector_set)
          res.insert (lambda (el));
        return res;
      }

      auto        begin ()       { return vector_set.begin (); }
      const auto  begin () const { return vector_set.begin (); }
      auto        end ()         { return vector_set.end (); }
      const auto  end () const   { return vector_set.end (); }

    private:
      std::set<Vector> vector_set;
      bool _updated = false;
  };
}

template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os, const set::standard<Vector>& f)
{
  for (auto&& el : f)
    os << el << std::endl;

  return os;
}
