#pragma once

// Sprint A, stage A0: the inclusion-dominance half of the semantic-action
// census.  The equality half -- how many distinct residual relations the output
// paths actually reach -- is counted by the alphabet census, which already
// walks the same DAG.  This header adds only what dominance needs, because
// dominance costs real BDD operations and is therefore opt-in.
//
// Theory, for the state-based acceptance the MONA path always uses: two
// complete output letters induce the same bounded-rank action iff they induce
// the same normalized endpoint relation, and controller action `a` is globally
// dominated by `b` iff the successor relation of `b` is contained in that of
// `a`.  More universal successors is worse for the controller, so the useful
// survivors are the inclusion-MINIMAL residual relations.

#include "solver/diagnostics.hh"

#include <bddx.h>

#include <cstddef>
#include <cstdlib>
#include <vector>

#if ACACIA_ENABLE_DIAGNOSTICS

namespace ios_precomputers {

  struct dominance_budget {
      // Per-input caps.  All-pairs dominance is quadratic in the number of
      // distinct residual roots, and each test is a BDD operation whose cost
      // depends on the relation size, so a test count alone does not bound the
      // work.  The wall-clock cap is the guard that actually holds; the test
      // cap only avoids starting a pass that obviously cannot finish.
      std::size_t max_tests = 200000;
      std::size_t max_ms = 2000;
  };

  /// Budget from the environment, so a census run can trade coverage against
  /// time without a rebuild.
  inline dominance_budget dominance_budget_from_env () {
    dominance_budget budget;
    if (const char* tests = std::getenv ("ACACIA_DIAG_SEMANTIC_DOMINANCE_TESTS");
        tests != nullptr and *tests != '\0')
      budget.max_tests = (std::size_t) std::strtoull (tests, nullptr, 10);
    if (const char* ms = std::getenv ("ACACIA_DIAG_SEMANTIC_DOMINANCE_MS");
        ms != nullptr and *ms != '\0')
      budget.max_ms = (std::size_t) std::strtoull (ms, nullptr, 10);
    return budget;
  }

  struct dominance_result {
      std::size_t minimal = 0;   ///< survivors; equals `roots.size ()` when declined
      std::size_t tests = 0;     ///< subset tests actually performed
      bool declined = false;     ///< the budget ran out before a verdict
  };

  /// Keep the inclusion-minimal relations among `roots`, in stable
  /// first-occurrence order.  `roots` must already be deduplicated by node
  /// identity; BuDDy nodes are canonical, so distinct nodes are distinct
  /// relations and equality needs no BDD operation.
  inline dominance_result minimal_by_inclusion (const std::vector<bdd>& roots,
                                                const dominance_budget& budget) {
    dominance_result result;
    if (roots.size () <= 1) {
      result.minimal = roots.size ();
      return result;
    }

    // A quadratic pass is only worth starting if it can finish: n*(n-1) is the
    // worst-case test count, and declining early keeps the census honest about
    // which inputs it could not resolve.
    const std::size_t n = roots.size ();
    if (n > 1 and (n - 1) > budget.max_tests / n) {
      result.minimal = n;
      result.declined = true;
      return result;
    }

    using clock = acacia::diagnostics::clock;
    const auto started = clock::now ();
    const auto over_time = [&] {
      return (std::size_t) std::chrono::duration_cast<std::chrono::milliseconds> (
                 clock::now () - started)
                 .count ()
             >= budget.max_ms;
    };

    std::vector<bdd> kept;
    kept.reserve (n);
    for (const bdd& candidate : roots) {
      // Check the clock once per candidate rather than once per test: the
      // inner loop is already bounded by |kept|, and a per-test clock read
      // would show up in the very timings this census reports.
      if (over_time ()) {
        result.minimal = n;
        result.declined = true;
        return result;
      }
      bool dominated = false;
      for (std::size_t i = 0; i < kept.size (); ) {
        ++result.tests;
        // kept[i] subset of candidate: the candidate has the larger successor
        // relation and is therefore the worse controller action.
        if ((kept[i] - candidate) == bddfalse) {
          dominated = true;
          break;
        }
        ++result.tests;
        if ((candidate - kept[i]) == bddfalse)
          kept.erase (kept.begin () + static_cast<std::ptrdiff_t> (i));
        else
          ++i;
      }
      if (not dominated)
        kept.push_back (candidate);
    }

    result.minimal = kept.size ();
    return result;
  }

}  // namespace ios_precomputers

#endif  // ACACIA_ENABLE_DIAGNOSTICS
