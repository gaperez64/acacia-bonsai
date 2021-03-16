#include <cassert>
#include <memory>
#include <ostream>
#include <set>
#include <vector>
#include <string>
#include <type_traits>
#include <cxxabi.h>

#include "test_maker.hh"

#include "antichains.hh"
#include "vectors.hh"

template<class T, class = void>
struct has_insert : std::false_type {};

template <class T>
struct has_insert<T, std::void_t<decltype(std::declval<T>().insert (std::declval<typename T::value_type> ()))>> : std::true_type {};

template<typename SetType>
struct test_t : public generic_test_t {
    using VType = typename SetType::value_type;

    VType vtov (const std::vector<int>& v) {
      VType out (v.size ());

      for (size_t i = 0; i < v.size (); ++i)
        out[i] = v[i];
      return out;
    }

    std::vector<VType> vvtovv (const std::vector<std::vector<int>>& vv) {
      std::vector<VType> out;
      //out.reserve (vv.size ());

      for (size_t i = 0; i < vv.size (); ++i)
        out.emplace_back(vtov (std::move (vv[i]))); //out[i] = vtov<VType> (vv[i]);
      return out;
    }

    SetType vec_to_set (std::vector<VType>&& v) {
      if constexpr (has_insert<SetType>::value) {
        SetType set (std::move (v[0]));
        for (size_t i = 1; i < v.size (); ++i)
          set.insert (std::move (v[i]));
        return set;
      }
      else
        return SetType (std::move (v));
    }

