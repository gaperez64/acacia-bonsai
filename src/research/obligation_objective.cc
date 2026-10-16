#include "configuration.hh"
#include "solver/obligation_objective.hh"
#if ACACIA_ENABLE_TLSF_FRONTEND
# include "tlsf_frontend.hh"
#endif

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {
  std::string quoted (const std::string& text) {
    std::ostringstream out;
    out << char (34);
    for (unsigned char c : text) {
      if (c == char (34) || c == '\\')
        out << '\\' << c;
      else if (c < 32) {
        char escaped[7];
        snprintf (escaped, sizeof escaped, "\\u%04x", c);
        out << escaped;
      }
      else
        out << c;
    }
    out << char (34);
    return out.str ();
  }
  std::string text (spot::formula f) {
    std::ostringstream out;
    out << f;
    return out.str ();
  }
  std::vector<std::string> split (const std::string& value) {
    std::vector<std::string> result;
    std::istringstream in (value);
    for (std::string item; std::getline (in, item, ',');)
      if (!item.empty ())
        result.push_back (item);
    return result;
  }
  void list (const std::vector<std::string>& values) {
    std::cout << '[';
    for (size_t i = 0; i < values.size (); ++i)
      std::cout << (i ? "," : "") << quoted (values[i]);
    std::cout << ']';
  }
}

int main (int argc, char** argv) {
  try {
    std::string formula, file, source_semantics = "Mealy", source_target = "Mealy";
    std::vector<std::string> inputs, outputs;
    bool simplify = ACACIA_ENABLE_REALIZABILITY_SIMPLIFIER, synthesis = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--synthesis") {
        synthesis = true;
        simplify = false;
      }
      else if (arg == "--no-simplify")
        simplify = false;
      else if (i + 1 < argc) {
        const std::string value = argv[++i];
        if (arg == "--formula")
          formula = value;
        else if (arg == "--formula-file") {
          std::ifstream in (value);
          if (!in)
            throw std::runtime_error ("cannot open formula file");
          formula.assign (std::istreambuf_iterator<char> (in), {});
        }
        else if (arg == "--inputs")
          inputs = split (value);
        else if (arg == "--outputs")
          outputs = split (value);
        else if (arg == "--tlsf")
          file = value;
        else
          throw std::runtime_error ("unknown argument: " + arg);
      }
      else
        throw std::runtime_error ("missing argument value");
    }
    if (!file.empty ()) {
      if (!formula.empty () || !inputs.empty () || !outputs.empty ())
        throw std::runtime_error ("use TLSF or a formula with its partition");
#if ACACIA_ENABLE_TLSF_FRONTEND
      const auto spec = acacia::tlsf_frontend::load (file);
      formula = spec.formula;
      inputs = spec.inputs;
      outputs = spec.outputs;
      source_semantics = spec.metadata.tlsf_semantics;
      source_target = spec.metadata.tlsf_target;
#else
      throw std::runtime_error ("TLSF frontend is disabled in this build");
#endif
    }
    if (formula.empty ())
      throw std::runtime_error ("use --tlsf FILE or --formula TEXT --inputs CSV --outputs CSV");
    std::set<std::string> declared;
    for (const auto* block : {&inputs, &outputs})
      for (const auto& ap : *block)
        if (!declared.insert (ap).second)
          throw std::runtime_error ("duplicate or overlapping partition signal");
    auto raw = acacia::obligation::prepare_objective (formula, inputs, false);
    raw.traverse ([&] (spot::formula ap) {
      if (ap.is (spot::op::ap) && !declared.contains (ap.ap_name ()))
        throw std::runtime_error ("formula signal is absent from the partition");
      return false;
    });
    auto objective = acacia::obligation::prepare_objective (formula, inputs, simplify);
    std::set<spot::formula> dag;
    std::set<std::string> used;
    std::vector<std::string> traversal;
    objective.traverse ([&] (spot::formula f) {
      if (!dag.insert (f).second)
        return true;
      if (f.is (spot::op::ap)) {
        used.insert (f.ap_name ());
        traversal.push_back (f.ap_name ());
      }
      return false;
    });
    auto dict = spot::make_bdd_dict ();
    int owner = 0;
    acacia::obligation::register_first_block (objective, dict, inputs, &owner);
    std::vector<std::pair<int, std::string>> registered;
    for (const auto& ap : traversal)
      if (std::ranges::find (inputs, ap) != inputs.end ())
        registered.emplace_back (dict->varnum (spot::formula::ap (ap)), ap);
    std::ranges::sort (registered);
    std::vector<std::string> first;
    for (const auto& [index, ap] : registered)
      first.push_back (ap);
    dict->unregister_all_my_variables (&owner);
    std::cout << "{\"normalized_formula\":" << quoted (text (raw))
              << ",\"objective\":" << quoted (text (objective))
              << ",\"eligible\":" << (objective.is_syntactic_obligation () ? "true" : "false")
              << ",\"simplified\":" << (simplify ? "true" : "false")
              << ",\"decision_only\":" << (synthesis ? "false" : "true")
              << ",\"effective_timing\":\"Mealy\",\"source_semantics\":"
              << quoted (source_semantics) << ",\"source_target\":" << quoted (source_target)
              << ",\"dag_nodes\":" << dag.size () << ",\"ap_count\":" << used.size ()
              << ",\"inputs\":";
    list (inputs);
    std::cout << ",\"outputs\":";
    list (outputs);
    std::cout << ",\"first_registered_inputs\":";
    list (first);
    std::vector<std::string> used_outputs, absent;
    for (const auto& ap : traversal)
      if (std::ranges::find (outputs, ap) != outputs.end ())
        used_outputs.push_back (ap);
    for (const auto& ap : declared)
      if (!used.contains (ap))
        absent.push_back (ap);
    std::cout << ",\"used_outputs_in_traversal_order\":";
    list (used_outputs);
    std::cout << ",\"absent_declared_aps\":";
    list (absent);
    std::cout << ",\"mapping\":[";
    if (simplify) {
      // Research-only repeat retains mappings without changing the arm's preparation cost.
      spot::realizability_simplifier rs (raw, inputs);
      if (rs.simplified_formula () != objective)
        throw std::runtime_error ("simplifier mapping disagrees with the arm objective");
      size_t i = 0;
      for (const auto& [from, input, to] : rs.get_mapping ())
        std::cout << (i++ ? "," : "") << "{\"from\":" << quoted (text (from))
                  << ",\"input\":" << (input ? "true" : "false")
                  << ",\"to\":" << quoted (text (to)) << '}';
    }
    std::cout << "],\"components\":1,\"simplify_terms\":true,\"debug\":-1}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 2;
  }
}
