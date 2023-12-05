#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
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
  std::ostream& operator<< (std::ostream& os, utils::kdtree<Vector>& f);

  template <typename Vector>
  class kdtree {
    private:
      struct kdtree_node {
          std::shared_ptr<kdtree_node> left;  // left child
          std::shared_ptr<kdtree_node> right; // right child
          bool removed;                       // whether the node has been
                                              // deleted
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
                                     removed (false),
                                     value_idx (idx),
                                     location (0),
                                     axis (0),
                                     clean_split (false) {}
          // constructor for inner nodes
          kdtree_node (std::shared_ptr<kdtree_node> l,
                       std::shared_ptr<kdtree_node> r,
                       int loc, size_t a, bool c) : left (l), right (r),
                                                    removed (false),
                                                    location (loc),
                                                    axis (a),
                                                    clean_split (c) {}
      };
      const size_t dim;
      std::shared_ptr<kdtree_node> tree;

      template <typename V>
      friend std::ostream& operator<< (std::ostream& os, kdtree<V>& f);

      /*
       * This is one of the only interesting parts of the code: building the
       * kd-tree to make sure it is balanced
       */
      std::shared_ptr<kdtree_node>
      recursive_build (std::vector<size_t>::iterator begin_it,
                       std::vector<size_t>::iterator end_it,
                       size_t length, size_t axis) {
        assert (std::distance (begin_it, end_it) == length);
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
        assert (std::distance (begin_it, median_it) == length - (length / 2));
        assert (std::distance (begin_it, median_it) > 0);
        assert (std::distance (median_it, end_it) == length / 2);
        assert (std::distance (median_it, end_it) > 0);
        assert (std::distance (begin_it, median_it) +
                std::distance (median_it, end_it) == length);
        assert (this->vector_set[max_idx][axis] <= loc);

        // the next axis is just the following dimension, wrapping around
        size_t next_axis = (axis + 1) % this->dim;
        return std::make_shared<kdtree_node> (recursive_build (begin_it, median_it,
                                                               length - (length / 2), next_axis),
                                              recursive_build (median_it, end_it,
                                                               length / 2, next_axis),
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

        if (node->removed) {                 // if we reached a dead subtree, return false
          return false;
        }
        if (node->left == nullptr) {         // if we are at a leaf, just check if it dominates
          auto po = v.partial_order (this->vector_set[node->value_idx]);
          if (strict)
            return po.leq () and not po.geq ();
          else
            return po.leq ();
        }
        // so we're at an inner node, let's check if the right subtree
        // is guaranteed to have a dominating vector
        int old_bound = lbounds[node->axis];
        size_t still_to_dom = dims_to_dom;
        if (node->location > lbounds[node->axis]) {
          if (node->location > v[node->axis] &&
              old_bound <= v[node->axis]) {
            still_to_dom--;
          } else if (!strict &&
                     node->location >= v[node->axis] &&
                     old_bound < v[node->axis]) {
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
            (v[node->axis] == node->location && (strict || node->clean_split)))
          return false;
        // it is pertinent after all
        return recursive_dominates (v, strict, node->left, lbounds, dims_to_dom);
      }

      void recursive_trim (const Vector& v, bool strict,
                           std::shared_ptr<kdtree_node> node,
                           int* ubounds, size_t dims_to_dom) {
        assert (node != nullptr);

        if (node->removed) {                 // if we reached a dead subtree, return
          return;
        }
        if (node->left == nullptr) {         // if we are at a leaf, just check if dominated
          auto po = v.partial_order (this->vector_set[node->value_idx]);
          if (strict && po.geq () && !po.leq ()) {
            node->removed = true;
            this->active_set.clear ();
          } else if (po.geq ()) {
            node->removed = true;
            this->active_set.clear ();
          }
          return;
        }
        // so we're at an inner node, let's check if the left subtree
        // is guaranteed to have a dominating vector
        int old_bound = ubounds[node->axis];
        size_t still_to_dom = dims_to_dom;
        if (node->location < ubounds[node->axis]) {
          if (node->location < v[node->axis] &&
              old_bound >= v[node->axis]) {
            still_to_dom--;
          } else if (!strict &&
                     node->location <= v[node->axis] &&
                     old_bound > v[node->axis]) {
            still_to_dom--;
          }
          ubounds[node->axis] = node->location;
          if (still_to_dom == 0) {
            node->left->removed = true;
            this->active_set.clear ();
          } else {  // here we go down recursively
            recursive_trim (v, strict, node->left, ubounds, still_to_dom);
          }
        }
        // all that's left is to check on the right recursively, if pertinent
        ubounds[node->axis] = old_bound;
        if (v[node->axis] < node->location ||
            (v[node->axis] == node->location && (strict)))
          return;
        // it is pertinent after all
        return recursive_trim (v, strict, node->right, ubounds, dims_to_dom);
      }

      std::vector<Vector> vector_set;
      std::vector<Vector> active_set;
    public:

      std::vector<Vector>* getActive() {
        if (this->active_set.empty ()) {
          // this means we need to refresh the active set doing a traversal of
          // the tree
          std::stack<std::shared_ptr<kdtree_node>> to_visit;
          to_visit.push (this->tree);
          while (!to_visit.empty ()) {
            std::shared_ptr<kdtree_node> cur = to_visit.top ();
            to_visit.pop ();
            if (cur->left != nullptr) {
              if (!cur->left->removed) to_visit.push (cur->left);
              if (!cur->right->removed) to_visit.push (cur->right);
            } else {  // it's a leaf!
              if (!cur->removed)
                this->active_set.push_back (this->vector_set[cur->value_idx]);
            }
          }
          assert (!this->active_set.empty ());
        }
        return &(this->active_set);
      }

      // FIXME: can't we assume that it's already a set? an antichain even?
      kdtree (std::vector<Vector>&& elements, const size_t dim) : dim (dim) {
        vector_set.reserve (elements.size ());
        for (ssize_t i = elements.size () - 1; i >= 0; --i) {
          bool unique = true;
          for (ssize_t j = 0; j < i; ++j)
            if (elements[j] == elements[i]) {
              unique = false;
              break;
            }
          if (unique)
            this->vector_set.push_back (std::move (elements[i]));
        }

        // WARNING: moved elements, so we can't really use it below! instead,
        // use this->vector_set
        assert (dim > 0);
        assert (this->vector_set.size () > 0);

        // We now prepare the list of indices to include in the tree 
        std::vector<size_t> points (this->vector_set.size ());
        std::iota(points.begin (), points.end (), 0);

        this->tree = recursive_build (points.begin (), points.end (),
                                      points.size (), 0);
        this->active_set.clear ();
      }

      kdtree (const kdtree& other) = delete;
      kdtree (kdtree&& other) = default;
      kdtree& operator= (kdtree&& other) = default;

      bool dominates (const Vector& v, bool strict = false) const {
        int lbounds[this->dim];
        std::fill_n (lbounds, this->dim, std::numeric_limits<int>::min ());
        return this->recursive_dominates (v, strict, this->tree, lbounds, this->dim);
      }

      void trim (const Vector& v, bool strict = false) {
        int ubounds[this->dim];
        std::fill_n (ubounds, this->dim, std::numeric_limits<int>::max ());
        this->recursive_trim (v, strict, this->tree, ubounds, this->dim);
      }
      
      bool is_antichain () {
        for (auto it = this->begin (); it != this->end (); ++it) {
          for (auto it2 = it + 1; it2 != this->end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }
        }
        return true;
      }
      bool operator== (kdtree& other) {
        return *(this->getActive ()) == *(other->getActive ());
      }
      auto size () {
        return this->getActive ()->size ();
      }
      bool empty () {
        return this->getActive ()->empty ();
      }
      auto begin () {
        return this->getActive ()->begin ();
      }
      auto end () {
        return this->getActive ()->end ();
      }
  };

  template <typename Vector>
  inline std::ostream& operator<< (std::ostream& os, kdtree<Vector>& f) {
    for (auto&& el : *(f.getActive ()))
      os << el << std::endl;

    return os;
  }
}