    void operator() () {
      const size_t dim = 3;
      VType v1(dim);
      v1[0] = 1; v1[1] = 2; v1[2] = 3;
      VType v2(dim);
      v2[0] = 2; v2[1] = 5; v2[2] = 1;
      VType v3(dim);
      v3[0] = 4; v3[1] = 1; v3[2] = 1;
      /*
       * v1 = (1, 2, 3)
       * v2 = (2, 5, 1)
       * v3 = (4, 1, 1)
       */

      // std::cout << "We have individual vectors!" << std::endl;

      /*
       std::cout << "We have a vector of vectors now! ";
       for (auto e : elements)
       std::cout << e << " ";
       std::cout << std::endl;
       */

      assert(v1.size () > 0);
      SetType set_one_elt (v1.copy ());
      SetType set_one_elt_cpy (v1.copy ());
      set_one_elt.union_with (std::move (set_one_elt_cpy));
      assert (set_one_elt.contains (v1));
      assert (not set_one_elt.contains (v2));
      assert (set_one_elt.max_elements ().size () == 1);
      SetType set_one_elt_cpy2 (v1.copy ());
      set_one_elt.intersect_with (std::move (set_one_elt_cpy2));
      assert (set_one_elt.max_elements ().size () == 1);
      set_one_elt = set_one_elt.apply ([] (const VType& v) { return v.copy (); });
      assert (set_one_elt.max_elements ().size () == 1);
      assert (set_one_elt.contains (v1));
      assert (not set_one_elt.contains (v2));

      std::vector<VType> elements;
      elements.emplace_back(v1.copy ());
      elements.emplace_back(v2.copy ());
      elements.emplace_back(v3.copy ());

      std::vector<VType> elements_cpy;
      elements_cpy.emplace_back(v1.copy ());
      elements_cpy.emplace_back(v2.copy ());
      elements_cpy.emplace_back(v3.copy ());

      SetType set = vec_to_set (std::move (elements));
      SetType set_cpy = vec_to_set (std::move (elements_cpy));
      set.union_with (std::move (set_cpy));
      assert (set.max_elements ().size () == 3);
      /*
       * Next, we will try S & S = S.
       * Remember that: S = {
       * v1 = (1, 2, 3),
       * v2 = (2, 5, 1),
       * v3 = (4, 1, 1),
       * }
       * so the "meets" will be S union {
       * v1 & v2 = (1, 2, 1)
       * v1 & v3 = (1, 1, 1)
       * v2 & v3 = (2, 1, 1)
       * }
       * let's make sure these will be eliminated inside the intersection...
       */
      auto tree = utils::kdtree<VType> (vvtovv ({
            {1, 2, 3},
            {2, 5, 1},
            {4, 1, 1},
          }), 3);
      assert (tree.size () == 3);
      assert (tree.dominates (vtov ({1, 2, 1}), true));
      assert (tree.dominates (vtov ({1, 1, 1}), true));
      assert (tree.dominates (vtov ({2, 1, 1}), true));
      set = set.apply ([] (const VType& v) { return v.copy (); });
      assert (set.max_elements ().size () == 3);

      // std::cout << "We built the kdtree!" << std::endl;

      VType v4(dim);
      v4[0] = 0; v4[1] = 1; v4[2] = 2;
      VType v5(dim);
      v5[0] = 1; v5[1] = 1; v5[2] = 10;

      /*
       std::cout << "Is " << v2 << " in the closure? " << set.contains(v2) << std::endl;
       std::cout << "Is " << v4 << " in the closure? " << set.contains(v4) << std::endl;
       std::cout << "Is " << v5 << " in the closure? " << set.contains(v5) << std::endl;
       */


      assert(set.contains(v2));
      assert(set.contains(v4));
      assert(!set.contains(v5));


      std::vector<VType> others;
      others.emplace_back(v4.copy ());
      others.emplace_back(v5.copy ());

      SetType set2 = vec_to_set (std::move (others));

      /*
       std::cout << "We built another kdtree_set! for ";
       for (auto e : others)
       std::cout << e << " ";
       std::cout << std::endl;
       */

      set.union_with (std::move (set2));

      /*
       std::cout << "We built a union kdtree_set of size " << set.size () << std::endl;
       std::cout << "Is " << v2 << " in the union? " << set.contains (v2) << std::endl;
       std::cout << "Is " << v4 << " in the union? " << set.contains (v4) << std::endl;
       std::cout << "Is " << v5 << " in the union? " << set.contains (v5) << std::endl;
       */
      assert(set.contains(v2));
      assert(set.contains(v4));
      assert(set.contains(v5));

      std::vector<VType> others2;
      others2.emplace_back(v4.copy ());
      others2.emplace_back(v5.copy ());
      SetType other_set = vec_to_set (std::move (others2));

      /*
       std::cout << "We built the intersection of last two sets, size = "
       << other_set.size () << std::endl;

       std::cout << "Is " << v2 << " in the intersection? " << other_set.contains (v2) << std::endl;
       std::cout << "Is " << v4 << " in the intersection? " << other_set.contains (v4) << std::endl;
       std::cout << "Is " << v5 << " in the intersection? " << other_set.contains (v5) << std::endl;
       */
      assert(!other_set.contains(v2));
      assert(other_set.contains(v4));
      assert(other_set.contains(v5));

      // std::cout << "made it to other tests!" << std::endl;
      {
        auto tree = utils::kdtree (vvtovv ({
              {7, 0, 9, 9, 7},
              {8, 0, 7, 7, 8},
              {8, 0, 9, 9, 8},
              {9, 0, 7, 7, 9}
            }), 5);
        assert (tree.size () == 4);
        assert (tree.dominates (vtov ({0, 0, 0, 0, 0}), true));
        assert (tree.dominates (vtov ({6, 0, 9, 9, 7}), true));
        assert (tree.dominates (vtov ({7, 0, 9, 9, 7}), true));
      }


      // Full set is so slow, this will never finish.
      if constexpr (std::is_same<SetType, antichains::full_set<VType>>::value)
                     return;


      auto F1i = vec_to_set (vvtovv ({
            {7, 0, 9, 9, 7},
            {8, 0, 9, 9, 8},
            {9, 0, 7, 7, 9}
          }));
      auto F = vec_to_set (vvtovv ({
            {7, 0, 9, 9, 7},
            {8, 0, 9, 9, 8},
            {9, 0, 7, 7, 9}
          }));

      F.intersect_with (F1i);
      assert (F.max_elements ().size () == 2);

      {
        auto F = vec_to_set (vvtovv ({
              {0, 7, 0, 0, 9, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {0, 8, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {-1, 8, -1, 0, 9, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {-1, 7, -1, 0, -1, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {-1, 8, -1, 0, -1, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
              {-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
            }));
        assert (F.contains (vtov ({-1, 9, -1, 0, -1, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})));
      }
    }

};



void usage (const char* progname) {
  std::cout << "usage: " << progname << " SETTYPE VECTYPE" << std::endl;

  std::cout << "  SETTYPE:\n  - all" << std::endl;
  for (auto&& s : set_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('<') - start) << std::endl;
  }

  std::cout << "  VECTYPE:\n  - all" << std::endl;
  for (auto&& s : vector_names) {
    auto start = s.find_last_of (':') + 1;
    std::cout << "  - " << s.substr (start, s.find_first_of ('<') - start) << std::endl;
  }

  exit (0);
}

using vector_types = type_list<vectors::simd_vector_backed<char>,
                               vectors::vector_backed<char>>;

using set_types = template_type_list<antichains::full_set,
                                     antichains::kdtree_backed,
                                     antichains::set_backed,
                                     antichains::vector_backed>;

int main(int argc, char* argv[]) {
  register_maker ((vector_types*) 0, (set_types*) 0);

  if (argc != 3)
    usage (argv[0]);

  auto implem = std::string ("antichains::") + argv[1] + "<vectors::" + argv[2] + "<char> >";

  try {
    test_makers[implem] ();
  } catch (std::bad_function_call& e) {
    std::cout << "error: no such implem: " << implem << std::endl;
  }

  return 0;
}
