#pragma once

#include <string_view>

#include <optional>
#include <spot/misc/optionmap.hh>
#include <spot/twaalgos/translate.hh>

namespace acacia::translation {
  using level = spot::postprocessor::optimization_level;
  inline constexpr level default_level = spot::postprocessor::High;
  inline constexpr const char* scalar_settings =
      "simul=0,ba-simul=0,det-simul=0,tls-impl=1,wdba-minimize=2";

  inline const char* level_name (level value) {
    switch (value) {
      case spot::postprocessor::High: return "high";
      case spot::postprocessor::Medium: return "medium";
      case spot::postprocessor::Low: return "low";
    }
    return "unknown";
  }

  inline std::optional<level> parse_level (std::string_view value) {
    if (value == "high")
      return spot::postprocessor::High;
    if (value == "medium")
      return spot::postprocessor::Medium;
    if (value == "low")
      return spot::postprocessor::Low;
    return std::nullopt;
  }

  // Spot 2.16 translate.hh: set_level changes postprocessing, rebuilds the owned
  // LTL simplifier with the same dictionary, and defaults gf-guarantee to off at
  // Low (on otherwise). With tls-impl=1, syntactic implication stays enabled and
  // containment checks disabled at every level. High also defaults to explicit
  // FM propositions, dead-end restriction and a final SCC filter; Low/Medium
  // skip those and reject larger WDBA results under Small. Simulations remain
  // disabled by our scalar options. Keep constructor High untouched, including
  // its simplifier caches. Configure each fresh translator once; alternate
  // levels are explicit, global opt-ins.
  inline void configure_level (spot::translator& translator, level value) {
    if (value != default_level)
      translator.set_level (value);
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
