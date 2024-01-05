#pragma once

#include <cassert>
#include <iostream>
#include <math.h>
#include <vector>

#include <utils/vector_mm.hh>

#include <downsets/kdtree_backed.hh>
#include <downsets/vector_backed.hh>

// FIXME? the theory says it should be (exp (dim) < m)
# define KD_THRESH(M, D)  (D * 2 < M)

#ifdef AC_DATA
# define data_do(acts...) do { \
    acts;                      \
  } while (0)
#else
# define data_do(x...)
#endif

namespace downsets {
  // Forward definition for the operator<<s.
  template <typename>
  class vector_or_kdtree_backed;

  template <typename Vector>
  std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<Vector>& f);

  template <typename Vector>
  class vector_or_kdtree_backed {
    private:
      std::unique_ptr<vector_backed<Vector>> vector;
      std::unique_ptr<kdtree_backed<Vector>> kdtree;

      template <typename V>
      friend std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<V>& f);

      size_t dim () {
        if (this->vector != nullptr)
          return this->vector->vector_set.size ();
        else {
          assert (this->kdtree != nullptr);
          return this->kdtree->tree.vector_set.size ();
        }
      }

      void boolop_with (vector_or_kdtree_backed&& other, bool inter = false) {
        // four cases: 1. both are vectors, 2/3. one is a vector,
        // 4. both are kdtrees
        if (this->kdtree == nullptr && other.kdtree == nullptr) {
          // case 1. just work with vectors
          assert (other.vector != nullptr);
          assert (this->vector != nullptr);
          if (inter)
            this->vector->intersect_with (std::move (*(other.vector)));
          else
            this->vector->union_with (std::move (*(other.vector)));
        } else if (this->kdtree == nullptr && other.kdtree != nullptr) {
          // case 2. reinterpret kdtree as vector
          assert (this->vector != nullptr);
          assert (other.vector == nullptr);
          // this costs linear time already: reinterpret the kdtree as a
          // vector
          vector_backed<Vector> B = vector_backed<Vector> (std::move (other.kdtree->tree.vector_set));
          if (inter)
            this->vector->intersect_with (std::move (B));
          else
            this->vector->union_with (std::move (B));
        } else if (other.kdtree == nullptr && this->kdtree != nullptr) {
          // case 3. again reinterpret kdtree as vector
          assert (other.vector != nullptr);
          assert (this->vector == nullptr);
          this->vector = std::make_unique<vector_backed<Vector>> (std::move (this->kdtree->tree.vector_set));
          this->kdtree = nullptr;
          if (inter)
            this->vector->intersect_with (std::move (*(other.vector)));
          else
            this->vector->union_with (std::move (*(other.vector)));
        } else {
          // case 4. we have two kdtrees, though we still
          // need to check if reinterpreting them as vectors
          // is easier
          size_t m, n;
          if (this->size () > other.size ()) {
            m = other.size ();
            n = this->size ();
          } else {
            m = this->size ();
            n = this->size ();
          }
          if (n < exp (m)) {
            if (inter)
              this->kdtree->intersect_with (std::move (*(other.kdtree)));
            else
              this->kdtree->union_with (std::move (*(other.kdtree)));
          } else {
            this->vector = std::make_unique<vector_backed<Vector>> (std::move (this->kdtree->tree.vector_set));
            this->kdtree = nullptr;
            vector_backed<Vector> B = vector_backed<Vector> (std::move (other.kdtree->tree.vector_set));
            if (inter)
              this->vector->intersect_with (std::move (B));
            else
              this->vector->union_with (std::move (B));
          }
        }

        // one last thing to deal with: it could be that we ended up with a
        // list but it satisfies the condition for it to be upgraded to a
        // kd-tree
        size_t m = this->size ();
        size_t dim = this->dim ();
        data_do (std::cout << "|VEKD: downset_size="
                         << dim << "," << m << "|" << std::endl);
        if (this->kdtree == nullptr && KD_THRESH(m, dim)) {
          this->kdtree = std::make_unique<kdtree_backed<Vector>> (std::move (this->vector->vector_set));
          this->vector = nullptr;
          data_do (std::cout << "VEKD: upgraded to kd-tree downset" << std::endl);
        }
      }

    public:
      typedef Vector value_type;

      vector_or_kdtree_backed () = delete;

      vector_or_kdtree_backed (std::vector<Vector>&& elements) noexcept {
        assert (elements.size() > 0);
        size_t m = elements.size ();
        size_t dim = elements[0].size ();
        data_do (std::cout << "|VEKD: downset_size="
                         << dim << "," << m << "|" << std::endl);

        // NOTE: we are checking the size BEFORE we actually create the
        // downset container; their respective constructors may reduce the
        // size by removing dominated elements... it's easier and clearer
        // to do the check here though
        if (KD_THRESH(m, dim)) {
          this->kdtree = std::make_unique<kdtree_backed<Vector>> (std::move (elements));
          data_do (std::cout << "VEKD: created kd-tree downset" << std::endl);
        } else {
          this->vector = std::make_unique<vector_backed<Vector>> (std::move (elements));
          data_do (std::cout << "VEKD: created vector downset" << std::endl);
        }
      }

      vector_or_kdtree_backed (Vector&& el) {
        // too small, just use a vector
        this->vector = std::make_unique<vector_backed<Vector>> (std::move (el));
      }

      template <typename F>
      auto apply (const F& lambda) const {
        const std::vector<Vector>& backing_vector =
          this->kdtree != nullptr ?
          this->kdtree->tree.vector_set :
          this->vector->vector_set;
        std::vector<Vector> ss;
        ss.reserve (backing_vector.size ());

        for (const auto& v : backing_vector)
          ss.push_back (lambda (v));

        return vector_or_kdtree_backed (std::move (ss));
      }

      vector_or_kdtree_backed (const vector_or_kdtree_backed&) = delete;
      vector_or_kdtree_backed (vector_or_kdtree_backed&&) = default;
      vector_or_kdtree_backed& operator= (const vector_or_kdtree_backed&) = delete;
      vector_or_kdtree_backed& operator= (vector_or_kdtree_backed&&) = default;

      bool contains (const Vector& v) const {
        if (this->kdtree != nullptr)
          return this->kdtree->contains (v);
        else
          return this->vector->contains (v);
      }

      /* Union in place
       * We use kd-trees only if both are already given as kd-trees
       * and their difference in size is subexponential
       */
      void union_with (vector_or_kdtree_backed&& other) {
        boolop_with (std::move (other), false);  // not intersection
      }

      /* Intersection in place
       */
      void intersect_with (vector_or_kdtree_backed&& other) {
        boolop_with (std::move (other), true);  // it is intersection
      }

      auto size () const {
        return this->kdtree != nullptr? this->kdtree->size () : this->vector->size ();
      }

      auto        begin ()       { return this->kdtree != nullptr?
                                          this->kdtree->begin () :
                                          this->vector->begin (); }
      const auto  begin () const { return this->kdtree != nullptr?
                                          this->kdtree->begin () :
                                          this->vector->begin (); }
      auto        end ()         { return this->kdtree != nullptr?
                                          this->kdtree->end () :
                                          this->vector->end (); }
      const auto  end () const   { return this->kdtree != nullptr?
                                          this->kdtree->end () :
                                          this->vector->end (); }
  };

  template <typename Vector>
  inline std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<Vector>& f) {
    if (f.kdtree != nullptr)
      os << *(f.kdtree) << std::endl;
    else
      for (auto&& el : *(f.vector))
        os << el << std::endl;
    return os;
  }
}
