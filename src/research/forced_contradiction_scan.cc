#include "solver/forced_output_contradiction.hh"
#include "solver/realizability_simplify.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <exception>
#include <iostream>
#include <spot/tl/parse.hh>
#include <string>

int main (int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: acacia-forced-contradiction-scan FILE.tlsf\n";
    return 2;
  }

  try {
    const auto specification = acacia::tlsf_frontend::load (argv[1]);
    auto parsed = spot::parse_infix_psl (
        specification.formula, spot::default_environment::instance (), false,
        false);
    if (not parsed.f or not parsed.errors.empty ()) {
      std::cerr << "unable to parse converted TLSF formula\n";
      parsed.format_errors (std::cerr);
      return 2;
    }

    acacia::realizability::apply_simplifier (parsed.f,
                                             specification.inputs);
    const auto result = acacia::forced_output_contradiction::try_direct (
        parsed.f, specification.inputs, specification.outputs);
    if (result.unrealizable and result.proof) {
      if (result.proof->kind
          == acacia::forced_output_contradiction::response_kind::fixed_delay)
        std::cout << "MATCH fixed_delay " << result.proof->delay << '\n';
      else if (result.proof->kind
               == acacia::forced_output_contradiction::response_kind::eventual)
        std::cout << "MATCH eventual 0\n";
      else
        std::cout << "MATCH contradictory_invariants 0\n";
    }
    else {
      std::string reason = result.decline_reason;
      std::replace (reason.begin (), reason.end (), ' ', '-');
      std::cout << "DECLINE " << reason << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 2;
  }
}
