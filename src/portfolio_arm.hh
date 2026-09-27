#pragma once

#include "configuration.hh"
#include "solver/game_backend.hh"
#include "solver/solver_invoker.hh"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class portfolio_arm_kind { legacy, gr1, param_lift };

struct legacy_arm_options {
    TRANSLATION_PREF_T translation_pref;
    UNREAL_X_T unreal_x;
    acacia::game_backend backend;
    acacia::automaton_provider provider = acacia::automaton_provider::frozen_graph;
    bool provider_explicit = false;

    bool operator== (const legacy_arm_options& rhs) const {
      return translation_pref == rhs.translation_pref && unreal_x == rhs.unreal_x &&
             backend == rhs.backend && provider == rhs.provider;
    }
};

struct portfolio_arm {
    bool unreal;  // false selects a realizability arm
    portfolio_arm_kind kind;
    std::optional<legacy_arm_options> legacy;

    portfolio_arm (bool is_unreal, TRANSLATION_PREF_T translation_pref, UNREAL_X_T unreal_x,
                   acacia::game_backend backend,
                   acacia::automaton_provider provider = acacia::automaton_provider::frozen_graph,
                   bool provider_explicit = false)
        : unreal (is_unreal), kind (portfolio_arm_kind::legacy),
          legacy (legacy_arm_options {translation_pref, unreal_x, backend,
                                      provider, provider_explicit}) {}

    static portfolio_arm native (bool is_unreal, portfolio_arm_kind native_kind) {
      return portfolio_arm (is_unreal, native_kind);
    }

    bool operator== (const portfolio_arm& rhs) const {
      return kind == rhs.kind && unreal == rhs.unreal && legacy == rhs.legacy;
    }

  private:
    portfolio_arm (bool is_unreal, portfolio_arm_kind native_kind)
        : unreal (is_unreal), kind (native_kind), legacy (std::nullopt) {}
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
  provider,
  native_provider,
  native_transform,
  native_backend,
  duplicate,
};

struct portfolio_arm_parse_result {
    std::vector<portfolio_arm> arms;
    portfolio_arm_parse_error error = portfolio_arm_parse_error::none;
    std::string spec;
    std::string value;
};

std::string trim_portfolio_arm_value (std::string_view value);
portfolio_arm_parse_result parse_portfolio_arms (std::string_view arg);
