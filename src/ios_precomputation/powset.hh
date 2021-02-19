#pragma once

#include "utils/transition_enumerator.hh"

namespace ios_precomputation {
  namespace detail {
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
        powset (Aut aut, bdd input_support, bdd output_support, int verbose) :
          aut {aut}, input_support {input_support}, output_support {output_support}, verbose {verbose}
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
        const int verbose;
    };

  }

  struct powset {
      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support, int verbose) {
        return detail::powset<Aut, TransSet> (aut, input_support, output_support, verbose);
      }
  };
}
