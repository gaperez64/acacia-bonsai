// Uninstalled diagnostics helper for measuring direct-simulation closure on
// exact antichain maxima captured by the solver.

#include "configuration.hh"
#include "solver/direct_simulation.hh"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <spot/parseaut/public.hh>
#include <spot/twa/twagraph.hh>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

  struct options {
      std::filesystem::path directory;
      size_t cap = 1200;
      bool header = true;
  };

  struct metadata {
      size_t states = 0;
      size_t bool_threshold = 0;
  };

  struct snapshot {
      int k = 0;
      int loop = 0;
      std::vector<std::vector<int>> maxima;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-antichain-replay: " + message);
  }

  std::string need_arg (int& index, int argc, char** argv) {
    if (++index >= argc)
      fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }

  size_t parse_size (const std::string& text, const std::string& description) {
    size_t result = 0;
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), result);
    if (text.empty () or parsed.ec != std::errc {} or
        parsed.ptr != text.data () + text.size ())
      fail (description + " requires a non-negative integer");
    return result;
  }

  int parse_int (const std::string& text, const std::string& description) {
    long long result = 0;
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), result);
    if (text.empty () or parsed.ec != std::errc {} or
        parsed.ptr != text.data () + text.size () or
        result < std::numeric_limits<int>::min () or result > std::numeric_limits<int>::max ())
      fail (description + " requires an integer");
    return static_cast<int> (result);
  }

  void usage (std::ostream& out, const char* program) {
    out << "Usage: " << program << " --dir DIR [--cap N] [--no-header]\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--dir")
        result.directory = need_arg (i, argc, argv);
      else if (argument == "--cap")
        result.cap = parse_size (need_arg (i, argc, argv), "--cap");
      else if (argument == "--no-header")
        result.header = false;
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      else
        fail ("unknown option " + argument);
    }
    if (result.directory.empty ())
      fail ("--dir is required");
    return result;
  }

  metadata load_metadata (const std::filesystem::path& path) {
    std::ifstream input {path};
    if (not input)
      fail ("cannot read " + path.string ());

    std::string header;
    std::string row;
    if (not std::getline (input, header) or header != "states\tbool_threshold" or
        not std::getline (input, row))
      fail ("invalid metadata in " + path.string ());

    metadata result;
    std::istringstream fields {row};
    std::string extra;
    if (not (fields >> result.states >> result.bool_threshold) or fields >> extra or
        result.bool_threshold > result.states)
      fail ("invalid metadata row in " + path.string ());
    return result;
  }

  std::string header_value (const std::string& field, const std::string& prefix,
                            const std::filesystem::path& path) {
    if (not field.starts_with (prefix))
      fail ("invalid snapshot header in " + path.string ());
    return field.substr (prefix.size ());
  }

  snapshot load_snapshot (const std::filesystem::path& path, size_t states) {
    std::ifstream input {path};
    if (not input)
      fail ("cannot read " + path.string ());

    std::string line;
    if (not std::getline (input, line))
      fail ("empty snapshot " + path.string ());
    std::istringstream header {line};
    std::string marker;
    std::string k_field;
    std::string loop_field;
    std::string maxima_field;
    std::string extra;
    if (not (header >> marker >> k_field >> loop_field >> maxima_field) or marker != "#" or
        header >> extra)
      fail ("invalid snapshot header in " + path.string ());

    snapshot result;
    result.k = parse_int (header_value (k_field, "k=", path), "snapshot k");
    result.loop = parse_int (header_value (loop_field, "loop=", path), "snapshot loop");
    const size_t declared_maxima =
        parse_size (header_value (maxima_field, "maxima=", path), "snapshot maxima");

    while (std::getline (input, line)) {
      if (line.empty ())
        continue;
      std::istringstream coordinates {line};
      std::vector<int> maximum;
      long long coordinate = 0;
      while (coordinates >> coordinate) {
        if (coordinate < std::numeric_limits<int>::min () or
            coordinate > std::numeric_limits<int>::max ())
          fail ("coordinate outside integer range in " + path.string ());
        maximum.push_back (static_cast<int> (coordinate));
      }
      if (not coordinates.eof () or maximum.size () != states)
        fail ("invalid maximum in " + path.string ());
      result.maxima.push_back (std::move (maximum));
    }
    if (result.maxima.size () != declared_maxima)
      fail ("maximum count does not match header in " + path.string ());
    return result;
  }

  std::vector<std::filesystem::path> find_snapshots (const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator {directory}) {
      const std::string name = entry.path ().filename ().string ();
      if (entry.is_regular_file () and name.starts_with ("antichain-") and
          name.ends_with (".tsv"))
        result.push_back (entry.path ());
    }
    std::ranges::sort (result, {}, [] (const auto& path) { return path.filename ().string (); });
    return result;
  }

  std::vector<int> close (const std::vector<int>& source,
                          const acacia::direct_simulation::relation& simulation,
                          size_t bool_threshold) {
    std::vector<int> result = source;
    if (not simulation.computed)
      return result;
    for (size_t p = 0; p < source.size (); ++p)
      for (unsigned q : simulation.simulators[p]) {
        const int candidate = p < bool_threshold ? source[q] : source[q] == -1 ? -1 : 0;
        result[p] = std::max (result[p], candidate);
      }
    return result;
  }

  bool coordinatewise_leq (const std::vector<int>& left, const std::vector<int>& right) {
    for (size_t i = 0; i < left.size (); ++i)
      if (left[i] > right[i])
        return false;
    return true;
  }

  size_t count_maxima (const std::vector<std::vector<int>>& vectors) {
    size_t result = 0;
    for (size_t i = 0; i < vectors.size (); ++i) {
      bool dominated = false;
      for (size_t j = 0; j < vectors.size (); ++j)
        if (i != j and vectors[i] != vectors[j] and
            coordinatewise_leq (vectors[i], vectors[j])) {
          dominated = true;
          break;
        }
      if (not dominated)
        ++result;
    }
    return result;
  }

}  // namespace

