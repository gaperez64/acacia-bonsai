#include "tlsf_frontend.hh"
#include "solver/encoded_family.hh"
#include "solver/realizability_simplify.hh"

#include <fstream>
#include <iostream>
#include <span>
#include <spot/tl/parse.hh>
#include <sstream>
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

  void inspect_encoded (const char* path) {
    std::ifstream file (path);
    if (not file)
      throw std::runtime_error (std::string {"unable to open "} + path);
    std::ostringstream text;
    text << file.rdbuf ();
    auto spec = acacia::tlsf_frontend::parse_with_value_origins (text.str ());
    if (not symmetry::certify_encoded_family (spec)) {
      std::cout << "declined\n";
      return;
    }
    const auto& logical = *spec.objective.metadata.tlsf_declared_families.front ().logical;
    const auto& cert = *logical.certificate;
    std::cout << "certified " << cert.moving;
    for (auto c : cert.coordinates)
      std::cout << ' ' << c;
    std::cout << '\n';
    for (const auto& bus : cert.buses) {
      std::cout << "bus " << bus.is_input << ' ' << bus.members.size ();
      for (size_t i = 0; i < bus.codes.size (); ++i) {
        std::cout << ' ' << bus.codes[i];
        if (bus.aliases[i])
          std::cout << '/' << *bus.aliases[i];
      }
      std::cout << '\n';
      for (const auto& group : bus.unused_classes) {
        std::cout << "unused";
        for (auto code : group)
          std::cout << ' ' << code;
        std::cout << '\n';
      }
    }
    for (const auto& p : cert.predicates) {
      std::cout << "table";
      for (auto [bus, next] : p.slots)
        std::cout << ' ' << bus << '/' << next;
      std::cout << ' ';
      for (bool value : p.truth)
        std::cout << value;
      std::cout << '\n';
    }
    for (const auto& swaps : cert.generators) {
      std::cout << "swap";
      for (auto [bus, first, second] : swaps)
        std::cout << ' ' << bus << '/' << first << '/' << second;
      std::cout << '\n';
    }
  }

}  // namespace

int main (int argc, char** argv) {
  const bool apply_rsimp = argc == 3 and std::string (argv[1]) == "--rsimp";
  const bool encoded = argc == 3 and std::string (argv[1]) == "--encoded";
  if (argc != 2 and not apply_rsimp and not encoded) {
    std::cerr << "usage: tlsf-frontend-inspect [--rsimp|--encoded] FILE.tlsf\n";
    return 2;
  }
  try {
    if (encoded) {
      inspect_encoded (argv[2]);
      return 0;
    }
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
