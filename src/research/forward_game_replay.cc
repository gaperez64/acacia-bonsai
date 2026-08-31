/// Run the explicit F0 reachable-game oracle on an existing schema-3 replay
/// directory.  This deliberately reuses meta.tsv and all-input-actions.tsv;
/// it introduces no new replay format.  K is supplied on the command line
/// because the recorded action table is shared by checkpoints at every bound.

#include "research/all_input_actions.hh"
#include "research/explicit_forward_game.hh"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

  using namespace acacia::research;

  struct options {
      std::filesystem::path directory;
      size_t k = std::numeric_limits<size_t>::max ();
      forward_limits limits;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-forward-game-replay: " + message);
  }

  std::string need_argument (int& index, int argc, char** argv) {
    if (++index >= argc)
      fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }

  size_t parse_size (const std::string& text, const std::string& option) {
    size_t value = 0;
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), value);
    if (text.empty () or parsed.ec != std::errc {}
        or parsed.ptr != text.data () + text.size ())
      fail (option + " requires a non-negative integer");
    return value;
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --dir DIR --k K\n"
        << "       [--max-env-nodes N] [--max-ctrl-nodes N] [--max-edges N]\n"
        << "  DIR is an ACACIA_ANTICHAIN_SNAPSHOT_DIR automaton directory holding\n"
        << "  schema-3 meta.tsv and all-input-actions.tsv.\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--dir")
        result.directory = need_argument (i, argc, argv);
      else if (argument == "--k")
        result.k = parse_size (need_argument (i, argc, argv), "--k");
      else if (argument == "--max-env-nodes")
        result.limits.max_env_nodes =
            parse_size (need_argument (i, argc, argv), "--max-env-nodes");
      else if (argument == "--max-ctrl-nodes")
        result.limits.max_ctrl_nodes =
            parse_size (need_argument (i, argc, argv), "--max-ctrl-nodes");
      else if (argument == "--max-edges")
        result.limits.max_edges =
            parse_size (need_argument (i, argc, argv), "--max-edges");
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      else
        fail ("unknown option " + argument);
    }

    if (result.directory.empty ())
      fail ("--dir is required");
    if (result.k == std::numeric_limits<size_t>::max ())
      fail ("--k is required");
    if (result.k > static_cast<size_t> (std::numeric_limits<VECTOR_ELT_T>::max ()))
      fail ("--k is outside VECTOR_ELT_T range");
    return result;
  }

  size_t meta_field (const std::filesystem::path& directory,
                     const std::string& name) {
    std::ifstream meta {directory / "meta.tsv"};
    if (not meta)
      fail ("cannot open " + (directory / "meta.tsv").string ());
    std::string header, values;
    if (not std::getline (meta, header) or not std::getline (meta, values))
      fail ("invalid " + (directory / "meta.tsv").string ());

    std::istringstream hs {header}, vs {values};
    std::string h, v;
    while (hs >> h and vs >> v)
      if (h == name)
        return parse_size (v, "meta.tsv field " + name);
    fail ("meta.tsv has no column " + name + "; a schema-2 dump cannot be used here");
  }

  const char* status_name (forward_status status) {
    switch (status) {
      case forward_status::win_k: return "win_k";
      case forward_status::lose_k: return "lose_k";
      case forward_status::resource_limit: return "resource_limit";
    }
    return "unknown";
  }

}  // namespace

int main (int argc, char** argv) {
  try {
    const options arguments = parse_options (argc, argv);
    const size_t schema_version = meta_field (arguments.directory, "schema_version");
    if (schema_version != 3)
      fail ("unsupported meta.tsv schema_version " + std::to_string (schema_version));
    const size_t states = meta_field (arguments.directory, "states");
    const size_t bool_threshold = meta_field (arguments.directory, "bool_threshold");
    const size_t init_state = meta_field (arguments.directory, "init_state");
    if (bool_threshold > states or init_state >= states)
      fail ("invalid state metadata");

    const input_action_table table =
        load_input_actions (arguments.directory / "all-input-actions.tsv", states);
    const rank_vector initial = initial_vector (states, init_state);
    const forward_result result = solve_explicit_forward_game (
        initial, table.actions, static_cast<VECTOR_ELT_T> (arguments.k),
        bool_threshold, arguments.limits);

    std::cout << "status\tk\tenv_nodes\tctrl_nodes\tedges\tstrategy_ranks\n"
              << status_name (result.status) << '\t' << arguments.k << '\t'
              << result.env_nodes << '\t' << result.ctrl_nodes << '\t'
              << result.edges << '\t' << result.strategy_ranks.size () << '\n';
    return result.status == forward_status::resource_limit ? 2 : 0;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
