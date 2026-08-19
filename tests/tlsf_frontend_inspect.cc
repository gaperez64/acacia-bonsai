#include "tlsf_frontend.hh"

#include <iostream>
#include <span>
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
  if (argc != 2) {
    std::cerr << "usage: tlsf-frontend-inspect FILE.tlsf\n";
    return 2;
  }
  try {
    const auto parsed = acacia::tlsf_frontend::load (argv[1]);
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
