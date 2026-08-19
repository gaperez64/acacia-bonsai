#pragma once

#include <spot/tl/formula.hh>
#include <spot/twaalgos/synthesis.hh>

#include <string>
#include <vector>

namespace acacia::syntactic_bypass {
  enum class verdict { unknown, realizable, unrealizable };

  struct result {
      verdict value = verdict::unknown;
  };

  inline result try_direct (spot::formula formula,
                            const std::vector<std::string>& output_aps) {
    spot::synthesis_info info;
    auto direct = spot::try_create_direct_strategy (formula, output_aps, info, false);
    using code = spot::mealy_like::realizability_code;
    switch (direct.success) {
      case code::UNREALIZABLE:
        return {verdict::unrealizable};
      case code::REALIZABLE_REGULAR:
      case code::REALIZABLE_DTGBA:
        return {verdict::realizable};
      case code::UNKNOWN:
        return {verdict::unknown};
    }
    return {verdict::unknown};
  }

  inline const char* name (verdict value) {
    switch (value) {
      case verdict::realizable:
        return "realizable";
      case verdict::unrealizable:
        return "unrealizable";
      case verdict::unknown:
        return "fallback";
    }
    return "fallback";
  }

  inline bool matches_worker (verdict value, bool checking_unrealizability) {
    return (value == verdict::realizable) != checking_unrealizability;
  }
}  // namespace acacia::syntactic_bypass
