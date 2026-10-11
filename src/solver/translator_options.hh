#pragma once

#include <unordered_set>

#include <spot/misc/optionmap.hh>
#include <spot/tl/nenoform.hh>
#include <spot/tl/simplify.hh>
#include <spot/tl/unabbrev.hh>
#include <spot/twa/acc.hh>
#include <vector>

namespace acacia::translation {
  enum class event_univ_policy { guarded, expanded };

  inline const char* event_univ_policy_name (event_univ_policy policy) {
    return policy == event_univ_policy::expanded ? "expanded" : "guarded";
  }

  inline size_t formula_dag_size (spot::formula formula) {
    std::unordered_set<spot::formula> seen;
    std::vector<spot::formula> pending {formula};
    while (not pending.empty ()) {
      const auto node = pending.back ();
      pending.pop_back ();
      if (seen.insert (node).second)
        for (const auto child : node)
          pending.push_back (child);
    }
    return seen.size ();
  }

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

  inline spot::formula favor_event_univ (spot::formula formula, bool favor = true) {
    // Match the translator's tls-impl=1 simplification: syntactic rules, without
    // language-containment checks. Only favor_event_univ is additionally enabled.
    // Spot exposes no cooperative cancellation for this syntactic call. Expanded
    // probing is opt-in, with no clock allowance or outer-deadline dependency.
    // All simplifier state is destroyed on return, before trans.run.
    spot::tl_simplifier_options options;
    options.containment_max_states = 64;
    options.favor_event_univ = favor;
    spot::tl_simplifier simplifier (options);
    return simplifier.simplify (formula);
  }

  struct event_univ_result {
      spot::formula formula;
      bool changed;
  };

  inline event_univ_result probe_event_univ_input (spot::formula formula, size_t promises) {
    const auto rewritten = favor_event_univ (formula);
    const auto ordinary = favor_event_univ (formula, false);
    const bool changed = rewritten != ordinary;
    // The census also probes above the width, retaining the guarded rewrite.
    return {needs_event_univ (promises) || changed ? rewritten : formula, changed};
  }

  inline event_univ_result event_univ_input (spot::formula formula, size_t promises,
                                            event_univ_policy policy) {
    if (needs_event_univ (promises)) {
      auto rewritten = favor_event_univ (formula);
      return {rewritten, rewritten != formula};
    }
    if (policy == event_univ_policy::expanded)
      return probe_event_univ_input (formula, promises);
    return {formula, false};
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
