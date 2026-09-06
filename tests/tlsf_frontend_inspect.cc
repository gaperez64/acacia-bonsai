#include "tlsf_frontend.hh"
#include "solver/realizability_simplify.hh"

#include <iostream>
#include <span>
#include <spot/tl/parse.hh>
#include <string>

namespace {

  void print_list (std::span<const std::string> values) {
    for (size_t i = 0; i < values.size (); ++i) {
      if (i != 0)
        std::cout << ',';
      std::cout << values[i];
    }
    std::cout.put ('\0');
  }

}  // namespace

int main (int argc, char** argv) {
  const bool apply_rsimp = argc == 3 and std::string (argv[1]) == "--rsimp";
  if (argc != 2 and not apply_rsimp) {
    std::cerr << "usage: tlsf-frontend-inspect [--rsimp] FILE.tlsf\n";
    return 2;
  }
  try {
    const auto parsed = acacia::tlsf_frontend::load (argv[apply_rsimp ? 2 : 1]);
    if (apply_rsimp) {
      auto formula = spot::parse_infix_psl (
          parsed.formula, spot::default_environment::instance (), false, false);
      if ((not formula.f) or (not formula.errors.empty ())) {
        formula.format_errors (std::cerr);
        return 1;
      }
      acacia::realizability::apply_simplifier (formula.f, parsed.inputs);
      std::cout << formula.f;
    }
    else
      std::cout << parsed.formula;
    std::cout.put ('\0');
    print_list (parsed.inputs);
    print_list (parsed.outputs);
    std::cout << parsed.metadata.tlsf_semantics;
    std::cout.put ('\0');
    std::cout << parsed.metadata.tlsf_target;
    std::cout.put ('\0');
    std::cout << parsed.metadata.tlsf_effective_target;
    std::cout.put ('\0');
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
