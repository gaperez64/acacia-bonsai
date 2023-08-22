#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <vector>
#include <map>

namespace utils {
  // Forward definition for the operator<<s.
  template <typename>
  class kdtree;

  template <typename Vector>
  std::ostream& operator<< (std::ostream& os, const utils::kdtree<Vector>& f);

  template <typename Vector>
  class kdtree {
    private:
      struct kdtree_node {
          std::shared_ptr<kdtree_node> left;
          std::shared_ptr<kdtree_node> right;
          int location;
          size_t value_idx;
          size_t axis;

          kdtree_node (size_t idx) : left (nullptr),
                                     right (nullptr),
                                     value_idx (idx),
                                     axis (0) {}
          kdtree_node (std::shared_ptr<kdtree_node> l,
                       std::shared_ptr<kdtree_node> r,
                       int loc, size_t a) : left (l), right (r),
                                            location (loc),
                                            axis (a) {}
      };
      const size_t dim;
      std::shared_ptr<kdtree_node> tree;

      template <typename V>
      friend std::ostream& operator<< (std::ostream& os, const kdtree<V>& f);

      size_t dim_max_range (std::vector<size_t>& points) {
        assert (points.size () > 0);

        if (points.size () == 1)
          return 0;

        // and determine the axis based on the dimension with the
        // longest range
        int max_range;
        size_t axis;
        for (size_t d = 0; d < this->dim; d++) {
          auto it = points.begin();
          int val = this->vector_set[(*it)][d];
          int min = val;
          int max = val;
          while (it != points.end ()) {
            val = this->vector_set[(*it)][d];
            if (val < min) min = val;
            if (val > max) max = val;
            it++;
          }
          if (d == 0 || max_range < max - min) {
            max_range = max - min;
            axis = d;
          }
        }
        return axis;
      }

      std::shared_ptr<kdtree_node>
      recursive_build (std::vector<size_t>& points, size_t axis) {
        const size_t length = points.size ();
        assert (length > 0);
        assert (axis < this->dim && axis >= 0);

        // if the list of elements is now a singleton, we make a leaf
        if (length == 1)
          return std::make_shared<kdtree_node> (points[0]);

        // Use a selection algorithm to get the median, technically we get the
        // floor of the median as the median of an even-length list could be a
        // rational if one chooses the arithmetic mean
        auto mit = points.begin () + length / 2;
        std::nth_element (points.begin (), mit, points.end (), 
                          [this, &axis] (size_t i1, size_t i2) {
                            return this->vector_set[i1][axis] <
                                   this->vector_set[i2][axis];
                          });
        size_t median_idx = points[length / 2];
        int loc = this->vector_set[median_idx][axis];
        // small hack: if the median is the same as the max then we choose a
        // location (splitting value) that is one less, this ensures a
        // nonempty subset of elements with a strictly larger value
        mit = points.begin () + length / 2;
        bool found = false;
        while (mit != points.end ()) {
          if (this->vector_set[*mit][axis] > loc) {
            found = true;
            break;
          }
          mit++;
        }
        if (!found)
          loc--;

        // we can start filling the vectors for the recursive calls
        std::vector<size_t> left (std::move (points));
        std::vector<size_t> right (points.size ());

        for (auto it = left.begin (); it != left.end (); /* noop */) {
          if (loc < this->vector_set[*it][axis]) {
            right.push_back (*it);
            it = left.erase (it);
          } else
            ++it;
        }

        // some sanity checks
        assert (left.size () > 0);
        assert (right.size () > 0);
        assert (right.size () + left.size () == length);

        // FIXME: can we be more efficient in our choice of axes than just
        // calling dim_max_range? after all, we are already traversing left
        // and right a few lines above
        return std::make_shared<kdtree_node> (recursive_build (left, dim_max_range(left)),
                                              recursive_build (right, dim_max_range(right)),
                                              loc, axis);
      }

      bool recursive_dominates (const Vector& v, bool strict,
                                std::shared_ptr<kdtree_node> node) const {
        assert (node != nullptr);

        // if we are at a leaf, just check if it dominates
        if (node->left == nullptr) {
          auto po = v.partial_order (this->vector_set[node->value_idx]);

          if (strict)
            return po.leq () and not po.geq ();
          else
            return po.leq ();
        } else {
          // we have to determine if left and right
          // have to be explored
          if (v[node->axis] > node->location) {
            // we won't find dominating vectors on the left
            return recursive_dominates (v, strict, node->right);
          } else {
            // in this case we do have to explore both sub-trees
            return (recursive_dominates (v, strict, node->left) ||
                    recursive_dominates (v, strict, node->right));
          }
        }
      }

    public:
      std::vector<Vector> vector_set;

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
        std::iota(points.begin(), points.end(), 0);

        this->tree = recursive_build (points, dim_max_range(points));
      }

      kdtree (const kdtree& other) = delete;
      kdtree (kdtree&& other) = default;
      kdtree& operator= (kdtree&& other) = default;

      bool is_antichain () const {
        for (auto it = vector_set.begin (); it != vector_set.end (); ++it) {
          for (auto it2 = it + 1; it2 != vector_set.end (); ++it2) {
            auto po = it->partial_order (*it2);
            if (po.leq () or po.geq ())
              return false;
          }
        }
        return true;
      }

      bool operator== (const kdtree& other) const {
        return vector_set == other.vector_set;
      }
      bool dominates (const Vector& v, bool strict = false) const {
        return this->recursive_dominates (v, strict, this->tree);
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
      const auto end () const   {
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
