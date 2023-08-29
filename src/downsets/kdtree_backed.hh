#pragma once

#include <cassert>
#include <iostream>
#include <vector>

#include <utils/vector_mm.hh>
#include <utils/kdtree.hh>

namespace downsets {
  // Forward definition for the operator<<s.
  template <typename>
  class kdtree_backed;

  template <typename Vector>
  std::ostream& operator<<(std::ostream& os, const kdtree_backed<Vector>& f);

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
        this->tree = std::make_shared<utils::kdtree<Vector>>(std::move (elements), elements[0].size());
      }

      kdtree_backed (Vector&& el) {
        auto v = std::vector<Vector> ();
        size_t dim = el.size ();
        v.push_back (std::move (el));
        this->tree = std::make_shared<utils::kdtree<Vector>>(std::move (v), dim);
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
