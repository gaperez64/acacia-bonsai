#pragma once

#include "configuration.hh"
#include "solver/game_backend.hh"
#include "solver/solver_invoker.hh"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

struct portfolio_arm {
    bool unreal;  // false selects a realizability arm
    TRANSLATION_PREF_T translation_pref;
    UNREAL_X_T unreal_x;  // meaningful only for unrealizability arms
    acacia::game_backend backend;

    bool operator== (const portfolio_arm&) const = default;
};

enum class portfolio_arm_parse_error {
  none,
  empty_list,
  empty_spec,
  malformed_spec,
  polarity,
  real_transform,
  unreal_transform,
  backend,
  duplicate,
};

struct portfolio_arm_parse_result {
    std::vector<portfolio_arm> arms;
    portfolio_arm_parse_error error = portfolio_arm_parse_error::none;
    std::string spec;
    std::string value;
};

inline std::string trim_portfolio_arm_value (std::string_view value) {
  const auto first = std::find_if_not (
      value.begin (), value.end (), [] (unsigned char c) { return std::isspace (c); });
  const auto last = std::find_if_not (
                        value.rbegin (), value.rend (),
                        [] (unsigned char c) { return std::isspace (c); })
                        .base ();
  return first < last ? std::string (first, last) : std::string {};
}

inline portfolio_arm_parse_result parse_portfolio_arms (std::string_view arg) {
  portfolio_arm_parse_result result;
  if (trim_portfolio_arm_value (arg).empty ()) {
    result.error = portfolio_arm_parse_error::empty_list;
    return result;
  }

  size_t start = 0;
  while (start <= arg.size ()) {
    const size_t comma = arg.find (',', start);
    const std::string spec = trim_portfolio_arm_value (
        arg.substr (start, comma == std::string_view::npos ? comma : comma - start));
    if (spec.empty ()) {
      result.error = portfolio_arm_parse_error::empty_spec;
      return result;
    }

    const size_t first_colon = spec.find (':');
    const size_t second_colon =
        first_colon == std::string::npos ? first_colon : spec.find (':', first_colon + 1);
    if (first_colon == std::string::npos or second_colon == std::string::npos or
        spec.find (':', second_colon + 1) != std::string::npos) {
      result.error = portfolio_arm_parse_error::malformed_spec;
      result.spec = spec;
      return result;
    }

    const std::string polarity = spec.substr (0, first_colon);
    const std::string transform =
        spec.substr (first_colon + 1, second_colon - first_colon - 1);
    const std::string backend_name = spec.substr (second_colon + 1);
    portfolio_arm arm {
        .unreal = false,
        .translation_pref = ACACIA_TRANSLATION_PREF,
        .unreal_x = UNREAL_X_FORMULA,
        .backend = acacia::game_backend::backward,
    };

    if (polarity == "real") {
      if (transform == "small")
        arm.translation_pref = spot::postprocessor::Small;
      else if (transform == "any")
        arm.translation_pref = spot::postprocessor::Any;
      else {
        result.error = portfolio_arm_parse_error::real_transform;
        result.spec = spec;
        result.value = transform;
        return result;
      }
    }
    else if (polarity == "unreal") {
      arm.unreal = true;
      if (transform == "formula")
        arm.unreal_x = UNREAL_X_FORMULA;
      else if (transform == "automaton")
        arm.unreal_x = UNREAL_X_AUTOMATON;
      else {
        result.error = portfolio_arm_parse_error::unreal_transform;
        result.spec = spec;
        result.value = transform;
        return result;
      }
    }
    else {
      result.error = portfolio_arm_parse_error::polarity;
      result.spec = spec;
      result.value = polarity;
      return result;
    }

    const auto backend = acacia::parse_game_backend (backend_name);
    if (not backend.has_value ()) {
      result.error = portfolio_arm_parse_error::backend;
      result.spec = spec;
      result.value = backend_name;
      return result;
    }
    arm.backend = *backend;

    if (std::ranges::find (result.arms, arm) != result.arms.end ()) {
      result.error = portfolio_arm_parse_error::duplicate;
      result.spec = spec;
      return result;
    }
    result.arms.push_back (arm);

    if (comma == std::string_view::npos)
      break;
    start = comma + 1;
  }
  return result;
}
