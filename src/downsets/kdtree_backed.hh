#pragma once

#include <cassert>
#include <iostream>
#include <math.h>
#include <memory>
#include <vector>

#include <utils/vector_mm.hh>
#include <utils/kdtree.hh>

namespace downsets {
  // Forward definition for the operator<<s.
  template <typename>
  class kdtree_backed;

  template <typename Vector>
  std::ostream& operator<<(std::ostream& os, const kdtree_backed<Vector>& f);

  // Another forward def to have friend status
  template <typename Vector>
  class vector_or_kdtree_backed;

  // Finally the actual class definition
  template <typename Vector>
  class kdtree_backed {
    private:
      std::shared_ptr<utils::kdtree<Vector>> tree;

      template <typename V>
      friend std::ostream& operator<<(std::ostream& os, const kdtree_backed<V>& f);

      template <typename V>
      class disregard_first_component {
        public:
          bool operator() (const V& v1, const V& v2) const {
            utils::vector_mm<typename Vector::value_type> v (v1.get ().size ());
            v.reserve (Vector::capacity_for (v.size ()));
            v2.get ().to_vector (v);
            v[0] = v1.get ()[0];
            v.resize (v2.get ().size ());
            return v1.get () < Vector (v);
          }
      };

    public:
      typedef Vector value_type;

      kdtree_backed () = delete;

      kdtree_backed (std::vector<Vector>&& elements) noexcept {
        assert (elements.size() > 0);
        this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (elements));
        std::vector<Vector> antichain;
        antichain.reserve (this->size ());

        // for all elements in this tree, if they are not strictly
        // dominated by the tree, we keep them
        bool removed = false;
        for (auto eit = this->tree->begin (); eit != this->tree->end (); ++eit) {
          auto& e = *eit;
          if (this->tree->dominates (e, true))
            removed = true;
          else
            antichain.push_back(e.copy ()); // this does requires a copy
        }
        if (removed) {
          assert (antichain.size () > 0);
          this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (antichain));
        }

      }

      kdtree_backed (Vector&& el) {
        auto v = std::vector<Vector> ();
        v.push_back (std::move (el));
        this->tree = std::make_shared<utils::kdtree<Vector>>(std::move (v));
      }

      template <typename F>
      auto apply (const F& lambda) const {
        const auto& backing_vector = this->tree->vector_set;
        std::vector<Vector> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return kdtree_backed (std::move (ss));
      }

      kdtree_backed (const kdtree_backed&) = delete;
      kdtree_backed (kdtree_backed&&) = default;
      kdtree_backed& operator= (const kdtree_backed&) = delete;
      kdtree_backed& operator= (kdtree_backed&&) = default;

      bool contains (const Vector& v) const {
        return this->tree->dominates(v);
      }

      // Union in place
      void union_with (const kdtree_backed& other) {
        assert (other.size () > 0);
        std::vector<Vector> result;
        result.reserve (this->size () + other.size ());
        // for all elements in this tree, if they are not strictly
        // dominated by the other tree, we keep them
        for (auto eit = this->tree->begin (); eit != this->tree->end (); ++eit) {
          auto& e = *eit;
          if (!other.tree->dominates(e, true))
            result.push_back(e.copy ()); // this does requires a copy
        }
        // for all elements in the other tree, if they are not dominated
        // (not necessarily strict) by this tree, we keep them
        for (auto eit = other.tree->begin(); eit != other.tree->end(); ++eit) {
          auto& e = *eit;
          if (!this->tree->dominates (e))
            result.push_back (std::move (e));
        }
        assert (result.size () > 0);
        this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (result));
      }

      // Intersection in place
      void intersect_with (const kdtree_backed& other) {
        std::vector<Vector> intersection;
        bool smaller_set = false;

        for (auto xit = this->tree->begin ();
             xit != this->tree->end ();
             ++xit) {
          auto& x = *xit;
          assert (x.size () > 0);

          // If x is part of the set of all meets, then x will dominate the
          // whole list! So we use this to short-circuit the computation: we
          // first check whether x will be there (which happens only if it is
          // itself dominated by some element in other)
          bool dominated = other.tree->dominates (x);
          if (dominated) {
            intersection.push_back (x.copy ());
          } else {
            for (auto yit = other.begin (); yit != other.end (); ++yit) {
              Vector &&v = x.meet (*yit);
              intersection.push_back (v.copy ());
            }
          }

          // If x wasn't in the set of meets, dominated is false and
          // the set of minima is different than what is in this->tree
          smaller_set |= not dominated;
        }

        // We can skip building trees and all if this->tree is the antichain
        // of minimal elements
        if (!smaller_set)
          return;

        // Worst-case scenario: we do need to build trees
        assert (intersection.size () > 0);
        auto vector_antichain = std::vector<Vector*> ();
        vector_antichain.reserve (intersection.size ());
        utils::kdtree<Vector> temp_tree(std::move (intersection));
        for (Vector& e : temp_tree) {
          if (not temp_tree.dominates(e, true)) {
            vector_antichain.push_back (&e);
          }
        }
        struct proj {
            const Vector& operator() (const Vector* pv) { return *pv; }
            Vector&& operator() (Vector*&& pv)          { return std::move (*pv); }
        };
        this->tree = std::make_shared<utils::kdtree<Vector>> (
          std::move (vector_antichain), proj ());
        assert (this->tree->is_antichain ());
      }

      auto size () const {
        return this->tree->size ();
      }

      auto        begin ()       { return this->tree->begin (); }
      const auto  begin () const { return this->tree->begin (); }
      auto        end ()         { return this->tree->end (); }
      const auto  end () const   { return this->tree->end (); }

      friend class vector_or_kdtree_backed<Vector>;
  };

  template <typename Vector>
  inline std::ostream& operator<<(std::ostream& os, const kdtree_backed<Vector>& f) {
    os << *(f.tree) << std::endl;
    return os;
  }
}
