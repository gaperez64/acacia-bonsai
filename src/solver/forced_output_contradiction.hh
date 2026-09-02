#pragma once

#include "solver/bounded_input_pattern.hh"

#include <bddx.h>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>

#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

// Soundness: alpha mentions only inputs, so the environment can force any
// satisfiable alpha.  The response then requires beta at some position, while
// the direct consequences G chi_i require chi_all at every position.  Thus an
// unsatisfiable chi_all & beta proves unrealizability.
namespace acacia::forced_output_contradiction {
  enum class response_kind { fixed_delay, eventual };

  struct witness {
    spot::formula invariant;
    spot::formula trigger;
    spot::formula forbidden_output;
    response_kind kind;
    unsigned delay;
  };

  struct result {
    bool unrealizable = false;
    std::optional<witness> proof;
    std::string decline_reason;
  };

  namespace detail {
    inline bool split_implication (const spot::formula& formula,
                                   spot::formula& left,
                                   spot::formula& right) {
      if (formula.is (spot::op::Implies)) {
        left = formula[0];
        right = formula[1];
        return true;
      }
      if (formula.is (spot::op::Or) and formula.size () == 2
          and formula[0].is (spot::op::Not)) {
        left = formula[0][0];
        right = formula[1];
        return true;
      }
      return false;
    }

    inline bool is_output_boolean (
        const spot::formula& formula,
        const std::unordered_set<std::string>& output_aps) {
      if (not formula.is_boolean ())
        return false;
      if (formula.is (spot::op::ap))
        return output_aps.contains (formula.ap_name ());
      for (spot::formula child : formula)
        if (not is_output_boolean (child, output_aps))
          return false;
      return true;
    }

    class output_translator {
      public:
        explicit output_translator (const std::vector<std::string>& output_aps) {
          for (const std::string& ap : output_aps)
            if (not variables_.contains (ap))
              variables_.emplace (ap, bdd_extvarnum (1));
        }

        std::optional<bdd> translate (const spot::formula& formula) const {
          switch (formula.kind ()) {
            case spot::op::tt:
              return bddtrue;
            case spot::op::ff:
              return bddfalse;
            case spot::op::ap: {
              auto variable = variables_.find (formula.ap_name ());
              if (variable == variables_.end ())
                return std::nullopt;
              return bdd_ithvar (variable->second);
            }
            case spot::op::Not: {
              auto child = translate (formula[0]);
              if (not child)
                return std::nullopt;
              return !*child;
            }
            case spot::op::And: {
              bdd result = bddtrue;
              for (spot::formula child : formula) {
                auto translated = translate (child);
                if (not translated)
                  return std::nullopt;
                result = bdd_and (result, *translated);
              }
              return result;
            }
            case spot::op::Or: {
              bdd result = bddfalse;
              for (spot::formula child : formula) {
                auto translated = translate (child);
                if (not translated)
                  return std::nullopt;
                result = bdd_or (result, *translated);
              }
              return result;
            }
            case spot::op::Xor: {
              auto left = translate (formula[0]);
              auto right = translate (formula[1]);
              if (not left or not right)
                return std::nullopt;
              return bdd_apply (*left, *right, bddop_xor);
            }
            case spot::op::Implies: {
              auto left = translate (formula[0]);
              auto right = translate (formula[1]);
              if (not left or not right)
                return std::nullopt;
              return bdd_apply (*left, *right, bddop_imp);
            }
            case spot::op::Equiv: {
              auto left = translate (formula[0]);
              auto right = translate (formula[1]);
              if (not left or not right)
                return std::nullopt;
              return bdd_apply (*left, *right, bddop_biimp);
            }
            default:
              return std::nullopt;
          }
        }

      private:
        std::map<std::string, int> variables_;
    };

    struct response {
      spot::formula trigger;
      spot::formula beta;
      response_kind kind;
      unsigned delay;
      bool trigger_satisfiable;
    };

    inline std::vector<spot::formula> collect_candidates (
        const spot::formula& formula) {
      std::vector<spot::formula> candidates;
      std::vector<spot::formula> worklist {formula};
      while (not worklist.empty ()) {
        spot::formula current = std::move (worklist.back ());
        worklist.pop_back ();
        if (current.is (spot::op::And)) {
          for (spot::formula child : current)
            worklist.push_back (child);
          continue;
        }
        if (not current.is (spot::op::G))
          continue;

        spot::formula body = current[0];
        candidates.push_back (body);
        if (body.is (spot::op::And))
          for (spot::formula child : body)
            worklist.push_back (spot::formula::G (child));
      }
      return candidates;
    }

    inline std::optional<response> classify_response (
        const spot::formula& formula,
        const std::vector<std::string>& input_aps,
        const std::unordered_set<std::string>& output_aps) {
      spot::formula trigger;
      spot::formula consequent;
      if (not split_implication (formula, trigger, consequent))
        return std::nullopt;

      auto trigger_satisfiable =
          bounded_input_pattern::is_satisfiable (trigger, input_aps);
      if (not trigger_satisfiable)
        return std::nullopt;

      if (consequent.is (spot::op::F)) {
        spot::formula beta = consequent[0];
        if (not is_output_boolean (beta, output_aps))
          return std::nullopt;
        return response {trigger, beta, response_kind::eventual, 0,
                         *trigger_satisfiable};
      }

      unsigned delay = 0;
      while (consequent.is (spot::op::X)) {
        ++delay;
        consequent = consequent[0];
      }
      if (not is_output_boolean (consequent, output_aps))
        return std::nullopt;
      return response {trigger, consequent, response_kind::fixed_delay, delay,
                       *trigger_satisfiable};
    }
  }  // namespace detail

  inline result try_direct (
      const spot::formula& effective_mealy_formula,
      const std::vector<std::string>& input_aps,
      const std::vector<std::string>& output_aps) {
    // Guarantee BuDDy initialization because this checker runs before the
    // solver creates its own dictionary.
    const spot::bdd_dict_ptr bdd_lifetime = spot::make_bdd_dict ();
    spot::formula ignored_left;
    spot::formula ignored_right;
    if (detail::split_implication (effective_mealy_formula, ignored_left,
                                   ignored_right))
      return {false, std::nullopt, "top-level implication"};

    const std::unordered_set<std::string> output_names (output_aps.begin (),
                                                         output_aps.end ());
    std::vector<spot::formula> invariants;
    std::vector<detail::response> responses;
    for (spot::formula candidate :
         detail::collect_candidates (effective_mealy_formula)) {
      if (detail::is_output_boolean (candidate, output_names)) {
        invariants.push_back (candidate);
        continue;
      }
      auto response = detail::classify_response (candidate, input_aps,
                                                  output_names);
      if (response)
        responses.push_back (std::move (*response));
    }

    detail::output_translator translator {output_aps};
    bdd chi_all = bddtrue;
    for (spot::formula invariant : invariants) {
      auto translated = translator.translate (invariant);
      if (not translated)
        return {false, std::nullopt, "no forced output contradiction"};
      chi_all = bdd_and (chi_all, *translated);
    }

    spot::formula invariant_body = spot::formula::And (invariants);
    for (const detail::response& response : responses) {
      if (not response.trigger_satisfiable)
        continue;
      auto beta = translator.translate (response.beta);
      if (not beta or bdd_and (chi_all, *beta) != bddfalse)
        continue;

      witness proof {spot::formula::G (invariant_body), response.trigger,
                     response.beta, response.kind, response.delay};
      return {true, std::move (proof), {}};
    }
    return {false, std::nullopt, "no forced output contradiction"};
  }
}  // namespace acacia::forced_output_contradiction
