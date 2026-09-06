/// Replay P3 queries on a captured worker HOA graph, without translation,
/// postprocessing, a second negation, or a game backend. Loading HOA is eager.
#include "solver/spot_letter_oracle.hh"

#include <spot/parseaut/public.hh>

#include <charconv>
#include <iostream>
#include <sstream>
#include <string>

namespace {
  using namespace acacia::spot_letters;
  using namespace acacia::spot_rows;

  struct options {
      std::string hoa, mode, query;
      std::optional<std::string> partition, rank, target;
      std::vector<std::string> generators;
      std::optional<std::size_t> bool_threshold;
      std::optional<StateId> coordinate;
      std::optional<std::int64_t> height;
      std::int32_t k = 0;
      Epoch epoch = 0;
      RowLimits rows;
      QueryLimits limits;
  };
  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-spot-letter-oracle-replay: " + message);
  }
  std::string need_argument (int& index, int argc, char** argv) {
    if (++index >= argc) fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
  }
  template <typename T> T number (const std::string& text, const std::string& option) {
    T value {};
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), value);
    if (text.empty () || parsed.ec != std::errc {} || parsed.ptr != text.data () + text.size ())
      fail (option + " requires an integer in range");
    return value;
  }
  Rank rank_of (const std::string& text) {
    Rank result;
    if (text.empty ()) return result;  // generic empty support
    if (text.back () == ',') fail ("rank has a trailing comma");
    std::istringstream values {text};
    std::string value;
    while (std::getline (values, value, ',')) result.push_back (number<std::int32_t> (value, "rank"));
    return result;
  }
  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --hoa FILE --mode MODE --k K --partition uc... --query QUERY\n"
        << "       [--rank R0,R1,...] [--target R0,R1,...] [--generator R0,R1,...]...\n"
        << "       [--coordinate Q --height H] [--epoch N] [--bool-threshold N]\n"
        << "       [--max-rows N] [--max-row-edges N] [--max-steps N] [--max-live-nodes N]\n"
        << "  MODE: frozen-acacia (requires recorded --bool-threshold) or generic-transition-buchi.\n"
        << "  QUERY: threshold, unsafe, up, down, eq, bad, good, losing, invariant.\n"
        << "  --partition records the ALREADY TRANSFORMED worker partition in HOA AP order:\n"
        << "  u = input, c = output; use '' for no APs. No assumptions/output filters.\n"
        << "  --rank defaults to the initial rank; invariant checks it against the generators.\n"
        << "  up/down/eq require --target; bad/good/losing/invariant use repeated --generator.\n"
        << "  Generic ranks use discovered StateIds, initially only StateId 0; rows are requested\n"
        << "  only for active sources. Use frozen mode for captured graph-coordinate ranks.\n"
        << "  BDD rows report a false-first total model in the recorded HOA AP order.\n"
        << "  Exit 0: completed query (UNRESOLVED is not WIN); 2: UNKNOWN; 1: invalid invocation.\n";
  }
  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help") { usage (std::cout, argv[0]); std::exit (0); }
      auto next = [&] { return need_argument (i, argc, argv); };
      if (arg == "--hoa") result.hoa = next ();
      else if (arg == "--mode") result.mode = next ();
      else if (arg == "--query") result.query = next ();
      else if (arg == "--partition") result.partition = next ();
      else if (arg == "--rank") result.rank = next ();
      else if (arg == "--target") result.target = next ();
      else if (arg == "--generator") result.generators.push_back (next ());
      else if (arg == "--k") result.k = number<std::int32_t> (next (), arg);
      else if (arg == "--epoch") result.epoch = number<Epoch> (next (), arg);
      else if (arg == "--bool-threshold") result.bool_threshold = number<std::size_t> (next (), arg);
      else if (arg == "--coordinate") result.coordinate = number<StateId> (next (), arg);
      else if (arg == "--height") result.height = number<std::int64_t> (next (), arg);
      else if (arg == "--max-rows") result.rows.max_rows = number<std::size_t> (next (), arg);
      else if (arg == "--max-row-edges") result.rows.max_edges_per_row = number<std::size_t> (next (), arg);
      else if (arg == "--max-steps") result.limits.max_steps = number<std::size_t> (next (), arg);
      else if (arg == "--max-live-nodes") result.limits.max_live_nodes = number<std::size_t> (next (), arg);
      else fail ("unknown option " + arg);
    }
    if (result.hoa.empty () || not result.partition || result.k < 1)
      fail ("--hoa, --partition and positive --k are required");
    if (result.mode != "frozen-acacia" && result.mode != "generic-transition-buchi")
      fail ("--mode must explicitly select an increment convention");
    if ((result.mode == "frozen-acacia") != result.bool_threshold.has_value ())
      fail ("--bool-threshold is required exactly in frozen-acacia mode");
    const std::set<std::string> queries {"threshold", "unsafe", "up", "down", "eq", "bad", "good", "losing", "invariant"};
    if (not queries.count (result.query)) fail ("--query must name a supported query");
    const bool target = result.query == "up" || result.query == "down" || result.query == "eq";
    if (target != result.target.has_value ()) fail ("--target is required exactly for up/down/eq");
    if (result.query == "threshold") {
      if (not result.coordinate || not result.height) fail ("threshold requires --coordinate and --height");
    }
    else if (result.coordinate || result.height) fail ("--coordinate/--height require threshold");
    if (not result.generators.empty () && result.query != "bad" && result.query != "good"
        && result.query != "losing" && result.query != "invariant")
      fail ("--generator requires bad/good/losing/invariant");
    return result;
  }
}  // namespace

