// Uninstalled deterministic-automaton synthesis route with stage records.
#include "research/parity_route.hh"

#include "solver/spot_worker_record.hh"
#include "version.hh"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <spot/misc/version.hh>
#include <spot/parseaut/public.hh>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/hoa.hh>
#include <sstream>
#include <unistd.h>

namespace route = acacia::research::parity_route;
namespace {
  spot::formula parse (const std::string& text) {
    auto parsed = spot::parse_infix_psl (text);
    if (parsed.format_errors (std::cerr) or not parsed.f)
      throw std::runtime_error ("unable to parse formula");
    return parsed.f;
  }
  std::string read (const std::string& file) {
    std::ifstream input (file);
    if (not input)
      throw std::runtime_error ("cannot read formula file");
    std::ostringstream data;
    data << input.rdbuf ();
    return data.str ();
  }
  std::vector<std::string> csv (const std::string& text) {
    std::vector<std::string> result;
    std::istringstream input (text);
    for (std::string name; std::getline (input, name, ',');)
      result.push_back (name);
    return result;
  }
  unsigned integer (const std::string& text) {
    size_t used = 0;
    auto n = std::stoul (text, &used);
    if (used != text.size () or n > std::numeric_limits<unsigned>::max ())
      throw std::runtime_error ("invalid nonnegative integer");
    return n;
  }
  std::string quote (const std::string& text) { return acacia::spot_records::quote (text); }
  template <typename Writer>
  void write_artifact (const std::string& path, Writer write) {
    // mkstemp requires six trailing X characters and a mutable, terminated buffer.
    std::string pattern = path + '.' + std::string (6, 'X');
    const int fd = mkstemp (pattern.data ());
    if (fd < 0)
      throw std::runtime_error ("cannot create temporary artifact file");
    const std::filesystem::path temporary {pattern};
    const int closed = close (fd);
    try {
      if (closed != 0)
        throw std::runtime_error ("cannot close temporary artifact file");
      std::ofstream output (temporary);
      if (not output)
        throw std::runtime_error ("cannot open temporary artifact file");
      write (output);
      output.close ();
      if (not output)
        throw std::runtime_error ("cannot write artifact output");
      std::filesystem::rename (temporary, path);
    } catch (...) {
      std::error_code ignored;
      std::filesystem::remove (temporary, ignored);
      throw;
    }
  }
  void write_game (const std::string& path, const acacia::parity::arena_game& arena,
                   const acacia::parity::solution* result = nullptr) {
    const auto& g = arena.explicit_game;
    if (not path.empty ()) {
      write_artifact (path, [&] (std::ostream& game) {
        game << "{\"initial\":" << arena.arena->get_init_state_number () << ",\"vertices\":[";
        for (unsigned v = 0; v < g.size (); ++v) {
          if (v)
            game << ',';
          game << "{\"owner\":" << unsigned (g.owner[v]) << ",\"priority\":" << g.priority[v]
               << ",\"winner\":" << unsigned (result ? result->winner[v] : 2) << ",\"strategy\":";
          if (not result or result->strategy[v] == acacia::parity::no_vertex)
            game << "null";
          else
            game << result->strategy[v];
          game << ",\"successors\":[";
          bool first = true;
          for (unsigned to : g.out (v)) {
            if (not first)
              game << ',';
            first = false;
            game << to;
          }
          game << "]}";
        }
        game << "]}\n";
      });
    }
  }

}

