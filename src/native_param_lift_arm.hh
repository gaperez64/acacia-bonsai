#pragma once
#include "configuration.hh"
#include <cstdint>
struct arg_parse_result;
namespace acacia {
  int run_native_param_lift_arm (const arg_parse_result& args, uint64_t deadline_mono_ns);
}
