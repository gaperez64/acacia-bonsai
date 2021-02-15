#pragma once

#include <cassert>
#include <iostream>
#include <vector>

#include "kdtree.hh"

// Forward definition for the operator<<s.
template <typename>
class set_kdtree;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set_kdtree<Vector>& f);

template <typename Vector>
class set_kdtree {
    private:
        std::shared_ptr<kdtree<Vector>> tree;
        bool _updated = false;

        template <typename V>
        friend std::ostream& ::operator<<(std::ostream& os, const set_kdtree<V>& f);
        template <typename V>
        class disregard_first_component : public std::less<V> {
          public:
            bool operator() (const V& v1, const V& v2) const {
              auto v2prime = v2;
              v2prime[0] = v1[0];
              return std::less<V>::operator() (v1, v2prime);
            }
        };

    public:
        set_kdtree (const std::vector<Vector>& elements) {
            assert (elements.size() > 0);
            this->tree = std::make_shared<kdtree<Vector>>(elements, elements[0].size());
        }

        set_kdtree (const set_kdtree& other) : tree(other.tree) {}

        set_kdtree& operator= (set_kdtree&& other) {
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

        bool operator== (const set_kdtree& other) const {
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
        void union_with (const set_kdtree& other) {
            std::vector<Vector> result;
            // for all elements in this tree, if they are not strictly
            // dominated by the other tree, we keep them
            for (auto eit = this->tree->begin(); eit != this->tree->end(); ++eit) {
                auto e = *eit;
                if (!other.tree->dominates(e, true))
                    result.emplace_back(e);
                else
                    _updated = true;
            }
            // for all elements in the other tree, if they are not dominated
            // (not necessarily strict) by this tree, we keep them
            for (auto eit = other.tree->begin(); eit != other.tree->end(); ++eit) {
                auto e = *eit;
                if (!this->tree->dominates(e))
                    result.emplace_back(e);
                else
                    _updated = true;
            }
            this->tree = std::make_shared<kdtree<Vector>> (result, result[0].size());
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
        void intersection_with (const set_kdtree& other) {
            std::vector<Vector> intersection;
            bool smaller_set = false;
            using cache_red_dim = std::set<Vector, disregard_first_component<Vector>>;
            using vector_of_vectors = std::vector<Vector>;
            std::map<int, std::pair<cache_red_dim, vector_of_vectors>> split_cache;
      
            for (auto xit = this->tree->begin();
                 xit != this->tree->end(); ++xit) {
                auto x = *xit;
                bool dominated = false;
      
                auto& cv = ([&x, &split_cache, &other] () -> auto& {
                    try {
                        return split_cache.at (x[0]);
                    } catch (...) {
                        auto& cv = split_cache[x[0]];
                        for (auto yit = other.tree->begin();
                             yit != other.tree->end(); ++yit) {
                          auto y = *yit;
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
                assert (intersection.size() > 0);
                kdtree<Vector> temp_tree(intersection, intersection[0].size());
                std::vector<Vector> inter_antichain;
                for (auto const& e : intersection) {
                    if (!temp_tree.dominates(e, true))
                        inter_antichain.emplace_back(e);
                }
                this->tree = std::make_shared<kdtree<Vector>> (inter_antichain,
                                                               inter_antichain[0].size());
                _updated = true;
            }
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


template <typename Vector>
inline std::ostream& operator<<(std::ostream& os, const set_kdtree<Vector>& f) {
    os << f.tree << std::endl;
  
    return os;
}
