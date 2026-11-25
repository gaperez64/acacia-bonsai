#pragma once

namespace acacia::parity_route {
  enum class mealy_reduction { none, bisim, bisim_output, reselect, sat, bisim_sat };
  inline constexpr auto default_mealy_reduction = mealy_reduction::reselect;
}
