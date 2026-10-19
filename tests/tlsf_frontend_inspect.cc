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
  try {
    bool apply_rsimp = false;
    auto policy = acacia::tlsf_frontend::normalization_policy::off;
    int at = 1;
    while (at < argc - 1) {
      const std::string_view option {argv[at++]};
      if (option == "--rsimp")
        apply_rsimp = true;
      else if (option == "--normalization" && at < argc - 1)
        policy = acacia::tlsf_frontend::parse_normalization_policy (argv[at++]);
      else
        throw std::invalid_argument ("unknown or incomplete option");
    }
    if (at != argc - 1) {
      std::cerr << "usage: tlsf-frontend-inspect [--rsimp] "
                   "[--normalization off|match-safe|route-safe] FILE.tlsf\n";
      return 2;
    }
    const auto parsed = acacia::tlsf_frontend::load (argv[at], policy);
    if (policy != acacia::tlsf_frontend::normalization_policy::off) {
      const auto& metadata = parsed.metadata;
      std::cerr << "normalization\t" << metadata.tlsf_normalization_schedule << '\t'
                << metadata.tlsf_normalization_changed << '\t'
                << metadata.tlsf_normalization_bounds_rejected << '\t'
                << metadata.tlsf_normalization_nodes_before << '\t'
                << metadata.tlsf_normalization_nodes_after << '\t'
                << metadata.tlsf_normalization_time_ns << '\n';
    }
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
