// Uninstalled research helper for materializing Acacia's exact
// formula-to-automaton boundary.  TLSF parsing and normalization remain in
// tlsf-tools; Spot remains an Acacia implementation dependency.

#include "configuration.hh"
#include "error_msg.hh"
#include "solver/create_automaton.hh"
#include "solver/translator_options.hh"
#include "utils/verbose.hh"
#include "version.hh"
#include <unordered_set>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <spot/tl/apcollect.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/length.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/sccinfo.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;

namespace {

  enum class orientation { real, direct, unreal_formula, unreal_automaton };

  struct options {
      std::string formula_path;
      std::string hoa_path;
      std::string name = "-";
      std::string schedule = "off";
      std::string inputs;
      std::string outputs;
      orientation worker = orientation::real;
      spot::postprocessor::output_pref preference = spot::postprocessor::Small;
      bool realizability_simplify = false;
      bool header = true;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-automata-study: " + message);
  }

  std::string need_arg (int& index, int argc, char** argv) {
    if (++index >= argc)
      fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }

  void usage (std::ostream& out, const char* program) {
    out << "Usage: " << program << " [OPTIONS]\n"
        << "Materialize Acacia's LTL-to-HOA research boundary.\n"
        << "  --formula FILE              LTL input (required; - for stdin)\n"
        << "  --hoa FILE                  HOA output (required; - for stdout)\n"
        << "  --name NAME                 stable instance name for metrics\n"
        << "  --schedule NAME             normalization schedule label\n"
        << "  --inputs CSV --outputs CSV  game partition\n"
        << "  --orientation real|direct|unreal-formula|unreal-automaton\n"
        << "  --preference any|small|deterministic (default small)\n"
        << "  --realizability-simplify    mirror Acacia's up-front simplifier\n"
        << "  --no-header                 omit the TSV header\n"
        << "  --version, --help\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--formula")
        result.formula_path = need_arg (i, argc, argv);
      else if (argument == "--hoa")
        result.hoa_path = need_arg (i, argc, argv);
      else if (argument == "--name")
        result.name = need_arg (i, argc, argv);
      else if (argument == "--schedule")
        result.schedule = need_arg (i, argc, argv);
      else if (argument == "--inputs")
        result.inputs = need_arg (i, argc, argv);
      else if (argument == "--outputs")
        result.outputs = need_arg (i, argc, argv);
      else if (argument == "--orientation") {
        const std::string value = need_arg (i, argc, argv);
        if (value == "real")
          result.worker = orientation::real;
        else if (value == "direct")
          result.worker = orientation::direct;
        else if (value == "unreal-formula")
          result.worker = orientation::unreal_formula;
        else if (value == "unreal-automaton")
          result.worker = orientation::unreal_automaton;
        else
          fail ("unknown orientation " + value);
      }
      else if (argument == "--preference") {
        const std::string value = need_arg (i, argc, argv);
        if (value == "any")
          result.preference = spot::postprocessor::Any;
        else if (value == "small")
          result.preference = spot::postprocessor::Small;
        else if (value == "deterministic")
          result.preference = spot::postprocessor::Deterministic;
        else
          fail ("unknown preference " + value);
      }
      else if (argument == "--realizability-simplify")
        result.realizability_simplify = true;
      else if (argument == "--no-header")
        result.header = false;
      else if (argument == "--version") {
        std::cout << "acacia-automata-study " << acacia_version () << '\n';
        std::exit (0);
      }
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      else
        fail ("unknown option " + argument);
    }
    if (result.formula_path.empty () or result.hoa_path.empty ())
      fail ("--formula and --hoa are required");
    if (result.hoa_path == "-")
      result.header = false;
    return result;
  }

  std::vector<std::string> split_csv (const std::string& text) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start < text.size ()) {
      const size_t end = text.find (',', start);
      std::string value = text.substr (start, end - start);
      if (not value.empty ())
        result.push_back (std::move (value));
      if (end == std::string::npos)
        break;
      start = end + 1;
    }
    return result;
  }

  std::string read_formula (const std::string& path) {
    std::istream* input = &std::cin;
    std::ifstream file;
    if (path != "-") {
      file.open (path);
      if (not file)
        fail ("cannot open " + path);
      input = &file;
    }
    return {std::istreambuf_iterator<char> (*input), std::istreambuf_iterator<char> ()};
  }

  const char* orientation_name (orientation value) {
    switch (value) {
      case orientation::real: return "real";
      case orientation::direct: return "direct";
      case orientation::unreal_formula: return "unreal-formula";
      case orientation::unreal_automaton: return "unreal-automaton";
    }
    return "unknown";
  }

  const char* preference_name (spot::postprocessor::output_pref value) {
    switch (value) {
      case spot::postprocessor::Any: return "any";
      case spot::postprocessor::Small: return "small";
      case spot::postprocessor::Deterministic: return "deterministic";
      default: return "unknown";
    }
  }

  spot::formula shift_inputs (spot::formula formula, const std::vector<std::string>& inputs) {
    const std::unordered_set<std::string> names (inputs.begin (), inputs.end ());
    auto recurse = [&names] (auto&& self, spot::formula current) -> spot::formula {
      if (current.is (spot::op::ap) and names.contains (current.ap_name ()))
        return spot::formula::X (current);
      return current.map ([&] (spot::formula child) { return self (self, std::move (child)); });
    };
    return recurse (recurse, std::move (formula));
  }

}  // namespace

