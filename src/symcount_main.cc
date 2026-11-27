// acacia-symcount: run the count-quotiented game of #454 on one TLSF file.
// Exit 0 for REALIZABLE, 1 for UNREALIZABLE, 2 for DECLINED.

#include "error_msg.hh"
#include "solver/bounded_subprocess.hh"
#include "solver/encoded_family.hh"
#include "solver/symmetric_count.hh"
#include "tlsf_frontend.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <spot/tl/parse.hh>
#include <sys/mman.h>

namespace {
  using namespace acacia;
  using symcount::verdict;

  int report (const symcount::result& r, char* line, size_t size) {
    const char* decision = r.outcome == verdict::realizable     ? "REALIZABLE"
                           : r.outcome == verdict::unrealizable ? "UNREALIZABLE"
                                                                : "DECLINED";
    std::snprintf (
        line, size,
        "%s n=%u local_states=%u events=%u global_states=%u count_states=%zu "
        "vertices=%zu edges=%zu build_s=%.3f solve_s=%.3f check_s=%.3f%s%s%s\n",
        decision, r.n, r.local_states, r.events, r.global_states, r.count_states, r.vertices,
        r.edges, r.build_seconds, r.solve_seconds, r.check_seconds,
        r.encoded ? " check=quotient" : "",
        r.reason.empty () ? "" : " reason=", r.reason.c_str ());
    return r.outcome == verdict::realizable     ? EXIT_CODE_REAL
           : r.outcome == verdict::unrealizable ? EXIT_CODE_UNREAL : EXIT_CODE_UNKNOWN;
  }

  std::optional<int> try_encoded (tlsf_frontend::specification& spec, symcount::limits bound,
                                  symcount::result& physical) {
    constexpr size_t size = 1024;
    auto* line = static_cast<char*> (
        mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (line == MAP_FAILED)
      return {};
    using namespace std::chrono_literals;
    bound.deadline_mono_ns = phase_clock (CLOCK_MONOTONIC) + (120s / 1ns);
    const auto worker =
        subprocess::run<true> (bound.deadline_mono_ns, subprocess::max_optional_memory, [&] {
          if (not symmetry::certify_encoded_family (spec, 1u << 22,
                                                    std::chrono::steady_clock::now () + 15s))
            return int (EXIT_CODE_UNKNOWN);
          const auto& cert = *spec.metadata.tlsf_declared_families.front ().logical->certificate;
          const auto r = symcount::solve_encoded (spec.formula, spec.inputs, spec.outputs,
                                                   cert, bound);
          if (r.outcome == verdict::declined)
            return r.reason == "deadline" ? subprocess::deadline_exit : int (EXIT_CODE_UNKNOWN);
          return report (r, line, size);
        });
    const bool decided = std::strcmp (worker.cause, "none") == 0
                         and (worker.code == EXIT_CODE_REAL or worker.code == EXIT_CODE_UNREAL);
    if (decided)
      std::fputs (line, stdout);
    munmap (line, size);
    if (decided)
      return worker.code;
    if (std::strcmp (worker.cause, "deadline") == 0)
      physical.reason = "deadline";
    return {};
  }
}

int main (int argc, char** argv) {
  using namespace acacia;
  if (argc < 2 or not std::strcmp (argv[1], "--help")) {
    std::fprintf (stderr, "usage: %s SPEC.tlsf [--max-vertices N]\n", argv[0]);
    return argc < 2 ? EXIT_CODE_ERROR : 0;
  }
  symcount::limits bound;
  for (int i = 2; i + 1 < argc; ++i)
    if (not std::strcmp (argv[i], "--max-vertices"))
      bound.max_vertices = std::strtoull (argv[++i], nullptr, 10);
  try {
    auto spec = tlsf_frontend::load_with_value_origins (argv[1]);
    // Check before Spot parses a conjunction too large for its uint16_t arity.
    for (const auto& f : spec.metadata.tlsf_indexed_families ())
      if (f.members.size () > UINT16_MAX) {
        std::puts ("DECLINED reason=index count exceeds uint16_t");
        return EXIT_CODE_UNKNOWN;
      }
    auto parsed = spot::parse_infix_psl (spec.formula);
    if (parsed.format_errors (std::cerr))
      return EXIT_CODE_ERROR;
    auto r = symcount::solve (parsed.f, spec.inputs, spec.outputs,
                              spec.metadata.tlsf_indexed_families (), bound);
    if (r.outcome == verdict::declined and spec.value_origins)
      if (auto code = try_encoded (spec, bound, r))
        return *code;
    char line[1024];
    const auto code = report (r, line, sizeof line);
    std::fputs (line, stdout);
    return code;
  } catch (const std::exception& e) {
    std::fprintf (stderr, "acacia-symcount: %s\n", e.what ());
    return EXIT_CODE_ERROR;
  }
}
