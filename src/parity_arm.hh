#pragma once

#include "arg_parser.hh"

namespace acacia {
  struct worker_record;
  void parity_arm_exhausted (worker_record& record);
  int run_parity_arm (const arg_parse_result& args, bool oxidd = false,
                      uint64_t deadline_mono_ns = 0);
}
