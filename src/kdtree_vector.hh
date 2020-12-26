#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <numeric>
#include <vector>
#include <map>

// Forward definition for the operator<<s.
template <typename, size_t>
class kdtree_vector;

template <typename Vector, size_t dim>
std::ostream& operator<<(std::ostream& os, const kdtree_vector<Vector, dim>& f);

template <typename Vector, size_t dim>
class kdtree_vector {
    private:
        struct kdtree_node {
            std::shared_ptr<kdtree_node> left;
            std::shared_ptr<kdtree_node> right;
            int location;
            Vector value;

            kdtree_node (Vector leaf_value) : left(nullptr), right(nullptr),
                                              value(leaf_value) {}
            kdtree_node (std::shared_ptr<kdtree_node> l,
                         std::shared_ptr<kdtree_node> r,
                         int loc) : left(l), right(r), location(loc),
                                    value(dim) {}                        
        };
        std::shared_ptr<kdtree_node> tree;
        const std::vector<Vector> vector_set;
        template <typename V, size_t d>
        friend std::ostream& ::operator<<(std::ostream& os, const kdtree_vector<V, d>& f);

        std::shared_ptr<kdtree_node> recursive_build (const std::vector<std::vector<size_t>>& sorted,
                                                      size_t depth) {
            std::cout << "building at depth " << depth << std::endl;
            // if the list of elements is now a singleton, we make a leaf
            size_t length = sorted[0].size();
            if (length == 1)
                return std::make_shared<kdtree_node>(this->vector_set[sorted[0][0]]);
            std::cout << "length = " << length << std::endl;
            // otherwise we need to create an inner node
            size_t axis = depth % dim;
            size_t median_idx = (length - 1) / 2;
            std::cout << "axis = " << axis
                      << " median index = " << median_idx
                      << " location = " << this->vector_set[sorted[axis][median_idx]][axis]
                      << std::endl;
            // we have a median, so we can now prepare a map of flags to
            // "compress" the other lists
            std::map<size_t, bool> go_left;
            for (size_t i = 0; i < length; i++) {
                std::cout << "Does " << this->vector_set[sorted[axis][i]]
                          << " go left? ";
                if (i <= median_idx)
                    go_left[sorted[axis][i]] = true;
                else
                    go_left[sorted[axis][i]] = false;
                std::cout << go_left[sorted[axis][i]] << std::endl;
            }
            // we can start filling the sorted vectors for the recursive calls
            // now
            std::vector<std::vector<size_t>> left (dim);
            std::vector<std::vector<size_t>> right (dim);
            for (size_t d = 0; d < dim; d++) {
                for (size_t i = 0; i < length; i++) {
                    if (go_left[sorted[d][i]])
                        left[d].push_back (sorted[d][i]);
                    else
                        right[d].push_back (sorted[d][i]);
                }
            }
            assert(left[0].size() == median_idx + 1);
            assert(right[0].size() == length - median_idx - 1);
            // we are now ready for the recursive calls and to create the node
            std::shared_ptr<kdtree_node> left_res = recursive_build (left, depth + 1);
            std::shared_ptr<kdtree_node> right_res = recursive_build (right, depth + 1);
            return std::make_shared<kdtree_node>(left_res, right_res,
                                                 this->vector_set[
                                                    sorted[axis][median_idx]
                                                 ][axis]);
        }
    public:
        kdtree_vector (const std::vector<Vector>& elements) : vector_set{elements} {
            assert(this->vector_set.size() > 0);
            // we sort for each dimension
            std::vector<std::vector<size_t>> sorted (dim, std::vector<size_t>(elements.size()));
            for (size_t d = 0; d < dim; d++) {
                // initialize the indices
                std::iota (sorted[d].begin(), sorted[d].end(), 0);
                // we now sort the indices based on the dimension
                std::stable_sort (sorted[d].begin(), sorted[d].end(),
                                 [&elements, &d](size_t i1, size_t i2) {
                                     return elements[i1][d] < elements [i2][d];
                                 });
            }
            this->tree = recursive_build (sorted, 0);
        }

        bool contains (const Vector& v) const {
            size_t depth = 0;
            size_t axis;
            std::shared_ptr<kdtree_node> node = this->tree;
            while (true) {
                // if we are at a leaf, just check if it dominates
                if (node->left == nullptr) {
                    return v.partial_order (node->value).leq ();
                } else {
                    // we have to determine whether we want to go left or
                    // right
                    axis = depth % dim;
                    if (v[axis] <= node->location) {
                        node = node->left;
                    } else {
                        node = node->right;
                    }
                }
                depth++;
            }
        }

