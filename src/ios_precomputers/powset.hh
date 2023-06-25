#pragma once

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

     This is certainly NP-complete ([SP7] in Garey and Johnson is that problem without "unambiguous").]]

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

     Two implementations are provided; powset.hh and fake_vars.hh:

     - powset.hh:

     1. C' = {U}  (recall that U is our universe, with BDDs this would be True)
     2. for each S in C:
     3.   for each S' in C':
     4.     if (S cap S' is not empty)
     5.        Remove S' from C'
     6.        Add (S cap S') and (S^c cap S') to C' (^c denotes complement).

     - fake_vars.hh:

     I encode C' as a BDD itself, introducing lots of fresh variables; namely,
     if C' = {F, G} for two BDDs F and G, I introduce two fresh variables X_F
     and X_G, and encode C' as (F & X_F) | (G & X_G).  This allows lines 3 and 4
     to become:

     3. Compute C' & S and project it on the X_? variables.
     4. Iterate through each of the variables that appear: if X_S' appears, this means that S cap S' is nonempty.
     */
    template <typename RetSet, typename FormSet, typename Projection>
    auto power (const FormSet& formulas_to_transs,
                const Projection& projection) {
      RetSet powset;

      powset.push_back (typename RetSet::value_type (bddtrue, {}));

      for (const auto& [formula, transs] : formulas_to_transs) {
        auto mod = projection (formula);

        for (auto it = powset.begin (); it != powset.end (); /* in-loop update */) {
          auto mod_and_it = mod & it->first;
          if (mod_and_it != bddfalse) {
            auto notmod_and_it = (!mod) & it->first;
            if (notmod_and_it != bddfalse) { // SPLIT
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

    template <typename Aut, typename TransSet>
    class powset {
      public:
        powset (Aut aut, bdd input_support, bdd output_support) :
          aut {aut}, input_support {input_support}, output_support {output_support}
        {}

        auto operator() () const
        {
          using crossings_t = typename std::list<std::pair<bdd, TransSet>>;
          using input_to_ios_t = typename std::list<std::pair<bdd, std::list<TransSet>>>;

          auto crossings = power<crossings_t> (
            transition_enumerator (aut, transition_formater::src_and_dst (aut)),
            [] (bdd b) { return b; });

          return power<input_to_ios_t> (crossings,
                                        [this] (bdd b) {
                                          return bdd_exist (b, output_support);
                                        });
        }

      private:
        Aut aut;
        const bdd input_support, output_support;
    };

  }

  struct powset {
    static const bool supports_invariant = false;

      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return detail::powset<Aut, TransSet> (aut, input_support, output_support);
      }
  };
}
