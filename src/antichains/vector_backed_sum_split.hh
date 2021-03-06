#pragma once

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <cassert>
#include <sstream>
#include <cstdlib>

namespace antichains {
  template <typename Vector>
  class vector_backed_sum_split {
    public:
      typedef Vector value_type;

      vector_backed_sum_split (Vector&& v) {
        vector_set.resize (10);
        insert (std::move (v));
      }

    private:
      vector_backed_sum_split () {
        vector_set.resize (10);
      }

    public:
      vector_backed_sum_split (const vector_backed_sum_split&) = delete;
      vector_backed_sum_split (vector_backed_sum_split&&) = default;
      vector_backed_sum_split& operator= (vector_backed_sum_split&&) = default;
      vector_backed_sum_split& operator= (const vector_backed_sum_split&) = delete;

      bool operator== (const vector_backed_sum_split& other) = delete;

      bool contains (const Vector& v) const {
        size_t bin = bin_of (v);

        if (bin >= vector_set.size ())
          return false;
        for (auto it = vector_set.begin () + bin; it != vector_set.end (); ++it)
          for (const auto& e : *it)
            if (v.partial_order (e).leq ())
              return true;
        return false;
      }

      auto size () const {
        return _size;
      }

      inline bool insert (Vector&& v) {
        size_t bin = bin_of (v);
        auto start = std::min (bin, vector_set.size () - 1);
        [[maybe_unused]] bool must_remove = false;

        size_t i = start;
        do {
          // This is like remove_if, but allows breaking.
          auto result = vector_set[i].begin ();
          auto end = vector_set[i].end ();

          for (auto it = result; it != end; ++it) {
            auto res = v.partial_order (*it);
            if (res.leq ()) { // v is dominated.
              assert (not must_remove); // Assuming we started with an antichain
              return false;
#warning Check that geq is not called when NDEBUG?
            } else if (res.geq ()) { // v dominates *it
              must_remove = true; /* *it should be removed */
            } else { // *it needs to be kept
              if (result != it) // This can be false only on the first element.
                *result = std::move (*it);
              ++result;
            }
          }

          if (result != vector_set[i].end ())
            vector_set[i].erase (result, vector_set[i].end ());

          i = (i == vector_set.size () - 1) ? 0 : i + 1;
          // i = (i + 1) % vector_set.size ();
        } while (i != start);

        if (bin >= vector_set.size ()) {
          std::cout << "Resizing to " << bin + 1 << std::endl;
          vector_set.resize (bin + 1);
        }
        vector_set[bin].push_back (std::move (v));
        ++_size;
        return true;
      }

      bool empty () {
        return vector_set.empty ();
      }

      void union_with (vector_backed_sum_split&& other) {
        for (auto&& evec : other.vector_set)
          for (auto&& e : evec)
            _updated |= insert (std::move (e));
      }

      void intersect_with (vector_backed_sum_split&& other) {
        if (vector_set.empty ())
          return;
        vector_backed_sum_split intersection;

        size_t bin = vector_set.size () / 2;

        do {
          for (const auto& x : vector_set[bin]) {
            bool dominated = false;

            // These can dominate x
            for (size_t i = bin; i < other.vector_set.size (); ++i) {
              for (auto&& el : other.vector_set[i]) {
                Vector&& v = x.meet (el);
                if (v == x)
                  dominated = true;
                #warning  Check v == el?
                intersection.insert (std::move (v));
                if (dominated)
                  break;
              }
              if (dominated)
                break;
            }
            if (dominated)
              continue;

            // These cannot dominate x
            for (ssize_t i = std::min (bin, other.vector_set.size () - 1); i >= 0; --i) {
              for (auto&& it = other.vector_set[i].begin (); it != other.vector_set[i].end (); /* in-body */) {
                Vector&& v = x.meet (*it);
                if (v == *it) {
                  intersection.insert (std::move (v));
                  if (it != other.vector_set[i].end () - 1)
                    std::swap (*it, other.vector_set[i].back());
                  other.vector_set[i].pop_back ();
                }
                else {
                  intersection.insert (std::move (v));
                  ++it;
                }
              }
            }
          }
          bin = (bin == vector_set.size () - 1 ? 0 : bin + 1);
        } while (bin != vector_set.size () / 2);

        *this = std::move (intersection);
      }

#warning FIXME: Antichain?
      template <typename F>
      vector_backed_sum_split apply (const F& lambda) const {
        vector_backed_sum_split res;
        for (const auto& elvec : vector_set)
          for (auto&& el : elvec)
            res.insert (lambda (el));
        return res;
      }

