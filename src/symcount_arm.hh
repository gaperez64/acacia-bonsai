#pragma once
#include <cstdint>
#include <string_view>
namespace acacia {
  namespace tlsf_frontend {
    struct specification;
  }
  // both:symcount:spot (#454): the count-quotiented game of symmetric specs.
  int run_symcount_arm (const tlsf_frontend::specification& objective, std::string_view tlsf_source,
                        uint64_t deadline_mono_ns);
}
