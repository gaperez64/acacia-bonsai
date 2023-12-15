#pragma once

#include <cassert>
#include <iostream>
#include <math>
#include <vector>

#include <utils/vector_mm.hh>

#include <kdtree_backed.hh>
#include <vector_backed.hh>

namespace downsets {
  // Forward definition for the operator<<s.
  template <typename>
  class vector_or_kdtree_backed;

  template <typename Vector>
  std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<Vector>& f);

  template <typename Vector>
  class vector_or_kdtree_backed {
    private:
      std::shared_ptr<vector_backed<Vector>> vector;
      std::shared_ptr<kdtree_backed<Vector>> kdtree;

      template <typename V>
      friend std::ostream& operator<<(std::ostream& os, const vector_or_kdtree_backed<V>& f);

      void boolop_with (const vector_or_kdtree_backed& other, bool inter = false) {
        if (this->kdtree == nullptr) {
          assert (this->vector != nullptr);
          if (other.kdtree != nullptr) {
            assert (other->vector == nullptr);
            // this costs linear time already: reinterpret the kdtree as a
            // vector
            vector_backed<Vector> B = vector_backed<Vector> (other.tree.vector_set);
            if (inter)
              this->vector->intersect_with (B);
            else
              this->vector->union_with (B); 
          }
        }
        if (other.kdtree == nullptr) {
          assert (other.vector != nullptr);
          if (this->kdtree != nullptr) {
            assert (this->vector == nullptr);
            this->vector = vector_backed<Vector> (this->tree.vector_set);
            this->kdtree = nullptr;
            if (inter)
              this->vector->intersect_with (other.vector);
            else
              this->vector->union_with (other.vector);
          }
        }

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
            this->kdtree->intersect_with (other.kdtree);
          else
            this->kdtree->union_with (other.kdtree);
        } else {
          this->vector = vector_backed<Vector> (this->tree.vector_set);
          this->kdtree = nullptr;
          vector_backed<Vector> B = vector_backed<Vector> (other.tree.vector_set);
          if (inter)
            this->vector->intersect_with (B);
          else
            this->vector->union_with (B);
        }
      }

    public:
      typedef Vector value_type;

      vector_or_kdtree_backed () = delete;

      vector_or_kdtree_backed (std::vector<Vector>&& elements) noexcept {
        assert (elements.size() > 0);
        size_t m = elements.size ();
        size_t dim = elements[0].size ();
        if (exp (dim) < m)
          this->tree = std::make_shared<kdtree_backed<Vector>> (elements);
        else
          this->vector = std::make_shared<vector_backed<Vector>> (elements);
      }

      vector_or_kdtree_backed (Vector&& el) {
        // too small, just use a vector
        this->vector = std::make_shared<vector_backed<Vector>> (v);
      }

      // FIXME: the result of the application may not be an antichain!
      template <typename F>
      auto apply (const F& lambda) const {
        std::vector<Vector> backing_vector;
        if (this->tree != nullptr)
          backing_vector = this->tree.vector_set;
        else
          backing_vector = this->vector.vector_set;
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
          return this->tree->contains (v);
        else
          return this->vector->contains (v);
      }

      /* Union in place
       * We use kd-trees only if both are already given as kd-trees
       * and their difference in size is subexponential
       */
      void union_with (const vector_or_kdtree_backed& other) {
        boolop_with (other, false);  // not intersection
      }

      /* Intersection in place
       */
      void intersect_with (const kdtree_backed& other) {
        boolop_with (other, true);  // it is intersection
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
    if (this->kdtree != nullptr)
      os << *(f.kdtree) << std::endl;
    else
      for (auto&& el : f.vector)
        os << el << std::endl;
    return os;
  }
}
