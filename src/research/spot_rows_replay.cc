/// Explicit-letter P2 oracle over an existing HOA graph. Frozen mode expects
/// the exact post-preprocessing action boundary and its recorded threshold.
/// Loading this graph is eager; this tool makes no translation-savings claim.

#include "solver/spot_rows.hh"

#include <spot/parseaut/public.hh>

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {
  using namespace acacia::spot_rows;

  struct options {
      std::string hoa, mode, valuation;
      bool has_valuation = false;
      std::optional<std::string> rank;
      std::optional<std::size_t> bool_threshold;
      std::int32_t k = 0;
      RowLimits limits;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-spot-rows-replay: " + message);
  }

  template <typename T>
  T number (const std::string& text, const std::string& option) {
    T value {};
    const auto parsed = std::from_chars (text.data (), text.data () + text.size (), value);
    if (text.empty () || parsed.ec != std::errc {}
        || parsed.ptr != text.data () + text.size ())
      fail (option + " requires an integer in range");
    return value;
  }

  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --hoa FILE --mode MODE --k K --valuation BITS\n"
        << "       [--rank R0,R1,...] [--bool-threshold N]\n"
        << "       [--max-rows N] [--max-row-edges N]\n"
        << "  MODE: frozen-acacia or generic-transition-buchi (required).\n"
        << "  Frozen mode requires --bool-threshold from the action boundary.\n"
        << "  BITS lists 0/1 values in HOA AP order; use '' for an empty alphabet.\n"
        << "  The default rank is 0 at the initial state and -1 elsewhere.\n"
        << "  Generic mode uses discovered StateIds, not HOA state numbers.\n"
        << "  Exit 0: complete update; 2: RESOURCE_LIMIT; 1: invalid/failed.\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      auto next = [&] {
        if (++i >= argc)
          fail (argument + " requires an argument");
        return std::string {argv[i]};
      };
      if (argument == "--hoa") result.hoa = next ();
      else if (argument == "--mode") result.mode = next ();
      else if (argument == "--k") result.k = number<std::int32_t> (next (), argument);
      else if (argument == "--rank") result.rank = next ();
      else if (argument == "--valuation") {
        result.valuation = next ();
        result.has_valuation = true;
      }
      else if (argument == "--bool-threshold")
        result.bool_threshold = number<std::size_t> (next (), argument);
      else if (argument == "--max-rows")
        result.limits.max_rows = number<std::size_t> (next (), argument);
      else if (argument == "--max-row-edges")
        result.limits.max_edges_per_row = number<std::size_t> (next (), argument);
      else fail ("unknown option " + argument);
    }
    if (result.hoa.empty () || result.k < 1 || not result.has_valuation)
      fail ("--hoa, positive --k and --valuation are required");
    if (result.mode != "frozen-acacia" && result.mode != "generic-transition-buchi")
      fail ("--mode must explicitly select frozen-acacia or generic-transition-buchi");
    if ((result.mode == "frozen-acacia") != result.bool_threshold.has_value ())
      fail ("--bool-threshold is required exactly in frozen-acacia mode");
    return result;
  }
}  // namespace

int main (int argc, char** argv) {
  try {
    const auto args = parse_options (argc, argv);
    auto parsed = spot::parse_aut (args.hoa, spot::make_bdd_dict ());
    if (not parsed || parsed->format_errors (std::cerr) || parsed->aborted || not parsed->aut)
      fail ("cannot read HOA graph");
    auto graph = parsed->aut;
    std::unique_ptr<SpotRows> rows;
    if (args.mode == "frozen-acacia")
      rows = std::make_unique<SpotRows> (FrozenAcacia {graph, *args.bool_threshold}, args.limits);
    else
      rows = std::make_unique<SpotRows> (GenericTransitionBuchi {graph}, args.limits);

    if (args.valuation.size () != graph->ap ().size ())
      fail ("--valuation must contain one bit per HOA AP");
    bdd valuation = bddtrue;
    std::size_t i = 0;
    for (const auto& ap : graph->ap ()) {
      const auto bit = args.valuation[i++];
      if (bit != '0' && bit != '1')
        fail ("--valuation accepts only 0 and 1");
      const int var = graph->get_dict ()->varnum (ap);
      valuation &= bit == '1' ? bdd_ithvar (var) : bdd_nithvar (var);
    }
    Rank rank = rows->initial_rank ();
    if (args.rank) {
      rank.clear ();
      if (args.rank->empty () || args.rank->back () == ',')
        fail ("--rank requires comma-separated coordinates");
      std::istringstream values {*args.rank};
      std::string value;
      while (std::getline (values, value, ','))
        rank.push_back (number<std::int32_t> (value, "--rank"));
    }

    const auto result = rows->evaluate (rank, valuation, args.k);
    std::cout << "mode\tstatus\tk\tstates\tcomplete_rows\tsuccessor\tsafe\n"
              << mode_name (result.mode) << '\t' << status_name (result.status) << '\t'
              << args.k << '\t' << rows->state_count () << '\t' << rows->complete_rows () << '\t';
    if (result.rank) {
      for (std::size_t q = 0; q < result.rank->size (); ++q)
        std::cout << (q ? "," : "") << (*result.rank)[q];
      std::cout << '\t' << (rows->is_safe (*result.rank, args.k) ? "true" : "false") << '\n';
    }
    else {
      std::cout << "NA\tNA\n";
      if (result.error) {
        try { std::rethrow_exception (result.error); }
        catch (const std::exception& error) { std::cerr << error.what () << '\n'; }
      }
    }
    return result.status == Status::complete ? 0 : result.status == Status::resource_limit ? 2 : 1;
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
