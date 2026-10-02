#include "portfolio_arm.hh"
#include <ranges>

std::string trim_portfolio_arm_value (std::string_view value) {
  const auto first = std::find_if_not (
      value.begin (), value.end (), [] (unsigned char c) { return std::isspace (c); });
  const auto last = std::find_if_not (
                        value.rbegin (), value.rend (),
                        [] (unsigned char c) { return std::isspace (c); })
                        .base ();
  return first < last ? std::string (first, last) : std::string {};
}

portfolio_arm_parse_result parse_portfolio_arms (std::string_view arg) {
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
    const size_t third_colon =
        second_colon == std::string::npos ? second_colon : spec.find (':', second_colon + 1);
    if (first_colon == std::string::npos or second_colon == std::string::npos or
        (third_colon != std::string::npos && spec.find (':', third_colon + 1) != std::string::npos)) {
      result.error = portfolio_arm_parse_error::malformed_spec;
      result.spec = spec;
      return result;
    }

    const std::string polarity = spec.substr (0, first_colon);
    const std::string transform =
        spec.substr (first_colon + 1, second_colon - first_colon - 1);
    const std::string backend_name = spec.substr (second_colon + 1, third_colon == std::string::npos
        ? std::string::npos : third_colon - second_colon - 1);
    const bool native_transform = transform == "gr1" || transform == "gr1-lift" ||
                                  transform == "gr1-real-lift" || transform == "param-lift";
    portfolio_arm arm = native_transform
        ? portfolio_arm::native (polarity == "unreal", transform == "gr1"
            ? portfolio_arm_kind::gr1 : transform == "gr1-lift"
                ? portfolio_arm_kind::gr1_lift : transform == "gr1-real-lift"
                    ? portfolio_arm_kind::gr1_real_lift : portfolio_arm_kind::param_lift,
            polarity == "both")
        : portfolio_arm {false, ACACIA_TRANSLATION_PREF, UNREAL_X_FORMULA,
                         acacia::game_backend::backward};

    if (native_transform) {
      if (polarity != "real" && polarity != "unreal" && polarity != "both") {
        result.error = portfolio_arm_parse_error::polarity;
        result.spec = spec;
        result.value = polarity;
        return result;
      }
      if (transform == "param-lift" && polarity != "real") {
        result.error = polarity == "both" ? portfolio_arm_parse_error::both_transform
                                          : portfolio_arm_parse_error::native_transform;
        result.spec = spec;
        return result;
      }
      if ((transform == "gr1-lift" || transform == "gr1-real-lift") && polarity != "both") {
        result.error = portfolio_arm_parse_error::gr1_lift_polarity;
        result.spec = spec;
        return result;
      }
      if (backend_name != "oxidd") {
        result.error = portfolio_arm_parse_error::native_backend;
        result.spec = spec;
        result.value = backend_name;
        return result;
      }
      if (third_colon != std::string::npos) {
        result.error = portfolio_arm_parse_error::native_provider;
        result.spec = spec;
        return result;
      }
    }
    else {
      if (polarity == "both") {
        result.error = portfolio_arm_parse_error::both_transform;
        result.spec = spec;
        return result;
      }
      if (polarity == "real") {
        if (transform == "small")
          arm.legacy->translation_pref = spot::postprocessor::Small;
        else if (transform == "any")
          arm.legacy->translation_pref = spot::postprocessor::Any;
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
          arm.legacy->unreal_x = UNREAL_X_FORMULA;
        else if (transform == "automaton")
          arm.legacy->unreal_x = UNREAL_X_AUTOMATON;
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
      arm.legacy->backend = *backend;
      if (third_colon != std::string::npos) {
        const auto name = spec.substr (third_colon + 1);
        const auto provider = acacia::parse_automaton_provider (name);
        if (!provider) {
          result.error = portfolio_arm_parse_error::provider;
          result.spec = spec;
          result.value = name;
          return result;
        }
        arm.legacy->provider = *provider;
        arm.legacy->provider_explicit = true;
      }
    }

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
