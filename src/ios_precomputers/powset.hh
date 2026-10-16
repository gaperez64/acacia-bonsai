#pragma once


#include <vector>
#include <list>
#include <bddx.h>

#include "solver/transition_payload.hh"
#include "utils/transition_enumerator.hh"

namespace ios_precomputers {
  namespace detail {
    /** Compute a unambiguous refinement of a collection of sets.

     *Definitions.*

     - A collection C of sets over a finite universe U is *unambiguous* if all
       sets in C are pairwise disjoint.  In other words, C is a partition of the
       union of its sets.

     - A collection C' refines a collection C if each set in C can be obtained
       as the union of sets in C'.

     *Problem.*

     Given a collection C, compute a (minimal) unambiguous refinement of C.

     [[For complexity purposes, the computational task can also be written as:

     Given C and k, does there exist an unambiguous refinement of C of size < k?

     This is certainly NP-complete ([SP7] in Garey and Johnson is that problem without
     "unambiguous").]]

     *Motivation.*

     With BDDs, the use case is to have C be a collection of BDDs and a BDD
     represent the set of its satisfying assignments.

     Building an unambiguous refinement of BDDs seems an essential step in
     determinization: If I have a state with two outgoing transitions labeled
     with BDDs {F, G} such that F&G is not False, then this state is
     nondeterministic.  To determinize it, we will have to consider the
     transition labels {F&G, F&!G, !F&G}.  Note that this set refines {F, G},
     since F = (F&G)|(F&!G) and similarly for G.

     *Use case.*

     The use case here is to consider the set of all transition labels (it's a
     collection C of BDDs) and find an unambiguous refinement of it, in order to
     find sets of transitions that can be taken using a full valuation.

     *Implementations.*

    */
    template <typename RetSet, typename FormSet, typename Projection>
    auto power (const FormSet& formulas_to_transs, const Projection& projection) {
      RetSet powset;

      powset.push_back (typename RetSet::value_type (bddtrue, {}));

      for (const auto& [formula, transs] : formulas_to_transs) {
        auto mod = projection (formula);

        for (auto it = powset.begin (); it != powset.end (); /* in-loop update */) {
          auto mod_and_it = mod & it->first;
          if (mod_and_it != bddfalse) {
            auto notmod_and_it = (!mod) & it->first;
            if (notmod_and_it != bddfalse) {  // SPLIT
              powset.emplace_front (mod_and_it, it->second);
              powset.front ().second.push_back (transs);
              powset.emplace_front (notmod_and_it, std::move (it->second));
              it = powset.erase (it);
            }
            else {
              it->first = mod_and_it;
              it->second.push_back (transs);
              ++it;
            }
          }
          else
            ++it;
        }
      }
      return powset;
    }

  }  // namespace detail
}  // namespace ios_precomputers