int main (int argc, char** argv) {
  try {
    route::request input;
    route::options options;
    std::string hoa, formula_file, specification_file, metrics, game_file, controller_file;
    bool inputs_set = false, outputs_set = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help") {
        std::cout
            << "Usage: acacia-parity-route --inputs CSV --outputs CSV [OPTIONS]\n"
               "  --formula TEXT | --formula-file FILE | --hoa FILE\n"
               "  --source original|real-worker (default original)\n"
               "  --specification TEXT | --specification-file FILE\n"
               "  --semantics mealy|moore (original specification timing)\n"
               "  --frame original|effective-mealy (input timing, default original)\n"
               "  --solver strpm-simd|spot (tier four only)\n"
               "  --conversion acd|lar (default acd)\n"
               "  --operation-budget N (zero is unbounded)\n"
               "  --initial-k N --initial-t N (default one)\n"
               "  --metrics FILE (JSONL, including incomplete stage entries)\n"
               "  --game-output FILE --controller-output FILE (JSON game / AAG)\n"
               "  --verify on|off (check strategies and original specification, default off)\n"
               "  --cross-check (compare every winning region with Spot)\n"
               "  --version\n";
        return 0;
      }
      if (arg == "--version") {
        std::cout << "acacia-parity-route " << acacia_version () << "; Spot " << spot::version ()
                  << "; strpm source 872be945\n";
        return 0;
      }
      if (arg == "--cross-check") {
        options.cross_check = true;
        continue;
      }
      if (++i >= argc)
        throw std::runtime_error (arg + " requires an argument");
      const std::string value = argv[i];
      if (arg == "--inputs") {
        input.inputs = csv (value);
        inputs_set = true;
      }
      else if (arg == "--outputs") {
        input.outputs = csv (value);
        outputs_set = true;
      }
      else if (arg == "--formula")
        input.formula = parse (value);
      else if (arg == "--formula-file")
        formula_file = value;
      else if (arg == "--specification")
        input.specification = parse (value);
      else if (arg == "--specification-file")
        specification_file = value;
      else if (arg == "--hoa")
        hoa = value;
      else if (arg == "--metrics")
        metrics = value;
      else if (arg == "--game-output")
        game_file = value;
      else if (arg == "--controller-output")
        controller_file = value;
      else if (arg == "--source") {
        if (value == "original")
          input.source = route::source_language::original;
        else if (value == "real-worker")
          input.source = route::source_language::real_worker;
        else
          throw std::runtime_error ("source must be original or real-worker");
      }
      else if (arg == "--semantics") {
        if (value != "mealy" and value != "moore")
          throw std::runtime_error ("semantics must be mealy or moore");
        input.moore = value == "moore";
      }
      else if (arg == "--frame") {
        if (value == "original")
          input.frame = route::timing_frame::original;
        else if (value == "effective-mealy")
          input.frame = route::timing_frame::effective_mealy;
        else
          throw std::runtime_error ("frame must be original or effective-mealy");
      }
      else if (arg == "--solver") {
        if (value != "strpm-simd" and value != "spot")
          throw std::runtime_error ("solver must be strpm-simd or spot");
        options.use_spot = value == "spot";
      }
      else if (arg == "--conversion") {
        if (value != "acd" and value != "lar")
          throw std::runtime_error ("conversion must be acd or lar");
        options.use_lar = value == "lar";
      }
      else if (arg == "--verify") {
        if (value != "on" and value != "off")
          throw std::runtime_error ("verify must be on or off");
        options.verify = value == "on";
      }
      else if (arg == "--operation-budget")
        options.bounds.lift_budget = integer (value);
      else if (arg == "--initial-k")
        options.bounds.initial_k = integer (value);
      else if (arg == "--initial-t")
        options.bounds.initial_t = integer (value);
      else
        throw std::runtime_error ("unknown option " + arg);
    }
    if (not inputs_set or not outputs_set)
      throw std::runtime_error ("declare both AP partitions, including empty partitions");
    std::set<std::filesystem::path> input_paths;
    for (const auto& path : {hoa, formula_file, specification_file})
      if (not path.empty ())
        input_paths.insert (std::filesystem::weakly_canonical (path));
    std::set<std::filesystem::path> output_paths;
    for (const auto& path : {metrics, game_file, controller_file})
      if (not path.empty ()) {
        auto canonical = std::filesystem::weakly_canonical (path);
        if (input_paths.contains (canonical) or not output_paths.insert (canonical).second)
          throw std::runtime_error ("input and output artifact paths must differ");
      }
    if (not formula_file.empty ()) {
      if (input.formula)
        throw std::runtime_error ("only one formula source is allowed");
      input.formula = parse (read (formula_file));
    }
    if (not specification_file.empty ()) {
      if (input.specification)
        throw std::runtime_error ("only one specification is allowed");
      input.specification = parse (read (specification_file));
    }
    if (not hoa.empty () and input.formula)
      throw std::runtime_error ("choose a formula or HOA source");
    if (not input.specification and input.source == route::source_language::original)
      input.specification = input.formula;
    std::ofstream records;
    if (not metrics.empty ()) {
      records.open (metrics);
      if (not records)
        throw std::runtime_error ("cannot write metrics file");
    }
    const auto emit = [&] (const std::string& line) {
      if (records.is_open ()) {
        records << line << '\n';
        records.flush ();
        if (not records)
          throw std::runtime_error ("cannot write metrics file");
      }
      std::cout << line << std::endl;
    };
    emit ("{\"event\":\"identity\",\"version\":" + quote (acacia_version ()) +
          ",\"spot\":" + quote (spot::version ()) + ",\"source\":" +
          quote (input.source == route::source_language::original ? "original" : "real-worker") +
          ",\"semantics\":" + quote (input.moore ? "moore" : "mealy") + ",\"frame\":" +
          quote (input.frame == route::timing_frame::original ? "original" : "effective-mealy") +
          ",\"solver\":" + quote (options.use_spot ? "spot" : "strpm-simd") +
          ",\"conversion\":" + quote (options.use_lar ? "lar" : "acd") +
          ",\"verify\":" + quote (options.verify ? "on" : "off") +
          ",\"translator_options\":\"Generic/Deterministic/High; simul=0,ba-simul=0,"
          "det-simul=0,tls-impl=1,wdba-minimize=2\"}");
    auto observe = [&] (const char* name, const spot::twa_graph_ptr& aut, double seconds) {
      std::ostringstream row;
      row << std::setprecision (17) << "{\"stage\":" << quote (name)
          << ",\"event\":" << quote (seconds < 0 ? "begin" : "complete") << ",\"seconds\":";
      if (seconds < 0)
        row << "null";
      else
        row << seconds;
      if (aut) {
        std::ostringstream acceptance;
        acceptance << aut->get_acceptance ();
        row << ",\"states\":" << aut->num_states () << ",\"edges\":" << aut->num_edges ()
            << ",\"priorities\":" << aut->num_sets ()
            << ",\"acceptance\":" << quote (acceptance.str ());
      }
      row << '}';
      emit (row.str ());
    };
    if (not hoa.empty ()) {
      observe ("parse", nullptr, -1);
      const auto start = std::chrono::steady_clock::now ();
      auto parsed = spot::parse_aut (hoa, spot::make_bdd_dict ());
      if (parsed->format_errors (std::cerr) or not parsed->aut)
        throw std::runtime_error ("unable to parse HOA");
      input.automaton = parsed->aut;
      observe ("parse", input.automaton,
               std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ());
    }
    options.on_classified = [&] (unsigned tier, const std::string& reason) {
      emit ("{\"event\":\"tier\",\"tier\":" + std::to_string (tier) +
            ",\"reason\":" + quote (reason) + "}");
    };
    options.on_game = [&] (const acacia::parity::arena_game& same_game) {
      const auto& game = same_game.explicit_game;
      write_game (game_file, same_game);
      emit ("{\"event\":\"game\",\"vertices\":" + std::to_string (game.size ()) +
            ",\"edges\":" + std::to_string (game.successors.size ()) + "}");
    };
    options.on_solution = [&] (const acacia::parity::arena_game& game,
                               const acacia::parity::solution& solved) {
      write_game (game_file, game, &solved);
    };
    options.bounds.fallback_observer = [&] (const acacia::parity::statistics& stats,
                                            double seconds) {
      std::ostringstream row;
      row << std::setprecision (17)
          << "{\"stage\":\"fallback\",\"event\":" << quote (seconds < 0 ? "begin" : "complete")
          << ",\"seconds\":";
      if (seconds < 0)
        row << "null";
      else
        row << seconds;
      row << ",\"k\":" << stats.k << ",\"t\":" << stats.t
          << ",\"vertices\":" << stats.fallback_vertices
          << ",\"reason\":" << quote (stats.fallback_reason) << '}';
      emit (row.str ());
    };
    options.bounds.parameter_observer = [&] (const acacia::parity::statistics& stats) {
      emit ("{\"event\":\"parameters\",\"k\":" + std::to_string (stats.k) + ",\"t\":" +
            std::to_string (stats.t) + ",\"pairs\":" + std::to_string (stats.pairs) + "}");
    };
    auto answer = route::run (std::move (input), options, observe);
    const auto& g = answer.game->explicit_game;
    if (not controller_file.empty () and answer.circuit) {
      write_artifact (controller_file, [&] (std::ostream& controller) {
        spot::print_aiger (controller, answer.circuit);
      });
    }
    const auto& stats = answer.solution.stats;
    std::ostringstream summary;
    summary << std::setprecision (17) << "{\"event\":\"result\",\"tier\":" << answer.tier
            << ",\"tier_reason\":" << quote (answer.tier_reason)
            << ",\"route\":" << quote (answer.route) << ",\"verdict\":" << quote (answer.verdict)
            << ",\"checked\":" << (answer.checked ? "true" : "false")
            << ",\"verification\":" << quote (answer.verification)
            << ",\"game_vertices\":" << g.size () << ",\"game_edges\":" << g.successors.size ()
            << ",\"k\":" << stats.k << ",\"t\":" << stats.t << ",\"pairs\":" << stats.pairs
            << ",\"update_attempts\":" << stats.lift_attempts
            << ",\"fallback\":" << (stats.fallback ? "true" : "false")
            << ",\"fallback_vertices\":" << stats.fallback_vertices
            << ",\"fallback_seconds\":" << stats.fallback_seconds
            << ",\"fallback_reason\":" << quote (stats.fallback_reason) << '}';
    emit (summary.str ());
    std::cout << answer.verdict << '\n';
    return answer.verdict == "REALIZABLE" ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "acacia-parity-route: " << error.what () << '\n';
    std::cout << "UNKNOWN\n";
    return 2;
  }
}