int main (int argc, char** argv) {
  try {
    const options arguments = parse_options (argc, argv);
    const std::string text = read_formula (arguments.formula_path);
    spot::parsed_formula parsed =
        spot::parse_infix_psl (text, spot::default_environment::instance (), false, false);
    if (parsed.format_errors (std::cerr) or not parsed.f)
      return EXIT_CODE_ERROR;
    spot::formula formula = parsed.f;
    const std::vector<std::string> inputs = split_csv (arguments.inputs);
    const std::vector<std::string> outputs = split_csv (arguments.outputs);
    if (arguments.realizability_simplify) {
      spot::realizability_simplifier simplifier (formula, inputs);
      formula = simplifier.simplified_formula ();
    }
    if (arguments.worker == orientation::real)
      formula = spot::formula::Not (formula);
    else if (arguments.worker == orientation::unreal_formula)
      formula = shift_inputs (std::move (formula), outputs);

    const bool unreal = arguments.worker == orientation::unreal_formula or
                        arguments.worker == orientation::unreal_automaton;
    const auto& worker_inputs = unreal ? outputs : inputs;
    const auto& worker_outputs = unreal ? inputs : outputs;
    const auto dictionary = spot::make_bdd_dict ();
    int owner = 0;
    for (const auto& name : worker_inputs)
      dictionary->register_proposition (spot::formula::ap (name), &owner);
    for (const auto& name : worker_outputs)
      dictionary->register_proposition (spot::formula::ap (name), &owner);
    spot::option_map translation_options = acacia::translation::make_options ();
    spot::translator translator (dictionary, &translation_options);
    acacia::translation::validate_options (translation_options);
    auto automaton = create_automaton (formula, translator, arguments.preference);

    std::ostream* hoa = &std::cout;
    std::ofstream hoa_file;
    if (arguments.hoa_path != "-") {
      hoa_file.open (arguments.hoa_path);
      if (not hoa_file)
        fail ("cannot write " + arguments.hoa_path);
      hoa = &hoa_file;
    }
    spot::print_hoa (*hoa, automaton);
    hoa->flush ();

    const spot::scc_info scc (automaton);
    if (arguments.header)
      std::cout << "schema_version\tname\tschedule\torientation\tpreference\t"
                   "inputs\toutputs\tformula_nodes\tstates\tedges\tacceptance_sets\t"
                   "sccs\tstate_acc\tdeterministic\tcomplete\tuniversal\thoa\n";
    if (arguments.hoa_path != "-")
      std::cout << "1\t" << arguments.name << '\t' << arguments.schedule << '\t'
                << orientation_name (arguments.worker) << '\t'
                << preference_name (arguments.preference) << '\t' << arguments.inputs << '\t'
                << arguments.outputs << '\t' << spot::length (formula) << '\t'
                << automaton->num_states () << '\t' << automaton->num_edges () << '\t'
                << automaton->num_sets () << '\t' << scc.scc_count () << '\t'
                << automaton->prop_state_acc ().is_true () << '\t'
                << spot::is_deterministic (automaton) << '\t' << spot::is_complete (automaton)
                << '\t' << spot::is_universal (automaton) << '\t' << arguments.hoa_path << '\n';
    dictionary->unregister_all_my_variables (&owner);
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return EXIT_CODE_ERROR;
  }
  return EXIT_CODE_REAL;
}
