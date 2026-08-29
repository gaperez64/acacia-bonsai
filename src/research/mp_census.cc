// Uninstalled research helper for measuring Manna-Pnueli extraction on the
// safe formulas seen by the REAL worker.  It does not alter the production
// formula frontend or solver selection.

#include "solver/mp_nba.hh"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <spot/tl/apcollect.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/synthesis.hh>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

  struct options {
    std::string formula_path;
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    acacia::mp_nba::extraction_options extraction;
    bool inputs_given = false;
    bool outputs_given = false;
    bool header = true;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-mp-census: " + message);
  }

  std::string need_arg (int& index, int argc, char** argv) {
    if (++index >= argc)
      fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }

  size_t parse_size (const std::string& text, const std::string& option) {
    size_t result = 0;
    const auto parsed =
        std::from_chars (text.data (), text.data () + text.size (), result);
    if (text.empty () or parsed.ec != std::errc {}
        or parsed.ptr != text.data () + text.size ())
      fail (option + " requires a non-negative integer");
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

  void usage (std::ostream& out, const char* program) {
    out << "Usage: " << program
        << " --formula FILE --inputs CSV --outputs CSV [--name NAME] [--no-header]\n"
        << "       [--node-cap N] [--cube-cap N] [--predicate-cap N]\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--formula")
        result.formula_path = need_arg (i, argc, argv);
      else if (argument == "--inputs") {
        result.inputs = split_csv (need_arg (i, argc, argv));
        result.inputs_given = true;
      }
      else if (argument == "--outputs") {
        result.outputs = split_csv (need_arg (i, argc, argv));
        result.outputs_given = true;
      }
      else if (argument == "--name")
        result.name = need_arg (i, argc, argv);
      else if (argument == "--no-header")
        result.header = false;
      else if (argument == "--node-cap")
        result.extraction.node_cap =
            parse_size (need_arg (i, argc, argv), "--node-cap");
      else if (argument == "--cube-cap")
        result.extraction.cube_cap =
            parse_size (need_arg (i, argc, argv), "--cube-cap");
      else if (argument == "--predicate-cap")
        result.extraction.predicate_cap =
            parse_size (need_arg (i, argc, argv), "--predicate-cap");
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (EXIT_SUCCESS);
      }
      else
        fail ("unknown option " + argument);
    }

    if (result.formula_path.empty ())
      fail ("--formula is required");
    if (not result.inputs_given or not result.outputs_given)
      fail ("--inputs and --outputs are required");
    if (result.name.empty ())
      result.name = result.formula_path;
    return result;
  }

  std::string read_formula (const std::string& path) {
    std::ifstream input {path};
    if (not input)
      fail ("cannot open " + path);
    return {std::istreambuf_iterator<char> (input),
            std::istreambuf_iterator<char> ()};
  }

  const char* status_name (acacia::mp_nba::extraction_status status) {
    using acacia::mp_nba::extraction_status;
    switch (status) {
      case extraction_status::accepted: return "accepted";
      case extraction_status::unsupported: return "unsupported";
      case extraction_status::node_cap: return "node_cap";
      case extraction_status::cube_cap: return "cube_cap";
      case extraction_status::predicate_cap: return "predicate_cap";
    }
    return "unsupported";
  }

  std::string component_aps (spot::formula formula) {
    spot::atomic_prop_set propositions;
    spot::atomic_prop_collect (formula, &propositions);
    std::vector<std::string> names;
    names.reserve (propositions.size ());
    for (spot::formula proposition : propositions)
      names.push_back (proposition.ap_name ());
    std::sort (names.begin (), names.end ());

    std::string result;
    for (const std::string& name : names) {
      if (not result.empty ())
        result += ',';
      result += name;
    }
    return result;
  }

  void emit_row (const options& arguments, long component_index,
                 size_t component_count, spot::formula formula,
                 const spot::bdd_dict_ptr& dictionary) {
    acacia::mp_nba::extraction_stats stats;
    [[maybe_unused]] auto cubes = acacia::mp_nba::extract_cubes (
        formula, dictionary, arguments.extraction, &stats);
    std::cout << arguments.name << '\t' << component_index << '\t'
              << component_count << '\t' << stats.nodes_before << '\t'
              << stats.nodes_after_delta2 << '\t'
              << status_name (stats.status) << '\t' << stats.cubes << '\t'
              << stats.predicates << '\t' << stats.max_inf_width << '\t'
              << component_aps (formula) << '\t' << formula << '\n';
  }

}  // namespace

int main (int argc, char** argv) {
  try {
    const options arguments = parse_options (argc, argv);
    const std::string text = read_formula (arguments.formula_path);
    spot::parsed_formula parsed = spot::parse_infix_psl (
        text, spot::default_environment::instance (), false, false);
    if (parsed.format_errors (std::cerr) or not parsed.f)
      return EXIT_FAILURE;

    spot::realizability_simplifier simplifier (parsed.f, arguments.inputs);
    const spot::formula whole_formula = simplifier.simplified_formula ();
    auto split =
        spot::split_independent_formulas (whole_formula, arguments.outputs);
    std::vector<spot::formula> components = std::move (split.first);

    // The production REAL decision path repeats this simplification on each
    // component when decomposition produced more than one component.
    if (components.size () > 1)
      for (spot::formula& component : components) {
        spot::realizability_simplifier component_simplifier (
            component, arguments.inputs);
        component = component_simplifier.simplified_formula ();
      }

    if (arguments.header)
      std::cout << "name\tcomponent_index\tcomponent_count\tnodes_before\t"
                   "nodes_after_delta2\tstatus\tcubes\tpredicates\t"
                   "max_inf_width\tcomponent_aps\tcomponent\n";

    const auto dictionary = spot::make_bdd_dict ();
    emit_row (arguments, -1, components.size (), whole_formula, dictionary);
    for (size_t index = 0; index < components.size (); ++index)
      emit_row (arguments, static_cast<long> (index), components.size (),
                components[index], dictionary);
    return EXIT_SUCCESS;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return EXIT_FAILURE;
  }
}
