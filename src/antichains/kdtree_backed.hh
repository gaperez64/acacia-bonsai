#pragma once

#include <cassert>
#include <iostream>
#include <vector>

#include "utils/kdtree.hh"

namespace antichains {
  // Forward definition for the operator<<s.
  template <typename>
  class kdtree_backed;
}

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const antichains::kdtree_backed<Vector>& f);

namespace antichains {

  template <typename Vector>
  class kdtree_backed {
    private:
      std::shared_ptr<utils::kdtree<Vector>> tree;
      bool _updated = false;

      template <typename V>
      friend std::ostream& ::operator<<(std::ostream& os, const kdtree_backed<V>& f);

      template <typename V>
      class disregard_first_component {
        public:
          bool operator() (const V& v1, const V& v2) const {
            auto v2prime = v2.get ().copy ();
            v2prime[0] = v1.get ()[0];
            return v1.get () < v2prime;
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
        // FUCK YOU and your std::moves Cadilhac, because you move el
        // below, we have to first save the dimension to use it later
        size_t dim = el.size ();
        v.push_back (std::move (el));
        this->tree = std::make_shared<utils::kdtree<Vector>>(std::move (v), dim);
      }

      auto max_elements () const {
        return *this;
      }

      void downward_close () {
        // nil
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


      kdtree_backed (const kdtree_backed& other) : tree(other.tree) {}

      kdtree_backed& operator= (kdtree_backed&& other) {
        this->tree = other.tree;
        return *this;
      }

      bool contains (const Vector& v) const {
        return this->tree->dominates(v);
      }

      void clear_update_flag () {
        this->_updated = false;
      }

      bool updated () {
        return this->_updated;
      }

      bool operator== (const kdtree_backed& other) const {
        return this->tree == other.tree;
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
          else
            _updated = true;
        }
        // for all elements in the other tree, if they are not dominated
        // (not necessarily strict) by this tree, we keep them
        for (auto eit = other.tree->begin(); eit != other.tree->end(); ++eit) {
          auto& e = *eit;
          if (!this->tree->dominates(e))
            result.push_back (std::move (e));
          else
            _updated = true;
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
        using cache_red_dim = std::set<std::reference_wrapper<const Vector>,
                                       disregard_first_component<std::reference_wrapper<const Vector>>>;
        using vector_of_vectors = std::vector<std::reference_wrapper<const Vector>>;
        std::map<int, std::pair<cache_red_dim, vector_of_vectors>> split_cache;

        for (auto xit = this->tree->begin ();
             xit != this->tree->end ();
             ++xit) {
          auto& x = *xit;
          assert (x.size () > 0);
          bool dominated = false;

          /* The madness below is meant to split all elements in the other
           * tree based on the value of the first dimension of x. We store in
           * a map a pair consisting of:
           * 1. (a set) all the elements y from the other tree
           *    such that y[0] >= x[0]
           * 2. a vector of all other elements y from the other
           *    tree
           * Why is the second one a vector and the first a set? If the
           * condition y[0] >= x[0] holds then x[0] will replace y[0] when
           * taking the minimum so the meet of y, y' and x is different only
           * if y, y' differ in some other dimension besides 0
           */
          auto& cv = ([&x, &split_cache, &other] () -> auto& {
            try {
              return split_cache.at (x[0]);
            } catch (...) {
              auto& cv = split_cache[x[0]];
              for (auto yit = other.tree->begin();
                   yit != other.tree->end (); ++yit) {
                auto&& y = *yit;
                if (y[0] >= x[0])
                  cv.first.insert (std::ref (y));
                else
                  cv.second.push_back (std::ref (y));
              }
              return cv;
            }
          }) ();

          // We mean to traverse the keys of the map and compute their meet
          // with x, so we will need the following definition of meet
          auto meet = [&] (std::reference_wrapper<const Vector> y) {
            Vector &&v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.emplace_back (std::move (v));
            return not dominated;
          };

          // If x is part of the set of all meets, then x will dominate the
          // whole list! So we use this to short-circuit the computation
          std::all_of (cv.first.begin (), cv.first.end (), meet);
          if (!dominated)
            std::all_of (cv.second.begin (), cv.second.end (), meet);

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
          else
            _updated = true;
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

      bool empty () {
        return this->tree->empty ();
      }

      auto        begin ()       { return this->tree->begin (); }
      const auto  begin () const { return this->tree->begin (); }
      auto        end ()         { return this->tree->end (); }
      const auto  end () const   { return this->tree->end (); }
  };
}

template <typename Vector>
inline std::ostream& operator<<(std::ostream& os, const antichains::kdtree_backed<Vector>& f) {
  os << *(f.tree) << std::endl;
  return os;
}
