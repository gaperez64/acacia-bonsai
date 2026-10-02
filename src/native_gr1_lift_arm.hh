#pragma once

#include "arg_parser.hh"

#include <cstdint>

namespace acacia {
  int run_native_gr1_lift_arm (const arg_parse_result& args, uint64_t deadline_mono_ns,
                               bool real_only = false);
}
