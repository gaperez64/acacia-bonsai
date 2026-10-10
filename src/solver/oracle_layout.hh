#pragma once

#include <string_view>

#include <optional>

namespace acacia {
  enum class OracleLayout { scan, grouped };
  // Runtime sparse guard storage selection.
  inline OracleLayout selected_oracle_layout = OracleLayout::grouped;
  inline const char* oracle_layout_name (OracleLayout layout) {
    return layout == OracleLayout::grouped ? "grouped" : "scan";
  }
  inline std::optional<OracleLayout> parse_oracle_layout (std::string_view value) {
    if (value == "scan")
      return OracleLayout::scan;
    if (value == "grouped")
      return OracleLayout::grouped;
    return std::nullopt;
  }
}  // namespace acacia
