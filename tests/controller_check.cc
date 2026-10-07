#include <algorithm>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/product.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <string>
#include <vector>

namespace {
  std::vector<std::string> names (const std::string& text) {
    std::vector<std::string> result;
    std::istringstream stream {text};
    for (std::string name; std::getline (stream, name, ',');)
      result.push_back (name);
    std::ranges::sort (result);
    return result;
  }
}

int main (int argc, char** argv) {
  if (argc != 6 or (std::string {argv[2]} != "Mealy" and std::string {argv[2]} != "Moore")) {
    std::cerr << "usage: controller-check AAG Mealy|Moore FORMULA INPUTS OUTPUTS\n";
    return 2;
  }
  try {
    const auto dict = spot::make_bdd_dict ();
    const auto circuit = spot::aig::parse_aag (std::string {argv[1]}, dict);
    auto inputs = circuit->input_names ();
    auto outputs = circuit->output_names ();
    std::ranges::sort (inputs);
    std::ranges::sort (outputs);
    if (inputs != names (argv[4]) or outputs != names (argv[5])) {
      std::cerr << "controller signal inventory differs from specification\n";
      return 2;
    }
    if (std::string {argv[2]} == "Moore")
      for (unsigned output : circuit->outputs ()) {
        const bdd function = circuit->aigvar2bdd (output);
        for (unsigned i = 0; i < circuit->num_inputs (); ++i)
          if (bdd_exist (function, circuit->input_bdd (i)) != function) {
            std::cerr << "Moore output depends on current input\n";
            return 2;
          }
      }
    auto parsed = spot::parse_infix_psl (argv[3]);
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      return 2;
    }
    spot::translator translator {dict};
    const auto negated = translator.run (spot::formula::Not (parsed.f));
    const auto product = spot::product (circuit->as_automaton (), negated);
    if (not product->is_empty ()) {
      std::cout << "controller violates original target formula\n";
      return 1;
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 2;
  }
}
