#include <cassert>
#include <memory>
#include <ostream>
#include <set>
#include <vector>

#include "set/kdtree_set.hh"
#include "vector/vector.hh"


using ab_vector = vector::simd_vector<char>;

ab_vector vtov (const std::vector<int>& v) {
  ab_vector out (v.size ());

  for (size_t i = 0; i < v.size (); ++i)
    out[i] = v[i];
  return out;
}

std::vector<ab_vector> vvtovv (const std::vector<std::vector<int>>& vv) {
  std::vector<ab_vector> out;
  out.reserve (vv.size ());

  for (size_t i = 0; i < vv.size (); ++i)
    out[i] = vtov (vv[i]);
  return out;
}

int main() {
  const size_t dim = 3;
  ab_vector v1(dim);
  v1[0] = 1; v1[1] = 2; v1[2] = 3;
  ab_vector v2(dim);
  v2[0] = 2; v2[1] = 5; v2[2] = 1;
  ab_vector v3(dim);
  v3[0] = 4; v3[1] = 1; v3[2] = 1;
  /*
   * v1 = (1, 2, 3)
   * v2 = (2, 5, 1)
   * v3 = (4, 1, 1)
   */

  // std::cout << "We have individual vectors!" << std::endl;

  std::vector<ab_vector> elements;
  elements.emplace_back(v1.copy ());
  elements.emplace_back(v2.copy ());
  elements.emplace_back(v3.copy ());

  /*
   std::cout << "We have a vector of vectors now! ";
   for (auto e : elements)
   std::cout << e << " ";
   std::cout << std::endl;
   */

  set::kdtree_set<ab_vector> set_one_elt (v1.copy ());
  set_one_elt.union_with (set_one_elt);
  assert (set_one_elt.contains (v1));
  assert (not set_one_elt.contains (v2));
  assert (set_one_elt.size () == 1);
  set_one_elt.intersect_with (set_one_elt);
  assert (set_one_elt.size () == 1);
  set_one_elt = set_one_elt.apply ([] (const ab_vector& v) { return v.copy (); });
  assert (set_one_elt.size () == 1);
  assert (set_one_elt.contains (v1));
  assert (not set_one_elt.contains (v2));

  set::kdtree_set<ab_vector> set (std::move (elements));
  set.union_with (set);
  assert (set.size () == 3);
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
  auto tree = utils::kdtree<ab_vector> (vvtovv ({
        {1, 2, 3,},
        {2, 5, 1},
        {4, 1, 1},
      }), 5);
  assert (tree.size () == 3);
  assert (tree.dominates (vtov ({1, 2, 1}), true));
  assert (tree.dominates (vtov ({1, 1, 1}), true));
  assert (tree.dominates (vtov ({2, 1, 1}), true));
  set.intersect_with (set);
  assert (set.size () == 3);
  set = set.apply ([] (const ab_vector& v) { return v.copy (); });
  assert (set.size () == 3);

  // std::cout << "We built the kdtree_set!" << std::endl;

  ab_vector v4(dim);
  v4[0] = 0; v4[1] = 1; v4[2] = 2;
  ab_vector v5(dim);
  v5[0] = 1; v5[1] = 1; v5[2] = 10;

  /*
   std::cout << "Is " << v2 << " in the closure? " << set.contains(v2) << std::endl;
   std::cout << "Is " << v4 << " in the closure? " << set.contains(v4) << std::endl;
   std::cout << "Is " << v5 << " in the closure? " << set.contains(v5) << std::endl;
   */


  assert(set.contains(v2));
  assert(set.contains(v4));
  assert(!set.contains(v5));


  std::vector<ab_vector> others;
  others.emplace_back(v4.copy ());
  others.emplace_back(v5.copy ());

  set::kdtree_set<ab_vector> other_set (std::move (others));

  /*
   std::cout << "We built another kdtree_set! for ";
   for (auto e : others)
   std::cout << e << " ";
   std::cout << std::endl;
   */

  set.union_with (other_set);

  /*
   std::cout << "We built a union kdtree_set of size " << set.size () << std::endl;
   std::cout << "Is " << v2 << " in the union? " << set.contains (v2) << std::endl;
   std::cout << "Is " << v4 << " in the union? " << set.contains (v4) << std::endl;
   std::cout << "Is " << v5 << " in the union? " << set.contains (v5) << std::endl;
   */
  assert(set.contains(v2));
  assert(set.contains(v4));
  assert(set.contains(v5));

  // std::cout << "other set before intersection " << other_set << std::endl;
  // std::cout << "set before intersection " << set << std::endl;
  other_set.intersect_with(set);

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
    auto tree = utils::kdtree<ab_vector> (vvtovv ({
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


  auto F1i = set::kdtree_set<ab_vector> (vvtovv ({
        {7, 0, 9, 9, 7},
        {8, 0, 9, 9, 8},
        {9, 0, 7, 7, 9}
      }));
  auto F = set::kdtree_set<ab_vector> (vvtovv ({
        {7, 0, 9, 9, 7},
        {8, 0, 9, 9, 8},
        {9, 0, 7, 7, 9}
      }));

  F.intersect_with (F1i);
  assert (F.size () == 2);

  return 0;
}
