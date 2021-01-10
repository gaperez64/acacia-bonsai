#pragma once

#include <cassert>
#include <iostream>
#include <vector>

// Forward definition for the operator<<s.
template <typename>
class set_kdtree;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set_kdtree<Vector>& f);

template <typename Vector>
class set_kdtree {
    private:
        kdtree<Vector> tree;
        template <typename V>
        friend std::ostream& ::operator<<(std::ostream& os, const set_kdtree<V>& f);
        bool _updated = false;

    public:
        set_kdtree (const std::vector<Vector>& elements) : tree(elements) {}

        set_kdtree (const set_kdtree& other) : tree(other.tree) {}

        set_kdtree& operator= (set_kdtree&& other) {
            this->tree = other.tree;
            return *this;
        }

        bool contains (const Vector& v) const {
          return this->tree.dominates(v);
        }

        void clear_update_flag () {
          _updated = false;
        }

        bool updated () {
          return _updated;
        }

        bool operator== (const set_kdtree& other) const {
            return tree == other.tree;
        }

        // union in place
        void union_with (const set_kdtree& other) {
            std::vector<Vector> result;
            // for all elements in this tree, if they are not strictly
            // dominated by the other tree, we keep them
            for (auto e = this->tree.begin(); e != this->tree.end(); ++e) {
                if (!other.contains(e, true))
                    result.emplace_back(e);
                else
                    _updated = true;
            }
            // for all elements in the other tree, if they are not dominated
            // (not necessarily strict) by this tree, we keep them
            for (auto e = other.tree.begin(); e != other.tree.end(); ++e) {
                if (!this->contains(e))
                    result.emplace_back(e);
                else
                    _updated = true;
            }
            this->tree = std::make_shared<kdtree<Vector>> (result);
        }

        // intersection in place TODO: continue fixing from here!!
    template <typename V>
    class disregard_first_component : public std::less<V> {
      public:
        bool operator() (const V& v1, const V& v2) const {
          auto v2prime = v2;
          v2prime[0] = v1[0];
          return std::less<V>::operator() (v1, v2prime);
        }
    };
        void intersection_with (const set_kdtree& other) {
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

      if (smaller_set) {
        this->tree = std::make_shared<kdtree<Vector>> (intersection);
        _updated = true;
      }
    }
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
      return tree.size ();
    }

    bool empty () {
      return tree.empty ();
    }

    auto        begin ()       { return tree.begin (); }
    const auto  begin () const { return tree.begin (); }
    auto        end ()         { return tree.end (); }
    const auto  end () const   { return tree.end (); }
};


template <typename Vector>
inline std::ostream& operator<<(std::ostream& os, const kdtree_vector<Vector>& f) {
    os << f.tree << std::endl;
  
    return os;
}
