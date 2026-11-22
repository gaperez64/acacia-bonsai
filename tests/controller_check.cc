#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tlsf/convert.hpp>
#include <vector>

namespace {
  bool same_signals (const std::vector<std::string>& actual,
                     const std::vector<std::string>& expected) {
    return actual.size () == expected.size () &&
           std::set<std::string> (actual.begin (), actual.end ()) ==
               std::set<std::string> (expected.begin (), expected.end ());
  }
}

int main (int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: controller-check SPEC.tlsf CONTROLLER.aag\n";
    return 2;
  }
  try {
    std::ifstream source (argv[1]);
    if (!source)
      throw std::runtime_error ("cannot open specification");
    std::ostringstream text;
    text << source.rdbuf ();
    // Retain TARGET: the emitted circuit has already undone Mealy lowering.
    const auto spec = tlsf::convert (text.str (), nullptr);
    auto parsed = spot::parse_infix_psl (spec.preprocessed_ltl);
    if (!parsed.f || !parsed.errors.empty ())
      throw std::runtime_error ("cannot parse specification objective");

    std::ifstream circuit_file (argv[2]);
    std::string header, kind, extra;
    unsigned m, i, l, o, a;
    std::getline (circuit_file, header);
    std::istringstream fields (header);
    if (!(fields >> kind >> m >> i >> l >> o >> a) || kind != "aag" || (fields >> extra))
      throw std::runtime_error ("expected basic ASCII AIGER header");
    if (uint64_t (m) != uint64_t (i) + l + a || i != spec.inputs.size () ||
        o != spec.outputs.size ())
      throw std::runtime_error ("invalid circuit dimensions");
    circuit_file.clear ();
    circuit_file.seekg (0);
    const auto circuit = spot::aig::parse_aag (circuit_file, argv[2]);
    if (!same_signals (circuit->input_names (), spec.inputs) ||
        !same_signals (circuit->output_names (), spec.outputs))
      throw std::runtime_error ("circuit input/output partition differs from specification");

    if (spec.effective_target == "Moore") {
      bdd inputs = bddtrue;
      for (unsigned n = 0; n < circuit->num_inputs (); ++n)
        inputs &= circuit->input_bdd (n);
      for (auto output : circuit->outputs ()) {
        const auto function = circuit->aigvar2bdd (output);
        if (bdd_exist (function, inputs) != function) {
          std::cerr << "Moore output depends on current input\n";
          std::cout << "failed_verification\n";
          return 1;
        }
      }
    }
    const auto behavior = circuit->as_automaton (false);
    spot::translator translator (behavior->get_dict ());
    translator.set_type (spot::postprocessor::BA);
    const auto violation = translator.run (spot::formula::Not (parsed.f));
    if (violation->intersects (behavior)) {
      std::cout << "failed_verification\n";
      return 1;
    }
    std::cout << "verified\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 2;
  }
}
