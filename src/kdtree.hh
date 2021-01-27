#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <vector>
#include <map>

// Forward definition for the operator<<s.
template <typename>
class kdtree;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const kdtree<Vector>& f);

template <typename Vector>
class kdtree {
    private:
        struct kdtree_node {
            std::shared_ptr<kdtree_node> left;
            std::shared_ptr<kdtree_node> right;
            int location;
            size_t value_idx;

            kdtree_node (size_t idx) : left (nullptr),
                                       right (nullptr),
                                       value_idx (idx) {}
            kdtree_node (std::shared_ptr<kdtree_node> l,
                         std::shared_ptr<kdtree_node> r,
                         int loc) : left (l), right (r),
                                    location (loc) {}
        };
        const size_t dim;
        std::shared_ptr<kdtree_node> tree;
        std::vector<Vector> vector_set;
        template <typename V>
        friend std::ostream& ::operator<< (std::ostream& os, const kdtree<V>& f);

        std::shared_ptr<kdtree_node>
        recursive_build (const std::vector<std::vector<size_t>>& sorted,
                         size_t depth) {
            // if the list of elements is now a singleton, we make a leaf
            size_t length = sorted[0].size ();
            if (length == 1)
                return std::make_shared<kdtree_node> (sorted[0][0]);
            // otherwise we need to create an inner node
            size_t axis = depth % this->dim;
            size_t median_idx = (length - 1) / 2;
            // we have a median, so we can now prepare a map of flags to
            // "compress" the other lists
            std::map<size_t, bool> go_left;
            for (size_t i = 0; i < length; i++) {
                if (i <= median_idx)
                    go_left[sorted[axis][i]] = true;
                else
                    go_left[sorted[axis][i]] = false;
            }
            // we can start filling the sorted vectors for the recursive calls
            std::vector<std::vector<size_t>> left (this->dim);
            std::vector<std::vector<size_t>> right (this->dim);
            for (size_t d = 0; d < this->dim; d++) {
                for (size_t i = 0; i < length; i++) {
                    if (go_left[sorted[d][i]])
                        left[d].push_back (sorted[d][i]);
                    else
                        right[d].push_back (sorted[d][i]);
                }
            }
            assert (left[0].size () == median_idx + 1);
            assert (right[0].size () == length - median_idx - 1);
            // we are now ready for the recursive calls and to create the node
            std::shared_ptr<kdtree_node> left_res = recursive_build (left, depth + 1);
            std::shared_ptr<kdtree_node> right_res = recursive_build (right, depth + 1);
            return std::make_shared<kdtree_node>(left_res, right_res,
                                                 this->vector_set[
                                                    sorted[axis][median_idx]
                                                 ][axis]);
        }

        bool recursive_dominates (const Vector& v,
                                  bool strict,
                                  std::shared_ptr<kdtree_node> node,
                                  size_t depth = 0) const {
            // if we are at a leaf, just check if it dominates
            if (node->left == nullptr) {
                return v.partial_order (this->vector_set[node->value_idx]).leq ();
            } else {
                // we have to determine if left and right
                // have to be explored
                size_t axis = depth % this->dim;
                if (strict && v[axis] >= node->location) {
                    // we won't find dominating vectors on the left
                    return recursive_dominates (v, strict, node->right, depth + 1);
                } else if (!strict && v[axis] > node->location) {
                    // in this case we also do not need to explore the left
                    return recursive_dominates (v, strict, node->right, depth + 1);
                } else {
                    // in this case we do have to explore both sub-trees
                    return (recursive_dominates (v, strict, node->left, depth + 1) ||
                            recursive_dominates (v, strict, node->right, depth + 1));
                }
            }
        }

    public:
        kdtree (const std::vector<Vector>& elements, const size_t dim) : dim(dim),
                                                                         vector_set{elements} {
            assert(this->vector_set.size () > 0);
            // we sort for each dimension
            std::vector<std::vector<size_t>> sorted (this->dim,
                                                     std::vector<size_t>(elements.size()));
            for (size_t d = 0; d < this->dim; d++) {
                // initialize the indices
                std::iota (sorted[d].begin (), sorted[d].end (), 0);
                // we now sort the indices based on the dimension
                std::stable_sort (sorted[d].begin(), sorted[d].end(),
                                 [&elements, &d](size_t i1, size_t i2) {
                                     return elements[i1][d] < elements [i2][d];
                                 });
            }
            this->tree = recursive_build (sorted, 0);
        }

        kdtree (const kdtree& other) : tree(other.tree), dim(other.dim),
                                       vector_set{other.vector_set} {}

        kdtree& operator= (kdtree&& other) {
            this->tree = other.tree;
            this->dim = other.dim;
            this->vector_set = std::move (other.vector_set);
            return *this;
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

        auto        begin ()       { return this->vector_set.begin (); }
        const auto  begin () const { return this->vector_set.begin (); }
        auto        end ()         { return this->vector_set.end (); }
        const auto  end () const   { return this->vector_set.end (); }
};

template <typename Vector>
inline std::ostream& operator<< (std::ostream& os, const kdtree<Vector>& f) {
    for (auto el : f.vector_set)
        os << el << std::endl;
    return os;
}
