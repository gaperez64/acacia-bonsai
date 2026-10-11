#include "solver/translator_options.hh"
#include "tlsf_frontend.hh"

#include <algorithm>
#include <chrono>
#include <sstream>
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

  std::vector<spot::formula> component_frames (
      spot::formula formula, const std::vector<std::string>& inputs,
      const std::vector<std::string>& outputs, bool real, bool shifted) {
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
    for (const auto form : forms)
      maximum = std::max (maximum, acacia::translation::distinct_promises (form));
    return maximum;
  }

  void properties (std::ostream& out, spot::formula formula) {
    out << '\t' << formula.is_syntactic_safety () << '\t' << formula.is_syntactic_guarantee ()
        << '\t' << formula.is_syntactic_obligation () << '\t' << formula.is_syntactic_recurrence ()
        << '\t' << formula.is_syntactic_persistence ();
  }

  void frame_columns (std::ostream& out, const char* frame, size_t component, size_t components,
                       spot::formula formula) {
    using namespace acacia::translation;
    const auto promises = distinct_promises (formula);
    const auto started = std::chrono::steady_clock::now ();
    const auto probe = probe_event_univ_input (formula, promises);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds> (
        std::chrono::steady_clock::now () - started).count ();
    const auto after = probe.formula;
    out << '\t' << frame << '\t' << component << '\t' << promises << '\t'
        << distinct_promises (after) << '\t' << formula_dag_size (formula) << '\t'
        << formula_dag_size (after) << '\t' << needs_event_univ (promises) << '\t'
        << probe.changed << '\t' << elapsed;
    properties (out, formula);
    properties (out, after);
    out << '\t' << components;
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
    std::ostringstream columns;
    frame_columns (columns, "raw-positive", 1, 1, spot::formula::tt ());
    const auto text = columns.str ();
    if (std::count (text.begin (), text.end (), '\t') != 20 ||
        !text.starts_with ("\traw-positive\t1\t0\t0\t1\t1\t0\t0\t") ||
        !text.ends_with ("\t1"))
      return 1;
    const auto changed = spot::parse_infix_psl ("G F (a | G b)").f;
    const auto probe = probe_event_univ_input (changed, distinct_promises (changed));
    if (!probe.changed ||
        probe.formula != event_univ_input (changed, distinct_promises (changed),
                                          event_univ_policy::expanded).formula)
      return 1;
    std::ostringstream component;
    frame_columns (component, "automaton-unreal", 1, 2, changed);
    if (!component.str ().ends_with ("\t2"))
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
               "unreal_automaton_guard\tstatus\tframe\tcomponent\tpromises_before\t"
               "promises_after\tdag_before\tdag_after\tguard_fires\texpanded_changed\t"
               "rewrite_us\tsafety_before\tguarantee_before\tobligation_before\t"
               "recurrence_before\tpersistence_before\tsafety_after\tguarantee_after\t"
               "obligation_after\trecurrence_after\tpersistence_after\texpected_components\n";
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
      const auto real_forms =
          component_frames (simplified, spec.inputs, spec.outputs, true, false);
      const auto real = maximum_promises (real_forms);
      const auto formula_forms =
          component_frames (simplified, spec.inputs, spec.outputs, false, true);
      const auto unreal_formula = maximum_promises (formula_forms);
      const auto automaton_forms =
          component_frames (simplified, spec.inputs, spec.outputs, false, false);
      const auto unreal_automaton = maximum_promises (automaton_forms);
      std::ostringstream original;
      original << ordinal << '\t' << distinct_promises (parsed.f) << '\t'
                << distinct_promises (spot::formula::Not (parsed.f)) << '\t'
                << distinct_promises (simplified) << '\t'
                << distinct_promises (spot::formula::Not (simplified)) << '\t' << real << '\t'
                << unreal_formula << '\t' << unreal_automaton << '\t' << acceptance_set_width ()
                << '\t' << needs_event_univ (real) << '\t' << needs_event_univ (unreal_formula)
                << '\t' << needs_event_univ (unreal_automaton) << "\tok";
      const auto emit = [&original] (const char* frame, const std::vector<spot::formula>& forms) {
        for (size_t i = 0; i < forms.size (); ++i) {
          std::cout << original.str ();
          frame_columns (std::cout, frame, i + 1, forms.size (), forms[i]);
          std::cout << '\n';
        }
      };
      emit ("raw-positive", {parsed.f});
      emit ("raw-negative", {spot::formula::Not (parsed.f)});
      emit ("simplified-positive", {simplified});
      emit ("simplified-negative", {spot::formula::Not (simplified)});
      emit ("real-components", real_forms);
      emit ("formula-unreal", formula_forms);
      emit ("automaton-unreal", automaton_forms);
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "row " << ordinal << ": " << error.what () << '\n';
    }
  }
  return failures ? 1 : 0;
}
