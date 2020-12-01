#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>

// Forward definition for the operator<<s.
template <typename Vector>
class set_antichain_vector;

template <typename Vector>
std::ostream& operator<<(std::ostream& os, const set_antichain_vector<Vector>& f);

template <typename Vector>
class set_antichain_vector {
  public:
    set_antichain_vector () {}

    set_antichain_vector (const set_antichain_vector& other) :
      vector_set{other.vector_set} {}

    set_antichain_vector& operator= (set_antichain_vector&& other) {
      vector_set = std::move (other.vector_set);
      _updated = other._updated;
      return *this;
    }

    // Ideally deleted
    bool operator== (const set_antichain_vector& other) const {
      return vector_set == other.vector_set;
    }

    bool contains (const Vector& v) const {
      for (const auto& e : vector_set)
        if (v.partial_order (e).leq ())
          return true;
      return false;
    }

    void clear_update_flag () {
      _updated = false;
    }

    bool updated () {
      return _updated;
    }

    auto size () const {
      return vector_set.size ();
    }

    bool insert (const Vector& v) {
      std::vector<Vector> to_remove;
      bool should_be_inserted = true;

      // This is like remove_if, but allows breaking.
      auto result = vector_set.begin ();
      auto end = vector_set.end ();
      for (auto it = result; it != end; ++it) {
       auto res = v.partial_order (*it);
       if (res.leq ()) {
          should_be_inserted = false;
          break;
       } else if (res.geq ()) {
          should_be_inserted = true;
        } else { // Element needs to be kept
          if (result != it) // This can be false only on the first element.
            *result = std::move (*it);
          ++result;
        }
      }

      if (should_be_inserted) {
        vector_set.erase (result, vector_set.end ());
        vector_set.push_back (v);
        _updated = true;
      }

      return should_be_inserted;
    }

    void downward_close () {
      // nil
    }

    const set_antichain_vector& max_elements () const {
      return *this;
    }

    bool empty () {
      return vector_set.empty ();
    }

    void union_with (const set_antichain_vector& other) {
      for (const auto& e : other.vector_set)
        _updated |= insert (e);
    }

    void intersect_with_ (const set_antichain_vector& other) {
      set_antichain_vector intersection;
      bool smaller_set = false;

      for (const auto& x : vector_set) {
        bool dominated = false;

        for (const auto& y : other.vector_set) {
          Vector &&v = x.meet (y);
          intersection.insert (v);
          if (v == x) {
            dominated = true;
            break;
          }
        }
        // If x wasn't <= an element in other, then x is not in the
        // intersection, thus the set is updated.
        smaller_set |= not dominated;
      }

      if (smaller_set) {
        *this = std::move (intersection);
        _updated = true;
      }
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

    void intersect_with (const set_antichain_vector& other) {
      set_antichain_vector intersection;
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

    template <typename F>
    set_antichain_vector apply (const F& lambda) {
      set_antichain_vector res;
      for (auto el : vector_set)
        res.insert (lambda (el));
      return res;
    }

    template <typename F>
    void apply_inplace (const F& lambda) {
      std::vector<Vector> new_set;
      for (auto el : vector_set) {
        auto&& changed_el = lambda (el);
        if (changed_el != el)
          _updated = true;
        new_set.push_back (changed_el); // May not be an antichain, but speeds up.
      }
      vector_set = std::move (new_set);
    }

  private:
    std::vector<Vector> vector_set;
    bool _updated = false;
    template <typename V>
    friend std::ostream& ::operator<<(std::ostream& os, const set_antichain_vector<V>& f);
};


template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os, const set_antichain_vector<Vector>& f)
{
  for (auto el : f.vector_set)
    os << el << std::endl;

  return os;
}
