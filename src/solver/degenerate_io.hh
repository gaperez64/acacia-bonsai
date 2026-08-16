#pragma once

#include "solver/create_automaton.hh"
#include "solver/syntactic_bypass.hh"
#include "solver/translator_options.hh"

#include <spot/tl/formula.hh>
#include <spot/twaalgos/postproc.hh>
#include <spot/twaalgos/translate.hh>

#include <string>
#include <vector>

namespace acacia::degenerate_io {
  using verdict = syntactic_bypass::verdict;

  inline verdict try_direct (spot::formula formula,
                             const std::vector<std::string>& input_aps,
                             const std::vector<std::string>& output_aps,
                             spot::postprocessor::output_pref translation_pref) {
    if (not input_aps.empty () and not output_aps.empty ())
      return verdict::unknown;

    // With no outputs the environment chooses the complete word, so the
    // specification is realizable exactly when its negation is empty.  With
    // no inputs the system chooses the complete word, so ordinary language
    // non-emptiness is enough.  When both alphabets are empty either view is
    // equivalent; prefer the universality formulation.
    const bool universality = output_aps.empty ();
    if (universality)
      formula = spot::formula::Not (formula);

    auto dict = spot::make_bdd_dict ();
    auto options = translation::make_options ();
    spot::translator trans (dict, &options);
    translation::validate_options (options);
    auto aut = create_automaton (formula, trans, translation_pref);
    const bool language_nonempty = aut->num_states () != 0 and not aut->is_empty ();
    const bool realizable = universality ? not language_nonempty : language_nonempty;
    return realizable ? verdict::realizable : verdict::unrealizable;
  }
}  // namespace acacia::degenerate_io
