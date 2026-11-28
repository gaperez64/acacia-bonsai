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
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
        r.lifted ? " check=lifted" : "",
        r.reason.empty () ? "" : " reason=", r.reason.c_str ());
    return r.outcome == verdict::realizable     ? EXIT_CODE_REAL
           : r.outcome == verdict::unrealizable ? EXIT_CODE_UNREAL : EXIT_CODE_UNKNOWN;
  }

  std::optional<int> try_encoded (const tlsf_frontend::specification& spec, std::string_view source,
                                  const symcount::limits& bound, symcount::result& physical,
                                  bool guarded, bool& retry_encoded) {
    constexpr size_t size = 1024;
    constexpr int retry_exit = 8;
    auto* line = static_cast<char*> (
        mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (line == MAP_FAILED)
      return {};
    using namespace std::chrono_literals;
    const auto worker =
        subprocess::run<true> (bound.deadline_mono_ns, subprocess::max_optional_memory, [&] {
          const auto captured = tlsf_frontend::parse_with_value_origins (source);
          const auto cert = symmetry::check_encoded_family (
              captured, 1u << 22, std::chrono::steady_clock::now () + 15s, guarded);
          if (not cert)
            return int (EXIT_CODE_UNKNOWN);
          const auto r = symcount::solve_encoded (spec.formula, spec.inputs, spec.outputs,
                                                  *cert, bound, nullptr, guarded);
          if (r.retry)
            return retry_exit;
          if (not r.lifted or r.outcome == verdict::declined)
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
    retry_encoded = std::strcmp (worker.cause, "none") == 0 and worker.code == retry_exit;
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
    std::ifstream input (argv[1], std::ios::binary);
    if (not input)
      throw std::runtime_error (std::string ("unable to open TLSF file: ") + argv[1]);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto source = std::move (buffer).str ();
    const auto spec = [&] {
      try {
        return tlsf_frontend::parse (source);
      } catch (const std::exception& e) {
        throw std::runtime_error (std::string ("unable to convert TLSF file ") + argv[1]
                                  + ": " + e.what ());
      }
    } ();
    // Check before Spot parses a conjunction too large for its uint16_t arity.
    for (const auto& f : spec.metadata.tlsf_indexed_families ())
      if (f.members.size () > UINT16_MAX) {
        std::puts ("DECLINED reason=index count exceeds uint16_t");
        return EXIT_CODE_UNKNOWN;
      }
    auto parsed = spot::parse_infix_psl (spec.formula);
    if (parsed.format_errors (std::cerr))
      return EXIT_CODE_ERROR;
    using namespace std::chrono_literals;
    bound.deadline_mono_ns = phase_clock (CLOCK_MONOTONIC) + (120s / 1ns);
    symcount::result r;
    std::vector<unsigned> retry_sizes;
    bool retry_encoded = false;
    for (bool guarded : {false, true}) {
      if (not guarded or not retry_sizes.empty ()) {
        auto physical = symcount::solve (parsed.f, spec.inputs, spec.outputs,
                                         spec.metadata.tlsf_indexed_families (), bound,
                                         guarded, &retry_sizes);
        if (not guarded or physical.outcome != verdict::declined or physical.n == r.n)
          r = std::move (physical);
      }
      if (r.outcome != verdict::declined)
        break;
      if (phase_clock (CLOCK_MONOTONIC) >= bound.deadline_mono_ns) {
        r.reason = "deadline";
        break;
      }
      if (not spec.metadata.tlsf_declared_families.empty () and (not guarded or retry_encoded))
        if (auto code = try_encoded (spec, source, bound, r, guarded, retry_encoded))
          return *code;
    }
    char line[1024];
    const auto code = report (r, line, sizeof line);
    std::fputs (line, stdout);
    return code;
  } catch (const std::exception& e) {
    std::fprintf (stderr, "acacia-symcount: %s\n", e.what ());
    return EXIT_CODE_ERROR;
  }
}