      // template <typename T>
      // struct const_iterator {
      //     const_iterator (const T& vs, bool end) :
      //       vs {vs}, cur {vs.size () / 2}, last {vs.size () == 1 ? 0 : vs.size () / 2 - 1} {
      //       if (end) {
      //         cur = last;
      //         sub_it = (sub_end = vs[cur].end ());
      //       }
      //       else {
      //         sub_it = vs[cur].begin ();
      //         sub_end = vs[cur].end ();
      //         stabilize ();
      //       }
      //     }

      //     auto operator++ () {
      //       ++sub_it;
      //       stabilize ();
      //     }

      //     bool operator!= (const const_iterator& other) const {
      //       return not (cur == other.cur and last == other.last and
      //                   sub_it == other.sub_it and sub_end == other.sub_end);
      //     }

      //     auto&& operator* () const { return *sub_it;}

      //   private:
      //     void stabilize () {
      //       while (sub_it == sub_end) {
      //         if (cur == last)
      //           return;
      //         ++cur;
      //         if (cur == vs.size ())
      //           cur = 0;
      //         sub_it = vs[cur].begin ();
      //         sub_end = vs[cur].end ();
      //       }
      //     }

      //     const T& vs;
      //     size_t cur, last;
      //     decltype (T ().begin ()->cbegin ()) sub_it, sub_end;
      // };

      // const auto  begin() const { return const_iterator (vector_set, false); }
      // const auto  end()   const { return const_iterator (vector_set, true); }


      template <typename T>
      struct iterator {
          iterator (T first, T end) : it {first}, end {end} {
            if (it == end) return;
            sub_it = it->begin ();
            sub_end = it->end ();
            stabilize ();
          }

          auto operator++ () {
            ++sub_it;
            stabilize ();
          }

          bool operator!= (const iterator& other) const {
            return not (it == other.it and end == other.end and
                        (it == end or
                         (sub_it == other.sub_it and
                          sub_end == other.sub_end)));
          }

          auto&& operator* () const { return *sub_it;}

        private:
          void stabilize () {
            while (sub_it == sub_end) {
              ++it;
              if (it == end)
                return;
              sub_it = it->begin ();
              sub_end = it->end ();
            }
          }
          T it, end;
          decltype (T ()->begin ()) sub_it, sub_end;
      };

      const auto  begin() const { return iterator (vector_set.crbegin (), vector_set.crend ()); }
      const auto  end() const   { return iterator (vector_set.crend (), vector_set.crend ()); }

    private:
      using vector_set_t = std::vector<std::vector<Vector>>;
      vector_set_t vector_set; // [n] -> all the vectors with v[split_dim] = n
      bool _updated = false;
      size_t _size = 0;
      size_t split_dim;

      // Surely: if bin_of (u) > bin_of (v), then v can't dominate u.
      struct general_ {};
      struct special_ : general_ {};
      template <typename> struct int_ { typedef int type; };

      size_t bin_of (const Vector& v) const {
        return bin_of_helper (v, special_ ());
      }

      template <typename V, typename int_<decltype (V::sum)>::type = 0>
      size_t bin_of_helper (const V& v, special_) const {
        return (size_t) (((ssize_t) v.sum + v.size ()) / v.size ());
      }

      template <typename V>
      size_t bin_of_helper (const V& v, general_) const {
        return 0;
      }
  };

}

template <typename Vector>
inline
std::ostream& operator<<(std::ostream& os,
                         const antichains::vector_backed_sum_split<Vector>& f)
{
  for (auto&& el : f)
    os << el << std::endl;

  return os;
}
