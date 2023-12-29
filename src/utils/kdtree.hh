#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <ranges>
#include <memory>
#include <numeric>
#include <stack>
#include <vector>


/*
 * This is Shrisha Rao's version of a kd-tree for variable dimension. The
 * basis for the algorithms comes from the Computational Geometry book
 * by de Berg et al.
 *
 * Coded by Guillermo A. Perez
 */
namespace utils {
  // Forward definition for the operator<<
  template <typename>
  class kdtree;

  template <typename Vector>
  std::ostream& operator<< (std::ostream& os, const utils::kdtree<Vector>& f);

  template <typename Vector>
  class kdtree {
    private:
      struct kdtree_node {
          std::shared_ptr<kdtree_node> left;  // left child
          std::shared_ptr<kdtree_node> right; // right child
          size_t value_idx;                   // only for leaves: the index of
                                              // the element from the list
          int location;                       // the value at which we split
          size_t axis;                        // the dimension at which we
                                              // split
          bool clean_split;                   // whether the split is s.t.
                                              // to the left all is smaller

          // constructor for leaves
          kdtree_node (size_t idx) : left (nullptr),
                                     right (nullptr),
                                     value_idx (idx),
                                     location (0),
                                     axis (0),
                                     clean_split (false) {}
          // constructor for inner nodes
          kdtree_node (std::shared_ptr<kdtree_node> l,
                       std::shared_ptr<kdtree_node> r,
                       int loc, size_t a, bool c) : left (l), right (r),
                                                    location (loc),
                                                    axis (a),
                                                    clean_split (c) {}
      };
      const size_t dim;
      std::shared_ptr<kdtree_node> tree;

      template <typename V>
      friend std::ostream& operator<< (std::ostream& os, const kdtree<V>& f);

      /*
       * This is one of the only interesting parts of the code: building the
       * kd-tree to make sure it is balanced
       */
      std::shared_ptr<kdtree_node>
      recursive_build (std::vector<size_t>::iterator begin_it,
                       std::vector<size_t>::iterator end_it,
                       size_t length, size_t axis) {
        assert (static_cast<size_t>(std::distance (begin_it, end_it)) == length);
        assert (length > 0);
        assert (axis < this->dim && axis >= 0);

        // if the list of elements is now a singleton, we make a leaf
        if (length == 1)
          return std::make_shared<kdtree_node> (*begin_it);

        // Use a selection algorithm to get the median
        // NOTE: we actually get the item whose index is
        auto median_it = begin_it + length / 2;
        std::nth_element (begin_it, median_it, end_it,
          [this, &axis] (size_t i1, size_t i2) {
            return this->vector_set[i1][axis] <
                   this->vector_set[i2][axis];
          });
        size_t median_idx = *median_it;
        int loc = this->vector_set[median_idx][axis];

        // check whether the maximal element on the left is equal to loc
        // (with respect to dimension axis) to determine if the split is clean
        auto max_it = std::max_element (begin_it, median_it,
          [this, &axis] (size_t i1, size_t i2) {
            return this->vector_set[i1][axis] <
                   this->vector_set[i2][axis];
          });
        size_t max_idx = *max_it;
        bool clean = (this->vector_set[max_idx][axis] < loc);

        // some sanity checks
        assert (std::distance (begin_it, median_it) > 0);
        assert (std::distance (median_it, end_it) > 0);
        assert (static_cast<size_t>(std::distance (begin_it, median_it)) == length / 2);
        assert (static_cast<size_t>(std::distance (median_it, end_it)) == length - (length / 2));
        assert (static_cast<size_t>(std::distance (begin_it, median_it)) +
                std::distance (median_it, end_it) == length);
        assert (this->vector_set[max_idx][axis] <= loc);

        // the next axis is just the following dimension, wrapping around
        size_t next_axis = (axis + 1) % this->dim;
        return std::make_shared<kdtree_node> (recursive_build (begin_it, median_it,
                                                               length / 2, next_axis),
                                              recursive_build (median_it, end_it,
                                                               length - (length / 2), next_axis),
                                              loc, axis, clean);
      }