        kdtree_vector (const kdtree_vector& other) :
            tree(other.tree), vector_set{other.vector_set} {}

        kdtree_vector& operator= (kdtree_vector&& other) {
            tree = other.tree;
            vector_set = std::move (other.vector_set);
            return *this;
        }

        // Ideally deleted
        bool operator== (const kdtree_vector& other) const {
          return this->vector_set == other.vector_set;
        }

        std::shared_ptr<kdtree_vector> operator| (const kdtree_vector& other) {
            std::vector<Vector> result;
            for (const auto& e : this->vector_set)
                if (!other.contains(e))
                    result.emplace_back(e);
            for (const auto& e : other.vector_set)
                if (!this->contains(e))
                    result.emplace_back(e);
            std::cout << "size of union before tree = " << result.size() << std::endl;
            return std::make_shared<kdtree_vector>(result);
        }

        std::shared_ptr<kdtree_vector> operator& (const kdtree_vector& other) {
            std::vector<Vector> intersection;
            bool smaller_set = false;
            using cache_red_dim = std::set<Vector, disregard_first_component<Vector>>;
            using vector_of_vectors = std::vector<Vector>;
            std::map<int, std::pair<cache_red_dim, vector_of_vectors>> split_cache;
      
            for (const auto& x : vector_set) {
              bool dominated = false;
      
              auto& cv = ([&x, &split_cache, &other] () -> auto& {
                try {
                  return split_cache.at (x[0]);
                } catch (...) {
                  auto& cv = split_cache[x[0]];
                  for (const auto& y : other.vector_set) {
                    if (y[0] >= x[0])
                      cv.first.insert (y);
                    else
                      cv.second.push_back (y);
                  }
                  return cv;
                }
              }) ();
      
              auto meet = [&] (const Vector& y) {
                Vector &&v = x.meet (y);
                intersection.emplace_back(v);
                if (v == x) {
                  dominated = true;
                  return false;
                }
                return true;
             };

        std::all_of (cv.first.begin (), cv.first.end (), meet);
        if (!dominated)
          std::all_of (cv.second.begin (), cv.second.end (), meet);

        // If x wasn't <= an element in other, then x is not in the
        // intersection, thus the set is updated.
        smaller_set |= not dominated;
      }

      // TODO: optimize using smaller set
      return std::make_shared<kdtree_node>(intersection);
    }
    template <typename V>
    class disregard_first_component : public std::less<V> {
      public:
        bool operator() (const V& v1, const V& v2) const {
          auto v2prime = v2;
          v2prime[0] = v1[0];
          return std::less<V>::operator() (v1, v2prime);
        }
    };
        /*


    void intersect_with (const kdtree_vector& other) {
      kdtree_vector intersection;
      bool smaller_set = false;
      using cache_red_dim = std::set<Vector, disregard_first_component<Vector>>;
      using vector_of_vectors = std::vector<Vector>;
      std::map<int, std::pair<cache_red_dim, vector_of_vectors>> split_cache;

      for (const auto& x : vector_set) {
        bool dominated = false;

        auto& cv = ([&x, &split_cache, &other] () -> auto& {
          try {
            return split_cache.at (x[0]);
          } catch (...) {
            auto& cv = split_cache[x[0]];
            for (const auto& y : other.vector_set) {
              if (y[0] >= x[0])
                cv.first.insert (y);
              else
                cv.second.push_back (y);
            }
            return cv;
          }
        }) ();

        auto meet = [&] (const Vector& y) {
          Vector &&v = x.meet (y);
          intersection.insert (v);
          if (v == x) {
            dominated = true;
            return false;
          }
          return true;
        };

        std::all_of (cv.first.begin (), cv.first.end (), meet);
        if (!dominated)
          std::all_of (cv.second.begin (), cv.second.end (), meet);

        // If x wasn't <= an element in other, then x is not in the
        // intersection, thus the set is updated.
        smaller_set |= not dominated;
      }

      if (smaller_set) {
        *this = std::move (intersection);
        _updated = true;
      }
    }
*/

    auto size () const {
      return vector_set.size ();
    }

    bool empty () {
      return vector_set.empty ();
    }

    auto        begin ()      { return vector_set.begin (); }
    const auto  begin() const { return vector_set.begin (); }
    auto        end()         { return vector_set.end (); }
    const auto  end() const   { return vector_set.end (); }
};


template <typename Vector, size_t dim>
inline std::ostream& operator<<(std::ostream& os, const kdtree_vector<Vector, dim>& f) {
    for (auto el : f.vector_set)
        os << el << std::endl;
  
    return os;
}
