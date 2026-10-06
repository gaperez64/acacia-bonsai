#pragma once

#include "solver/unreal_weakening_records.hh"

#include <algorithm>
#include <cstddef>
#include <spot/tl/formula.hh>
#include <utility>
#include <vector>

namespace acacia::unreal_witnesses {

  struct census {
      const char* reason = "eligible";
      size_t oversized_globals = 0, largest_global_conjunction_size = 0;
      size_t safety_conjuncts = 0, obligations = 0;
  };

  // A large G(a & b & ...) becomes a generalized-Buchi translation with one
  // acceptance set per liveness conjunct.  Spot is intentionally compiled
  // with a finite acceptance-set limit, so build a few sound, much smaller
  // unrealizability witnesses.  Each witness is safety_core & obligation, a
  // logical consequence of the original specification: proving one
  // unrealizable proves the original unrealizable, while an inconclusive
  // witness changes no verdict.
  inline std::vector<spot::formula> make_safety_core_witnesses (const spot::formula& formula,
                                                                size_t max_witnesses = 8,
                                                                census* observed = nullptr) {
    if (observed)
      *observed = {};
    if (max_witnesses == 0 or not formula.is (spot::op::And)) {
      if (observed)
        observed->reason = max_witnesses == 0 ? "zero_allowance" : "not_top_level_and";
      return {};
    }

    std::vector<spot::formula> conjuncts;
    bool has_oversized_global_conjunction = false;
    for (spot::formula conjunct : formula) {
      if (conjunct.is (spot::op::G) and conjunct[0].is (spot::op::And)) {
        has_oversized_global_conjunction =
            has_oversized_global_conjunction or conjunct[0].size () > 64;
        if (observed) {
          observed->largest_global_conjunction_size =
              std::max<size_t> (observed->largest_global_conjunction_size, conjunct[0].size ());
          if (conjunct[0].size () > 64)
            ++observed->oversized_globals;
        }
        for (spot::formula nested : conjunct[0])
          conjuncts.push_back (spot::formula::G (nested));
      }
      else {
        conjuncts.push_back (conjunct);
      }
    }
    if (not has_oversized_global_conjunction and not observed)
      return {};

    std::vector<spot::formula> safety_core;
    std::vector<spot::formula> obligations;
    for (spot::formula conjunct : conjuncts)
      if (conjunct.is_syntactic_safety ())
        safety_core.push_back (conjunct);
      else
        obligations.push_back (conjunct);
    if (observed) {
      observed->safety_conjuncts = safety_core.size ();
      observed->obligations = obligations.size ();
      observed->reason = observed->largest_global_conjunction_size == 0 ? "no_global_conjunction"
                         : not has_oversized_global_conjunction
                             ? "global_conjunction_threshold_not_met"
                         : safety_core.empty () ? "absent_safety_conjuncts"
                         : obligations.empty () ? "no_non_safety_obligations"
                                                : "eligible";
    }
    if (not has_oversized_global_conjunction or safety_core.empty () or obligations.empty ())
      return {};

    std::vector<spot::formula> witnesses;
    witnesses.reserve (std::min (max_witnesses, obligations.size ()));
    for (spot::formula obligation : obligations) {
      std::vector<spot::formula> parts = safety_core;
      parts.push_back (obligation);
      witnesses.push_back (spot::formula::And (std::move (parts)));
      if (witnesses.size () == max_witnesses)
        break;
    }
    return witnesses;
  }

  template <typename Runner>
  std::optional<bool> try_safety_core_witnesses (const spot::formula& formula, bool unreal,
                                                 bool synthesis, Runner& runner,
                                                 const records& observed) {
    census info;
    if (synthesis || !unreal) {
      info.reason = synthesis ? "synthesis_requested" : "not_unreal_worker";
      observed.eligibility (info.reason, 0, 0, 0, 0, 0);
      observed.finish ("ineligible");
      return std::nullopt;
    }
    auto witnesses = make_safety_core_witnesses (formula, 8, observed ? &info : nullptr);
    observed.eligibility (info.reason, info.oversized_globals,
                          info.largest_global_conjunction_size, info.safety_conjuncts,
                          info.obligations, witnesses.size ());
    if (observed)
      for (unsigned i = 0; i < witnesses.size (); ++i)
        observed.generated (witnesses[i], i);
    try {
      for (unsigned i = 0; i < witnesses.size (); ++i) {
        diagnostics::scoped_attempt diag_attempt;
        records::attempt attempt (observed, i);
        const bool result = runner (witnesses[i]);
        attempt.finish (result);
        if (result) {
          diag_attempt.commit ();
          observed.finish ("proof");
          return std::optional<bool> {true};
        }
      }
    } catch (...) {
      observed.finish ("exception");
      throw;
    }
    observed.finish (witnesses.empty () ? "ineligible" : "inconclusive");
    return std::nullopt;
  }

}  // namespace acacia::unreal_witnesses