int main (int argc, char** argv) {
  try {
    const auto args = parse_options (argc, argv);
    const auto dictionary = spot::make_bdd_dict ();
    // bdd_init installs its default hook: install ours AFTER initialization.
    // Covers HOA parsing, AP construction, queries, rendering and teardown.
    BuddyErrors errors;
    const auto parsed = spot::parse_aut (args.hoa, dictionary);
    if (not parsed || parsed->format_errors (std::cerr) || parsed->aborted || not parsed->aut)
      fail ("cannot read worker HOA graph");
    const auto graph = parsed->aut;
    if (args.partition->size () != graph->ap ().size ()) fail ("partition needs one u/c per HOA AP");
    WorkerAlphabet alphabet {graph->ap_vars (), bddtrue, bddtrue, {}};
    std::size_t index = 0;
    for (const auto& ap : graph->ap ()) {
      const int var = graph->get_dict ()->varnum (ap);
      alphabet.order.push_back (var);
      switch ((*args.partition)[index++]) {
        case 'u': alphabet.inputs &= bdd_ithvar (var); break;
        case 'c': alphabet.outputs &= bdd_ithvar (var); break;
        default: fail ("partition accepts only u/c");
      }
    }
    auto rows = args.mode == "frozen-acacia"
                    ? std::make_shared<SpotRows> (FrozenAcacia {graph, *args.bool_threshold}, args.rows)
                    : std::make_shared<SpotRows> (GenericTransitionBuchi {graph}, args.rows);
    Oracle oracle {rows, alphabet, args.k};
    oracle.set_limits (args.limits);
    const Rank rank = args.rank ? rank_of (*args.rank) : rows->initial_rank ();
    std::vector<Rank> generators;
    for (const auto& text : args.generators) generators.push_back (rank_of (text));
    std::string status, model = "NA", truth = "NA";
    int nodes = 0;
    Unknown unknown = Unknown::none;
    if (args.query == "losing") {
      const auto result = oracle.losing (rank, generators, args.epoch);
      unknown = result.unknown;
      status = not result.value ? "UNKNOWN" : *result.value == Losing::proved_losing ? "PROVED_LOSING" : "UNRESOLVED";
    }
    else if (args.query == "invariant") {
      const auto result = oracle.invariant (rank, generators, args.epoch);
      unknown = result.unknown;
      status = not result.value ? "UNKNOWN" : *result.value == Invariant::verified ? "VERIFIED" : "REJECTED";
    }
    else {
      Result<bdd> result;
      if (args.query == "threshold") result = oracle.threshold (rank, *args.coordinate, *args.height);
      else if (args.query == "unsafe") result = oracle.unsafe (rank);
      else if (args.query == "up") result = oracle.up_pre (rank, rank_of (*args.target));
      else if (args.query == "down") result = oracle.down_pre (rank, rank_of (*args.target));
      else if (args.query == "eq") result = oracle.eq (rank, rank_of (*args.target));
      else if (args.query == "bad") result = oracle.bad (rank, generators, args.epoch);
      else result = oracle.good (rank, generators, args.epoch);
      unknown = result.unknown;
      status = result.value ? "COMPLETE" : "UNKNOWN";
      if (result.value) {
        // Rendering is also a checked query: failure must suppress the predicate.
        const auto rendered = oracle.query<std::tuple<int, std::string, std::string>> ([&] (auto& b) {
          const bdd f = *result.value;
          const auto selected = b.model (f, Variables::all);
          std::string bits = selected ? "" : "UNSAT";
          if (selected)
            for (const int var : alphabet.order)
              bits += b.satisfiable (b.land (*selected, bdd_ithvar (var))) ? '1' : '0';
          return std::make_tuple (b.nodes (f), b.tautology (f) ? "true" : b.satisfiable (f) ? "mixed" : "false", bits);
        });
        if (rendered.value) std::tie (nodes, truth, model) = *rendered.value;
        else { status = "UNKNOWN"; unknown = rendered.unknown; }
      }
    }
    const auto& metrics = oracle.metrics ();
    std::cout << "mode\tquery\tstatus\treason\tk\tepoch\tpartition\tap_order\tstates\tcomplete_rows"
                 "\tnodes\ttruth\tmodel\tsteps\tbdd_operations\tpeak_live_nodes\tpeak_result_nodes\n"
              << args.mode << '\t' << args.query << '\t' << status << '\t' << unknown_name (unknown)
              << '\t' << args.k << '\t' << args.epoch << '\t' << *args.partition << '\t';
    // HOA AP indices record the semantic order without leaking manager node IDs.
    for (std::size_t i = 0; i < alphabet.order.size (); ++i) std::cout << (i ? "," : "") << i;
    std::cout << '\t' << rows->state_count () << '\t' << rows->complete_rows () << '\t'
              << (status == "UNKNOWN" ? "NA" : std::to_string (nodes)) << '\t' << truth << '\t' << model
              << '\t' << metrics.steps << '\t' << metrics.bdd_operations << '\t' << metrics.peak_live_nodes
              << '\t' << metrics.peak_result_nodes << '\n';
    return status == "UNKNOWN" ? 2 : 0;
  }
  catch (const std::bad_alloc&) { std::cerr << "UNKNOWN: allocation failure\n"; return 2; }
  catch (const std::length_error&) { std::cerr << "UNKNOWN: resource limit\n"; return 2; }
  catch (const std::exception& error) { std::cerr << error.what () << '\n'; return 1; }
}
