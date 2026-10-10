#pragma once

#include <unordered_set>

#include <spot/misc/optionmap.hh>
#include <spot/tl/nenoform.hh>
#include <spot/tl/simplify.hh>
#include <spot/tl/unabbrev.hh>
#include <spot/twa/acc.hh>
#include <vector>

namespace acacia::translation {
  inline unsigned acceptance_set_width () { return spot::acc_cond::mark_t::max_accsets (); }

  inline size_t distinct_promises (spot::formula formula) {
    formula = spot::negative_normal_form (spot::unabbreviate (formula, "ei^"));
    std::unordered_set<spot::formula> seen;
    std::vector<spot::formula> pending {formula};
    size_t count = 0;
    while (not pending.empty ()) {
      const auto node = pending.back ();
      pending.pop_back ();
      if (not seen.insert (node).second)
        continue;
      count += node.is (spot::op::F, spot::op::U, spot::op::M);
      for (const auto child : node)
        pending.push_back (child);
    }
    return count;
  }

  inline bool needs_event_univ (size_t promises) {
    // The only threshold is Spot's configured acceptance representation width.
    return promises > acceptance_set_width ();
  }

  inline spot::formula favor_event_univ (spot::formula formula) {
    // Match the translator's tls-impl=1 simplification: syntactic rules, without
    // language-containment checks. Only favor_event_univ is additionally enabled.
    spot::tl_simplifier_options options;
    options.containment_max_states = 64;
    options.favor_event_univ = true;
    spot::tl_simplifier simplifier (options);
    return simplifier.simplify (formula);
  }

  inline spot::option_map make_options () {
    spot::option_map options;
    options.set ("simul", 0);
    options.set ("ba-simul", 0);
    options.set ("det-simul", 0);
    options.set ("tls-impl", 1);
    options.set ("wdba-minimize", 2);
    return options;
  }

  // spot::translator consumes its known options in its constructor.  Calling
  // this immediately afterward turns unknown names into a visible exception
  // instead of silently benchmarking a misspelled no-op.
  inline void validate_options (const spot::option_map& options) {
    options.report_unused_options ();
  }
}  // namespace acacia::translation
