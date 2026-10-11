#include "solver/translator_options.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <spot/tl/apcollect.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/synthesis.hh>
#include <stdexcept>

namespace {
  spot::formula shift_outputs (spot::formula formula, const std::vector<std::string>& outputs) {
    auto shift = [&outputs] (auto&& self, spot::formula node) -> spot::formula {
      if (node.is (spot::op::ap) && std::ranges::find (outputs, node.ap_name ()) != outputs.end ())
        return spot::formula::X (node);
      return node.map ([&] (spot::formula child) { return self (self, child); });
    };
    return shift (shift, formula);
  }

  size_t component_count (spot::formula formula, const std::vector<std::string>& inputs,
                          const std::vector<std::string>& outputs, bool real, bool shifted) {
    auto [forms, partitions] = spot::split_independent_formulas (formula, outputs);
    // The runner keeps its original objective unless decomposition yields
    // multiple components; Spot can rewrite even its sole returned component.
    if (forms.size () <= 1)
      forms = {formula};
    size_t maximum = 0;
    for (auto form : forms) {
      if (real && forms.size () > 1) {
        spot::realizability_simplifier simplifier (form, inputs);
        form = simplifier.simplified_formula ();
      }
      if (real)
        form = spot::formula::Not (form);
      if (shifted)
        form = shift_outputs (form, outputs);
      maximum = std::max (maximum, acacia::translation::distinct_promises (form));
    }
    return maximum;
  }
}

int main (int argc, char** argv) {
  if (argc == 2 && std::string_view {argv[1]} == "--self-test") {
    const auto formula = spot::parse_infix_psl ("(G F a -> G !b) & (G F a -> G F b)").f;
    using namespace acacia::translation;
    const auto positive = distinct_promises (formula);
    const auto negative = distinct_promises (spot::formula::Not (formula));
    if (component_count (formula, {"a"}, {"b"}, false, false) != positive ||
        component_count (formula, {"a"}, {"b"}, false, true) != positive ||
        component_count (formula, {"a"}, {"b"}, true, false) != negative)
      return 1;
    return 0;
  }
  if (argc != 3) {
    std::cerr << "usage: event-univ-census SOURCE_MAP CORPUS_ROOT\n";
    return 2;
  }
  std::ifstream sources (argv[1]);
  if (!sources)
    return 2;
  std::string row;
  std::getline (sources, row);
  unsigned ordinal = 0, failures = 0;
  std::cout << "source_row\tfrontend_positive\tfrontend_negative\tsimplified_positive\t"
               "simplified_negative\treal_components_max\tunreal_formula_components_max\t"
               "unreal_automaton_components_max\twidth\treal_guard\tunreal_formula_guard\t"
               "unreal_automaton_guard\tstatus\n";
  while (std::getline (sources, row)) {
    ++ordinal;
    try {
      const auto tab = row.find ('\t');
      if (tab == std::string::npos)
        throw std::runtime_error ("source row lacks a path");
      const auto spec =
          acacia::tlsf_frontend::load (std::string (argv[2]) + '/' + row.substr (tab + 1));
      const auto parsed = spot::parse_infix_psl (spec.formula);
      if (!parsed.f || !parsed.errors.empty ())
        throw std::runtime_error ("frontend formula did not parse");
      spot::realizability_simplifier simplifier (parsed.f, spec.inputs);
      const auto simplified = simplifier.simplified_formula ();
      using namespace acacia::translation;
      const auto real = component_count (simplified, spec.inputs, spec.outputs, true, false);
      const auto unreal_formula =
          component_count (simplified, spec.inputs, spec.outputs, false, true);
      const auto unreal_automaton =
          component_count (simplified, spec.inputs, spec.outputs, false, false);
      std::cout << ordinal << '\t' << distinct_promises (parsed.f) << '\t'
                << distinct_promises (spot::formula::Not (parsed.f)) << '\t'
                << distinct_promises (simplified) << '\t'
                << distinct_promises (spot::formula::Not (simplified)) << '\t' << real << '\t'
                << unreal_formula << '\t' << unreal_automaton << '\t' << acceptance_set_width ()
                << '\t' << needs_event_univ (real) << '\t' << needs_event_univ (unreal_formula)
                << '\t' << needs_event_univ (unreal_automaton) << "\tok\n";
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "row " << ordinal << ": " << error.what () << '\n';
    }
  }
  return failures ? 1 : 0;
}
