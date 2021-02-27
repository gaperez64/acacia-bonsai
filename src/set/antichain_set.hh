#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

namespace set {
  template <typename Vector>
  class antichain_set {
    public:
      typedef Vector value_type;

    private:
      antichain_set () {}

    public:
      antichain_set (Vector&& v) noexcept {
        insert (std::move (v));
      }

      antichain_set (const antichain_set&) = delete;
      antichain_set (antichain_set&&) = default;

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
        reference_wrapper_set<const Vector> to_remove;
        bool should_be_inserted = true;

        for (const auto& e : vector_set) {
          auto po = v.partial_order (e);
          if (po.leq ()) {
            should_be_inserted = false;
            break;
          } else if (po.geq ()) {
            should_be_inserted = true;
            to_remove.insert (std::cref (e));
          }
        }

        for (const auto& elt : to_remove)
          vector_set.erase (elt.get ());

        if (should_be_inserted) {
          vector_set.insert (std::move (v));
          _updated = true;
        }

        return should_be_inserted;
      }

      void downward_close () {
        // nil
      }

      const antichain_set& max_elements () const {
        return *this;
      }

      bool empty () {
        return vector_set.empty ();
      }

      void union_with (antichain_set&& other) {
        for (auto it = other.begin (); it != other.end (); /* in-body */)
          _updated |= insert (std::move (other.vector_set.extract (it++).value ()));
      }

      void intersect_with (const antichain_set& other) {
        antichain_set intersection;
        bool smaller_set = false;

        for (const auto& x : vector_set) {
          bool dominated = false;

          for (const auto& y : other.vector_set) {
            Vector &&v = x.meet (y);
            intersection.insert (std::move (v));
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
          this->vector_set = std::move (intersection.vector_set);
          _updated = true;
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
      antichain_set apply (const F& lambda) const {
        antichain_set res;
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
std::ostream& operator<<(std::ostream& os, const set::antichain_set<Vector>& f)
{
  for (auto&& el : f)
    os << el << std::endl;

  return os;
}
