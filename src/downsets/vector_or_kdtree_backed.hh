#pragma once

#include <cassert>
#include <iostream>
#include <math>
#include <vector>

#include <utils/vector_mm.hh>

#include <kdtree_backed.hh>
#include <vector_backed.hh>

namespace downsets {
  // Forward definition for the operator<<s.
  template <typename>
  class vector_or_kdtree_backed;

  template <typename Vector>
  std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<Vector>& f);

  template <typename Vector>
  class vector_or_kdtree_backed {
    private:
      std::shared_ptr<vector_backed<Vector>> vector;
      std::shared_ptr<kdtree_backed<Vector>> kdtree;

      template <typename V>
      friend std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<V>& f);

    public:
      typedef Vector value_type;

      vector_or_kdtree_backed () = delete;

      vector_or_kdtree_backed (std::vector<Vector>&& elements) noexcept {
        assert (elements.size() > 0);
        size_t m = elements.size ();
        size_t dim = elements[0].size ();
        if (exp (dim) < m)
          this->tree = std::make_shared<kdtree_backed<Vector>> (elements);
        else
          this->vector = std::make_shared<vector_backed<Vector>> (elements);
      }

      vector_or_kdtree_backed (Vector&& el) {
        // too small, just use a vector
        this->vector = std::make_shared<vector_backed<Vector>> (v);
      }

      template <typename F>
      auto apply (const F& lambda) const {
        std::vector<Vector> backing_vector;
        if (this->tree != nullptr)
          backing_vector = this->tree.vector_set;
        else
          backing_vector = this->vector.vector_set;
        std::vector<Vector> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return vector_or_kdtree_backed (std::move (ss));
      }

      vector_or_kdtree_backed (const vector_or_kdtree_backed&) = delete;
      vector_or_kdtree_backed (vector_or_kdtree_backed&&) = default;
      vector_or_kdtree_backed& operator= (const vector_or_kdtree_backed&) = delete;
      vector_or_kdtree_backed& operator= (vector_or_kdtree_backed&&) = default;

      bool contains (const Vector& v) const {
        if (this->tree != nullptr)
          return this->tree.contains (v);
        else
          return this->vector.contains (v);
      }

      // TODO: left off here, going down from here

      /* Union in place
       *
       * Worst-case complexity: if the size of this antichain is n, and that
       * of the antichain from other m, then we do n domination queries in
       * other and m domination queries here. Finally, we build the kdtree
       * for the result which is at most n + m in size. Hence:
       * O( n * dim * m^(1-1/dim) +
       *    m * dim * n^(1-1/dim) +
       *    dim * (n+m) * lg(n+m) )
       */
      void union_with (const kdtree_backed& other) {
        std::vector<Vector> result;
        result.reserve (this->size () + other.size ());
        // for all elements in this tree, if they are not strictly
        // dominated by the other tree, we keep them
        for (auto eit = this->tree->begin(); eit != this->tree->end(); ++eit) {
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
        size_t size_before_move = result[0].size ();
        this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (result), size_before_move);
      }

      /* Intersection in place
       *
       * Worst-case complexity: We again write n for the size of this
       * antichain and m for the size of that of other. Some heuristics will
       * be applied but, in essence, we take the meet of all pairs of
       * elements one from each antichain. Then, we construct a kdtree on
       * that set (not necessarily an antichain!). Finally, using the
       * previous tree we compute the antichain and build a kdtree for it.
       * Hence:
       * O( n * m +
       *    dim * n * m * lg(n * m) +
       *    n * m * dim * (nm)^(1-1/dim) )
       */
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
          // the minima of the set is updated
          smaller_set |= not dominated;
        }

        assert (intersection.size() > 0);
        utils::kdtree<Vector> temp_tree(std::move (intersection), intersection[0].size());
        std::vector<std::reference_wrapper<Vector>> inter_antichain;
        for (auto& e : temp_tree) {
          if (not temp_tree.dominates(e, true))
            inter_antichain.push_back (std::ref (e));
        }
        auto vector_antichain = std::vector<Vector> ();
        vector_antichain.reserve (inter_antichain.size ());
        for (auto r : inter_antichain)
          vector_antichain.push_back (std::move (r.get ()));

        size_t dim_before_move = vector_antichain[0].size ();
        this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (vector_antichain),
                                                              dim_before_move);
        assert (dim_before_move > 0);
        assert (this->tree->is_antichain ());
      }

      auto size () const {
        return this->tree->size ();
      }

      auto        begin ()       { return this->tree->begin (); }
      const auto  begin () const { return this->tree->begin (); }
      auto        end ()         { return this->tree->end (); }
      const auto  end () const   { return this->tree->end (); }
  };

  template <typename Vector>
  inline std::ostream& operator<<(std::ostream& os, const kdtree_backed<Vector>& f) {
    os << *(f.tree) << std::endl;
    return os;
  }
}
