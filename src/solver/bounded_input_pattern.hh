#pragma once

#include <bddx.h>
#include <spot/tl/formula.hh>
#include <spot/twa/bdddict.hh>

#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace acacia::bounded_input_pattern {
  namespace detail {
    class translator {
      public:
        explicit translator (const std::vector<std::string>& input_aps)
          : input_aps_ (input_aps.begin (), input_aps.end ())
        {}

        std::optional<bdd> translate (const spot::formula& formula,
                                      unsigned offset = 0) {
          switch (formula.kind ()) {
            case spot::op::tt:
              return bddtrue;
            case spot::op::ff:
              return bddfalse;
            case spot::op::ap:
              return variable (formula.ap_name (), offset);
            case spot::op::Not: {
              if (not formula[0].is (spot::op::ap))
                return std::nullopt;
              auto child = translate (formula[0], offset);
              if (not child)
                return std::nullopt;
              return !*child;
            }
            case spot::op::And: {
              bdd result = bddtrue;
              for (spot::formula child : formula) {
                auto translated = translate (child, offset);
                if (not translated)
                  return std::nullopt;
                result = bdd_and (result, *translated);
              }
              return result;
            }
            case spot::op::Or: {
              bdd result = bddfalse;
              for (spot::formula child : formula) {
                auto translated = translate (child, offset);
                if (not translated)
                  return std::nullopt;
                result = bdd_or (result, *translated);
              }
              return result;
            }
            case spot::op::X:
              return translate (formula[0], offset + 1);
            default:
              return std::nullopt;
          }
        }

      private:
        std::optional<bdd> variable (const std::string& ap, unsigned offset) {
          if (not input_aps_.contains (ap))
            return std::nullopt;

          const auto key = std::pair {ap, offset};
          auto existing = variables_.find (key);
          if (existing != variables_.end ())
            return bdd_ithvar (existing->second);

          const int variable = bdd_extvarnum (1);
          variables_.emplace (key, variable);
          return bdd_ithvar (variable);
        }

        std::unordered_set<std::string> input_aps_;
        std::map<std::pair<std::string, unsigned>, int> variables_;
    };
  }  // namespace detail

  inline std::optional<bool> is_satisfiable (
      const spot::formula& formula,
      const std::vector<std::string>& input_aps) {
    // Guarantee BuDDy initialization because this checker runs before the
    // solver creates its own dictionary.
    const spot::bdd_dict_ptr bdd_lifetime = spot::make_bdd_dict ();
    auto translated = detail::translator {input_aps}.translate (formula);
    if (not translated)
      return std::nullopt;
    return *translated != bddfalse;
  }
}  // namespace acacia::bounded_input_pattern
