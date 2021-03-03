#pragma once

#include <cassert>
#include <iostream>
#include <vector>

#include "utils/kdtree.hh"

namespace set {
  // Forward definition for the operator<<s.
  template <typename>
  class kdtree_set;
}

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set::kdtree_set<Vector>& f);

namespace set {

  template <typename Vector>
  class kdtree_set {
    private:
      std::shared_ptr<utils::kdtree<Vector>> tree;
      bool _updated = false;

      template <typename V>
      friend std::ostream& ::operator<<(std::ostream& os, const kdtree_set<V>& f);

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

      kdtree_set () = delete;

      kdtree_set (std::vector<Vector>&& elements) noexcept {
        assert (elements.size() > 0);
        this->tree = std::make_shared<utils::kdtree<Vector>>(std::move (elements), elements[0].size());
      }

      kdtree_set (Vector&& el) {
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

        return kdtree_set (std::move (ss));
      }


      kdtree_set (const kdtree_set& other) : tree(other.tree) {}

      kdtree_set& operator= (kdtree_set&& other) {
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

      bool operator== (const kdtree_set& other) const {
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
      void union_with (const kdtree_set& other) {
        std::vector<Vector> result;
        result.reserve (this->size () + other.size ());
        // for all elements in this tree, if they are not strictly
        // dominated by the other tree, we keep them
        for (auto eit = this->tree->begin(); eit != this->tree->end(); ++eit) {
          auto& e = *eit;
          if (!other.tree->dominates(e, true))
            result.push_back(std::move (e)); // this requires a copy
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
        this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (result), result[0].size());
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
      void intersect_with (const kdtree_set& other) {
        std::vector<Vector> intersection;
        bool smaller_set = false;
        using cache_red_dim = std::set<std::reference_wrapper<const Vector>,
                                       disregard_first_component<std::reference_wrapper<const Vector>>>;
        using vector_of_vectors = std::vector<std::reference_wrapper<const Vector>>;
        std::map<int, std::pair<cache_red_dim, vector_of_vectors>> split_cache;

        for (auto xit = this->tree->begin();
             xit != this->tree->end(); ++xit) {
          auto& x = *xit;
          bool dominated = false;

          auto& cv = ([&x, &split_cache, &other] () -> auto& {
            try {
              return split_cache.at (x[0]);
            } catch (...) {
              auto& cv = split_cache[x[0]];
              for (auto yit = other.tree->begin();
                   yit != other.tree->end(); ++yit) {
                auto&& y = *yit;
                if (y[0] >= x[0])
                  cv.first.insert (std::ref (y));
                else
                  cv.second.push_back (std::ref (y));
              }
              return cv;
            }
          }) ();

          auto meet = [&] (std::reference_wrapper<const Vector> y) {
            Vector &&v = x.meet (y);
            if (v == x)
              dominated = true;
            intersection.emplace_back (std::move (v));
            return not dominated;
          };

          std::all_of (cv.first.begin (), cv.first.end (), meet);
          if (!dominated)
            std::all_of (cv.second.begin (), cv.second.end (), meet);

          // If x wasn't <= an element in other, then x is not in the
          // intersection, thus the set is updated.
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

        this->tree = std::make_shared<utils::kdtree<Vector>> (std::move (vector_antichain),
                                                              vector_antichain[0].size ());
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
inline std::ostream& operator<<(std::ostream& os, const set::kdtree_set<Vector>& f) {

  os << *(f.tree) << std::endl;

  return os;
}
