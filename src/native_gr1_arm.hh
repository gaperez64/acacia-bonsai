#pragma once
#include "configuration.hh"
#include <cstdint>
struct arg_parse_result;
namespace acacia {
  int run_native_gr1_arm (const arg_parse_result& args, bool unreal, bool both,
                          uint64_t deadline_mono_ns);
}
