// acacia-symcount: run the count-quotiented game of #454 on one TLSF file and
// print its verdict and sizes. Exits like acacia-bonsai: 0 REALIZABLE,
// 1 UNREALIZABLE, 2 when the spec is outside the fragment (declined).

#include "error_msg.hh"
#include "solver/symmetric_count.hh"
#include "tlsf_frontend.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <spot/tl/parse.hh>

int main (int argc, char** argv) {
  if (argc < 2 or not std::strcmp (argv[1], "--help")) {
    std::fprintf (stderr, "usage: %s SPEC.tlsf [--max-vertices N]\n", argv[0]);
    return argc < 2 ? EXIT_CODE_ERROR : 0;
  }
  acacia::symcount::limits bound;
  for (int i = 2; i + 1 < argc; ++i)
    if (not std::strcmp (argv[i], "--max-vertices"))
      bound.max_vertices = std::strtoull (argv[++i], nullptr, 10);
  try {
    const auto spec = acacia::tlsf_frontend::load (argv[1]);
    auto parsed = spot::parse_infix_psl (spec.formula);
    if (parsed.format_errors (std::cerr))
      return EXIT_CODE_ERROR;
    const auto r = acacia::symcount::solve (parsed.f, spec.inputs, spec.outputs,
                                            spec.metadata.tlsf_indexed_families, bound);
    const char* verdict = r.outcome == acacia::symcount::verdict::realizable     ? "REALIZABLE"
                          : r.outcome == acacia::symcount::verdict::unrealizable ? "UNREALIZABLE"
                                                                                 : "DECLINED";
    std::printf ("%s n=%u local_states=%u events=%u global_states=%u count_states=%zu "
                 "vertices=%zu edges=%zu build_s=%.3f solve_s=%.3f check_s=%.3f%s%s\n",
                 verdict, r.n, r.local_states, r.events, r.global_states, r.count_states,
                 r.vertices, r.edges, r.build_seconds, r.solve_seconds, r.check_seconds,
                 r.reason.empty () ? "" : " reason=", r.reason.c_str ());
    return r.outcome == acacia::symcount::verdict::realizable     ? EXIT_CODE_REAL
           : r.outcome == acacia::symcount::verdict::unrealizable ? EXIT_CODE_UNREAL
                                                                  : EXIT_CODE_UNKNOWN;
  }
  catch (const std::exception& e) {
    std::fprintf (stderr, "acacia-symcount: %s\n", e.what ());
    return EXIT_CODE_ERROR;
  }
}