      /*
       * And this is the second interesting piece of code, using the
       * properties of how the kd-tree was constructed to look for an element
       * that dominates a given vector.
       * NOTE: Through the recursive calls we keep track of dim variables
       * which store the lower bounds of the region of the current node and a
       * counter dims_to_dom which records the dimensions on which the current
       * region is not yet dominating the region of v
       */
      bool recursive_dominates (const Vector& v, bool strict,
                                std::shared_ptr<kdtree_node> node,
                                int* lbounds, size_t dims_to_dom) const {
        assert (node != nullptr);
        assert (dims_to_dom > 0);

        // if we are at a leaf, just check if it dominates
        if (node->left == nullptr) {
          auto po = v.partial_order (this->vector_set[node->value_idx]);
          if (strict)
            return po.leq () and not po.geq ();
          else
            return po.leq ();
        }

        // so we're at an inner node, this means the known lower bound for the
        // dimension at which we split for this node must be smaller than the
        // component of the given vector
        assert (lbounds[node->axis] <= v[node->axis]);
        assert (strict || (lbounds[node->axis] < v[node->axis]));

        // let's check if the right subtree
        // is guaranteed to have a dominating vector
        const int old_bound = lbounds[node->axis];
        size_t still_to_dom = dims_to_dom;
        if (node->location > old_bound) {
          if (node->location > v[node->axis]) {
            still_to_dom--;
          } else if (!strict && node->location >= v[node->axis]) {
            still_to_dom--;
          }
          if (still_to_dom == 0) return true;
          lbounds[node->axis] = node->location;
        }
        // if we got here, we need to check on the right recursively
        bool r_succ = recursive_dominates (v, strict, node->right, lbounds, still_to_dom);
        if (r_succ) return true;
        // all that's left is to check on the left recursively, if pertinent
        lbounds[node->axis] = old_bound;
        if (v[node->axis] > node->location ||
            (v[node->axis] == node->location && node->clean_split)) {
          return false;
        }
        // it is pertinent after all
        return recursive_dominates (v, strict, node->left, lbounds, dims_to_dom);
      }

    public:
      std::vector<Vector> vector_set;

      // NOTE: this works for any collection of vectors, not even set assumed
      template <std::ranges::input_range R, class Proj = std::identity>
      kdtree (R&& elements, Proj proj = {}) : dim (proj (elements[0]).size ()) {
        assert (elements.size () > 0);
        assert (this->dim > 0);
        vector_set.reserve (elements.size ());
        for (ssize_t i = elements.size () - 1; i >= 0; --i) {
          this->vector_set.push_back (proj (std::move (elements[i])));
        }

        // WARNING: moved elements, so we can't really use it below! instead,
        // use this->vector_set
        assert (this->vector_set.size () > 0);

        // We now prepare the list of indices to include in the tree
        std::vector<size_t> points (this->vector_set.size ());
        std::iota(points.begin (), points.end (), 0);

        this->tree = recursive_build (points.begin (), points.end (),
                                      points.size (), 0);
      }

      kdtree (const kdtree& other) = delete;
      kdtree (kdtree&& other) = default;
      kdtree& operator= (kdtree&& other) = default;

      bool dominates (const Vector& v, bool strict = false) const {
        int lbounds[this->dim];
        std::fill_n (lbounds, this->dim, std::numeric_limits<int>::min ());
        return this->recursive_dominates (v, strict, this->tree, lbounds, this->dim);
      }

      bool is_antichain () const {
        for (auto it = this->begin (); it != this->end (); ++it) {
          for (auto it2 = it + 1; it2 != this->end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ()) {
              return false;
            }
          }
        }
        return true;
      }
      bool operator== (const kdtree& other) const {
        return this->vector_set == other->vector_set;
      }
      auto size () const {
        return this->vector_set.size ();
      }
      bool empty () {
        return this->vector_set.empty ();
      }
      auto begin () {
        return this->vector_set.begin ();
      }
      const auto begin () const {
        return this->vector_set.begin ();
      }
      auto end () {
        return this->vector_set.end ();
      }
      const auto end () const {
        return this->vector_set.end ();
      }
  };

  template <typename Vector>
  inline std::ostream& operator<< (std::ostream& os, const kdtree<Vector>& f) {
    for (auto&& el : f.vector_set)
      os << el << std::endl;

    return os;
  }
}
