#include "solver/translator_options.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <spot/tl/apcollect.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/synthesis.hh>
#include <sstream>
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

  std::vector<spot::formula> component_frames (spot::formula formula,
                                               const std::vector<std::string>& inputs,
                                               const std::vector<std::string>& outputs, bool real,
                                               bool shifted) {
    auto [forms, partitions] = spot::split_independent_formulas (formula, outputs);
    // Match the runner: a sole decomposition result never replaces its objective.
    if (forms.size () <= 1)
      forms = {formula};
    for (auto& form : forms) {
      if (real && forms.size () > 1) {
        spot::realizability_simplifier simplifier (form, inputs);
        form = simplifier.simplified_formula ();
      }
      if (real)
        form = spot::formula::Not (form);
      if (shifted)
        form = shift_outputs (form, outputs);
    }
    return forms;
  }

  size_t maximum_promises (const std::vector<spot::formula>& forms) {
    size_t maximum = 0;
    for (const auto& form : forms)
      maximum = std::max (maximum, acacia::translation::distinct_promises (form));
    return maximum;
  }

  void emit (std::ostream& out, unsigned ordinal, const char* frame,
             const std::vector<spot::formula>& forms) {
    using namespace acacia::translation;
    for (size_t i = 0; i < forms.size (); ++i) {
      const auto promises = distinct_promises (forms[i]);
      out << ordinal << "\tok\t" << frame << '\t' << i + 1 << '\t' << forms.size () << '\t'
          << promises << '\t' << acceptance_set_width () << '\t' << needs_event_univ (promises)
          << std::endl;
    }
  }
}

int main (int argc, char** argv) {
  if (argc == 2 && std::string_view {argv[1]} == "--self-test") {
    const auto formula = spot::parse_infix_psl ("(G F a -> G !b) & (G F a -> G F b)").f;
    using namespace acacia::translation;
    const auto positive = distinct_promises (formula);
    const auto negative = distinct_promises (spot::formula::Not (formula));
    if (maximum_promises (component_frames (formula, {"a"}, {"b"}, false, false)) != positive ||
        maximum_promises (component_frames (formula, {"a"}, {"b"}, false, true)) != positive ||
        maximum_promises (component_frames (formula, {"a"}, {"b"}, true, false)) != negative)
      return 1;
    const auto independent = spot::parse_infix_psl ("G F b & G F c").f;
    const auto components = component_frames (independent, {"a"}, {"b", "c"}, false, false);
    if (components.size () != 2)
      return 1;
    std::ostringstream rows;
    emit (rows, 7, "automaton-unreal", components);
    const auto text = rows.str ();
    if (std::count (text.begin (), text.end (), '\n') != 2 ||
        !text.starts_with ("7\tok\tautomaton-unreal\t1\t2\t") ||
        text.find ("\n7\tok\tautomaton-unreal\t2\t2\t") == std::string::npos)
      return 1;
    return 0;
  }
  if (argc != 3) {
    std::cerr << "usage: event-univ-census SOURCE_MAP CORPUS_ROOT\n"
                 "For bounded per-input isolation, use tests/event-univ-census-isolated.py.\n";
    return 2;
  }
  std::ifstream sources (argv[1]);
  if (!sources)
    return 2;
  std::string row;
  std::getline (sources, row);
  unsigned ordinal = 0, failures = 0;
  std::cout << "source_row\tstatus\tframe\tcomponent\texpected_components\tdistinct_promises\t"
               "width\tguard_fires\n"
            << std::flush;
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
      emit (std::cout, ordinal, "raw-positive", {parsed.f});
      emit (std::cout, ordinal, "raw-negative", {spot::formula::Not (parsed.f)});
      spot::realizability_simplifier simplifier (parsed.f, spec.inputs);
      const auto simplified = simplifier.simplified_formula ();
      emit (std::cout, ordinal, "simplified-positive", {simplified});
      emit (std::cout, ordinal, "simplified-negative", {spot::formula::Not (simplified)});
      emit (std::cout, ordinal, "real-components",
            component_frames (simplified, spec.inputs, spec.outputs, true, false));
      emit (std::cout, ordinal, "formula-unreal",
            component_frames (simplified, spec.inputs, spec.outputs, false, true));
      emit (std::cout, ordinal, "automaton-unreal",
            component_frames (simplified, spec.inputs, spec.outputs, false, false));
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "row " << ordinal << ": " << error.what () << '\n';
    }
  }
  return failures ? 1 : 0;
}
