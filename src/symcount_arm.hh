#pragma once
#include <cstdint>
struct arg_parse_result;
namespace acacia {
  // both:symcount:spot (#454): the count-quotiented game of symmetric specs.
  int run_symcount_arm (const arg_parse_result& args, uint64_t deadline_mono_ns);
}
