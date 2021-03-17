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
}

template <typename Vector>
std::ostream& operator<< (std::ostream& os, const utils::kdtree<Vector>& f);

namespace utils {

  template <typename Vector>
  class kdtree {
    private:
      struct kdtree_node {
          std::shared_ptr<kdtree_node> left;
          std::shared_ptr<kdtree_node> right;
          int location;
          size_t value_idx;
          size_t depth;

          kdtree_node (size_t idx) : left (nullptr),
                                     right (nullptr),
                                     value_idx (idx),
                                     depth (0) {}
          kdtree_node (std::shared_ptr<kdtree_node> l,
                       std::shared_ptr<kdtree_node> r,
                       int loc, size_t d) : left (l), right (r),
                                            location (loc),
                                            depth (d) {}
      };
      const size_t dim;
      std::shared_ptr<kdtree_node> tree;

      template <typename V>
      friend std::ostream& ::operator<< (std::ostream& os, const kdtree<V>& f);

      std::vector<bool> go_left;

      std::shared_ptr<kdtree_node>
      recursive_build (const std::vector<std::list<size_t>>& sorted,
                       size_t depth) {
        assert (sorted.size () > 0);
        
        // if the list of elements is now a singleton, we make a leaf
        const size_t length = sorted[0].size ();
        if (length == 1)
          return std::make_shared<kdtree_node> (sorted[0].front());

        // otherwise we need to create an inner node based on the dimension
        // "axis" and the median w.r.t. the axis
        const size_t axis = depth % this->dim;

        // we want the median to be the greatest index with some value (in
        // dimension = axis) we first try moving left, then right
        auto idx_it = sorted[axis].begin();
        size_t cur_value = this->vector_set[*idx_it][axis];
        ++idx_it;
        size_t last_change = 0;
        bool found = false;
        for (size_t i = 1; i < length; i++) {
          size_t val = this->vector_set[*idx_it][axis];
          if (cur_value != val) {
            cur_value = val;
            last_change = i;
            if (i >= ((length - 1) / 2)) {
              found = true;
              break;
            }
          }
          ++idx_it;
        }

        if (not found) {
          // this dimension is useless, move to the next one
          return recursive_build (sorted, depth + 1);
        }

        // otherwise a border was found!
        const size_t median_idx = last_change - 1;
        // we have a median, so we have a location
        const size_t loc = this->vector_set[*std::next(sorted[axis].begin(),
                                                       median_idx)
                                           ][axis];
        // and we can now prepare a map of flags to
        // "compress" the other lists
        idx_it = sorted[axis].begin();
        for (size_t i = 0; i < length; i++) {
          if (i <= median_idx)
            go_left[*idx_it] = true;
          else
            go_left[*idx_it] = false;
          ++idx_it;
        }

        // we can start filling the sorted vectors for the recursive calls
        std::vector<std::list<size_t>> left (std::move (sorted));
        std::vector<std::list<size_t>> right (this->dim);

        for (size_t d = 0; d < this->dim; d++) {
           for (auto it = left[d].begin (); it != left[d].end (); /* noop */) {
             if (not go_left[*it]) {
               right[d].push_back (*it);
               it = left[d].erase(it);
             } else
               ++it;
           }
        }

        // some sanity checks
        assert (left[0].size () > 0);
        assert (right[0].size () > 0);
        assert (right[0].size () + left[0].size () == length);

        return std::make_shared<kdtree_node> (recursive_build (left, depth + 1),
                                              recursive_build (right, depth + 1),
                                              loc, depth);
      }

      bool recursive_dominates (const Vector& v,
                                bool strict,
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
          size_t axis = node->depth % this->dim;

          if (v[axis] > node->location) {
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

      kdtree (std::vector<Vector>&& elements, const size_t dim) : dim (dim),
                                                                  go_left (elements.size ()) {
        vector_set.reserve (elements.size ());

        for (ssize_t i = elements.size () - 1; i >= 0; --i) {
          bool unique = true;

          for (ssize_t j = 0; j < i; ++j)
            if (elements[j] == elements[i]) {
              unique = false;
              break;
            }

          if (unique)
            vector_set.push_back (std::move (elements[i]));
        }

        // WARNING: moved elements, so we can't really use it below! instead,
        // use this->vector_set
        assert (dim > 0);
        assert (this->vector_set.size () > 0);
        // we sort for each dimension
        std::vector<std::list<size_t>> sorted (this->dim,
                                               std::list<size_t> (vector_set.size (), 0));

        for (size_t d = 0; d < this->dim; d++) {
          // initialize the indices
          std::iota (sorted[d].begin (), sorted[d].end (), 0);
          // we now sort the indices based on the dimension
          sorted[d].sort([this, &d] (size_t i1, size_t i2) {
                           return this->vector_set[i1][d] < this->vector_set[i2][d];
                        });
          assert (this->vector_set.size () == sorted[d].size ());
        }

        assert (sorted.size () == dim);
        assert (sorted[0].size () == this->vector_set.size ());
        this->tree = recursive_build (sorted, 0);
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

      auto        begin ()       {
        return this->vector_set.begin ();
      }
      const auto  begin () const {
        return this->vector_set.begin ();
      }
      auto        end ()         {
        return this->vector_set.end ();
      }
      const auto  end () const   {
        return this->vector_set.end ();
      }
  };
}

template <typename Vector>
inline std::ostream& operator<< (std::ostream& os, const utils::kdtree<Vector>& f) {
  for (auto&& el : f.vector_set)
    os << el << std::endl;

  return os;
}