int main (int argc, char** argv) {
  try {
    const options arguments = parse_options (argc, argv);
    const metadata meta = load_metadata (arguments.directory / "meta.tsv");

    auto dictionary = spot::make_bdd_dict ();
    const std::filesystem::path automaton_path = arguments.directory / "automaton.hoa";
    auto parsed = spot::parse_aut (automaton_path.string (), dictionary);
    if (parsed->format_errors (std::cerr) or not parsed->aut)
      fail ("cannot parse " + automaton_path.string ());
    const spot::twa_graph_ptr aut = parsed->aut;
    if (aut->num_states () != meta.states)
      fail ("automaton state count does not match meta.tsv");

    const acacia::direct_simulation::relation simulation =
        acacia::direct_simulation::compute (aut, arguments.cap);
    const std::vector<std::filesystem::path> snapshots = find_snapshots (arguments.directory);

    if (arguments.header)
      std::cout << "snapshot\tk\tloop\tpre_maxima\tpost_maxima\tsurvivor_ratio\t"
                   "relation_computed\tstates\tbool_threshold\tstrict_pairs\t"
                   "closure_inflationary\tclosure_idempotent\tclosure_changed\n";

    for (const auto& path : snapshots) {
      const snapshot data = load_snapshot (path, meta.states);
      std::vector<std::vector<int>> closed;
      closed.reserve (data.maxima.size ());
      bool inflationary = true;
      bool idempotent = true;
      size_t changed = 0;
      for (const auto& maximum : data.maxima) {
        std::vector<int> closure = close (maximum, simulation, meta.bool_threshold);
        // Distinguish "the closure is the identity here" from "the closure
        // moved vectors but they stayed incomparable": a survivor ratio of 1.0
        // means something very different in the two cases.
        if (closure != maximum)
          ++changed;
        inflationary = inflationary and coordinatewise_leq (maximum, closure);
        idempotent = idempotent and
                     close (closure, simulation, meta.bool_threshold) == closure;
        closed.push_back (std::move (closure));
      }

      const size_t pre_maxima = data.maxima.size ();
      const size_t post_maxima = count_maxima (closed);
      const double survivor_ratio = pre_maxima == 0
                                        ? 0.0
                                        : static_cast<double> (post_maxima) / pre_maxima;
      std::cout << path.filename ().string () << '\t' << data.k << '\t' << data.loop << '\t'
                << pre_maxima << '\t' << post_maxima << '\t' << std::fixed
                << std::setprecision (6) << survivor_ratio << '\t'
                << (simulation.computed ? 1 : 0) << '\t' << simulation.states << '\t'
                << meta.bool_threshold << '\t' << simulation.strict_pairs << '\t'
                << (inflationary ? 1 : 0) << '\t' << (idempotent ? 1 : 0) << '\t' << changed << '\n';
    }
    return 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
