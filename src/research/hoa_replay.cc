// Uninstalled research helper for replaying the exact post-translation HOA
// boundary.  The production acacia-bonsai CLI and formula frontend are not
// extended by this tool.

#include "configuration.hh"
#include "error_msg.hh"
#include "solver/configured_components.hh"
#include "solver/diagnostics.hh"
#include "solver/solve_game.hh"
#include "solver/spot_nba_fastpath.hh"
#include "utils/push_aps.hh"
#include "utils/verbose.hh"
#include "version.hh"

#include <algorithm>
#include <bddx.h>
#include <iostream>
#include <spot/parseaut/public.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/sbacc.hh>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <posets/vectors/traits.hh>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
size_t posets::vectors::bool_threshold = 0;

namespace {

#define ACACIA_HOA_STRINGIFY_INNER(x) #x
#define ACACIA_HOA_STRINGIFY(x) ACACIA_HOA_STRINGIFY_INNER (x)

  enum class orientation { real, unreal_formula, unreal_automaton, direct };

  struct options {
      std::string hoa;
      std::vector<std::string> inputs;
      std::vector<std::string> outputs;
      orientation worker = orientation::real;
      VECTOR_ELT_T kmax = DEFAULT_K;
      VECTOR_ELT_T kmin = DEFAULT_KMIN;
      VECTOR_ELT_T kinc = DEFAULT_KINC;
      SPOT_FAST_T spot_fast = DEFAULT_SPOT_FAST;
  };

  [[noreturn]] void fail (const std::string& message) {
    throw std::runtime_error ("acacia-hoa-replay: " + message);
  }

  std::string need_arg (int& index, int argc, char** argv) {
    if (++index >= argc)
      fail (std::string {argv[index - 1]} + " requires an argument");
    return argv[index];
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

  VECTOR_ELT_T parse_counter (const std::string& text, const char* option) {
    size_t consumed = 0;
    const long value = std::stol (text, &consumed);
    if (consumed != text.size () or value < 1 or value > 127)
      fail (std::string {option} + " must be in 1..127");
    return static_cast<VECTOR_ELT_T> (value);
  }

  void usage (std::ostream& out, const char* program) {
    out << "Usage: " << program << " --hoa FILE --inputs CSV --outputs CSV [OPTIONS]\n"
        << "  --orientation real|unreal-formula|unreal-automaton|direct\n"
        << "  -K N  final bound (default " << static_cast<int> (DEFAULT_K) << ")\n"
        << "  -k N  initial bound (default " << static_cast<int> (DEFAULT_KMIN) << ")\n"
        << "  -I N  bound increment (default " << static_cast<int> (DEFAULT_KINC) << ")\n"
        << "  --spot-fast off|det\n"
        << "  -v, --version, --help\n";
  }

  options parse_options (int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--hoa")
        result.hoa = need_arg (i, argc, argv);
      else if (argument == "--inputs")
        result.inputs = split_csv (need_arg (i, argc, argv));
      else if (argument == "--outputs")
        result.outputs = split_csv (need_arg (i, argc, argv));
      else if (argument == "--orientation") {
        const std::string value = need_arg (i, argc, argv);
        if (value == "real")
          result.worker = orientation::real;
        else if (value == "unreal-formula")
          result.worker = orientation::unreal_formula;
        else if (value == "unreal-automaton")
          result.worker = orientation::unreal_automaton;
        else if (value == "direct")
          result.worker = orientation::direct;
        else
          fail ("unknown orientation " + value);
      }
      else if (argument == "-K")
        result.kmax = parse_counter (need_arg (i, argc, argv), "-K");
      else if (argument == "-k")
        result.kmin = parse_counter (need_arg (i, argc, argv), "-k");
      else if (argument == "-I")
        result.kinc = parse_counter (need_arg (i, argc, argv), "-I");
      else if (argument == "--spot-fast") {
        const std::string value = need_arg (i, argc, argv);
        if (value == "off")
          result.spot_fast = SPOT_FAST_OFF;
        else if (value == "det")
          result.spot_fast = SPOT_FAST_DET;
        else
          fail ("--spot-fast must be off or det");
      }
      else if (argument == "-v")
        ++utils::verbose;
      else if (argument == "--version") {
        std::cout << "acacia-hoa-replay " << acacia_version () << '\n';
        std::exit (0);
      }
      else if (argument == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      else
        fail ("unknown option " + argument);
    }
    if (result.hoa.empty ())
      fail ("--hoa is required");
    if (result.kmin > result.kmax)
      fail ("-k must not exceed -K");
    for (const auto& input : result.inputs)
      if (std::ranges::find (result.outputs, input) != result.outputs.end ())
        fail ("input/output partition overlap at " + input);
    return result;
  }

  const char* path_name (orientation value) {
    switch (value) {
      case orientation::real: return "real";
      case orientation::unreal_formula: return "unreal-formula";
      case orientation::unreal_automaton: return "unreal-automaton";
      case orientation::direct: return "direct";
    }
    return "unknown";
  }

  bool solve (spot::twa_graph_ptr aut, const options& arguments, const bdd& all_inputs,
              const bdd& all_outputs) {
    const bool is_real =
        arguments.worker == orientation::real or arguments.worker == orientation::direct;
    if (arguments.worker == orientation::unreal_automaton) {
      aut = utils::push_aps (aut, all_inputs, all_outputs);
      if (not aut)
        return false;
      if (aut->num_states () > 0 and not aut->prop_state_acc ().is_true ())
        aut = spot::sbacc (aut);
      acacia::diagnostics::snapshot ("after-input-push");
    }
    if (aut->num_states () == 0)
      return is_real;

    auto fast = acacia::spot_fastpath::try_spot_nba_fast_path (aut, all_inputs, all_outputs, false,
                                                               is_real, arguments.spot_fast);
    if (fast.conclusive)
      return fast.current_output_player_wins;
    acacia::diagnostics::snapshot ("after-spot-fast");

    {
#if ACACIA_ENABLE_DIAGNOSTICS
      auto* diag = acacia::diagnostics::current ();
      if (diag) {
        diag->preprocessor = "hoa-replay:" ACACIA_HOA_STRINGIFY (AUT_PREPROCESSOR);
        diag->preproc_states_before = aut->num_states ();
        diag->preproc_edges_before = aut->num_edges ();
      }
      acacia::diagnostics::scoped_timer timer (diag ? &diag->preproc_ms : nullptr);
#endif
      AUT_PREPROCESSOR::make (aut, all_inputs, all_outputs, arguments.kmax) ();
#if ACACIA_ENABLE_DIAGNOSTICS
      if (diag) {
        diag->preproc_states_after = aut->num_states ();
        diag->preproc_edges_after = aut->num_edges ();
      }
#endif
    }
    acacia::diagnostics::snapshot ("after-preprocessing");
    if (aut->num_states () == 0)
      return false;
    posets::vectors::bool_threshold = (BOOLEAN_STATES::make (aut, arguments.kmax)) ();
#if ACACIA_ENABLE_DIAGNOSTICS
    if (auto* diag = acacia::diagnostics::current ())
      diag->bool_threshold = posets::vectors::bool_threshold;
#endif
    acacia::diagnostics::snapshot ("before-solve");
    return solve_game (aut, arguments.kmax, arguments.kmin, arguments.kinc, all_inputs,
                       all_outputs, false, {})
        .has_value ();
  }

}  // namespace

int main (int argc, char** argv) {
  try {
    options arguments = parse_options (argc, argv);
    acacia::diagnostics::scoped_child diagnostics {path_name (arguments.worker)};
#if ACACIA_ENABLE_DIAGNOSTICS
    if (auto* diag = acacia::diagnostics::current ()) {
      diag->instance = arguments.hoa;
      diag->source_format = "hoa-replay";
    }
#endif
    auto dictionary = spot::make_bdd_dict ();
    auto parsed = spot::parse_aut (arguments.hoa, dictionary);
    if (parsed->format_errors (std::cerr) or not parsed->aut)
      return EXIT_CODE_ERROR;
    auto aut = parsed->aut;
    if (not aut->acc ().is_buchi () or aut->acc ().num_sets () != 1)
      fail ("HOA must use one-set Buchi acceptance");
    if (not aut->prop_state_acc ().is_true ())
      fail ("HOA must have state-based acceptance");

    if (arguments.worker == orientation::unreal_formula or
        arguments.worker == orientation::unreal_automaton)
      std::swap (arguments.inputs, arguments.outputs);
    int owner = 0;
    bdd all_inputs = bddtrue;
    bdd all_outputs = bddtrue;
    for (const auto& name : arguments.inputs) {
      const int variable = dictionary->register_proposition (spot::formula::ap (name), &owner);
      all_inputs &= bdd_ithvar (variable);
    }
    for (const auto& name : arguments.outputs) {
      const int variable = dictionary->register_proposition (spot::formula::ap (name), &owner);
      all_outputs &= bdd_ithvar (variable);
    }

#if ACACIA_ENABLE_DIAGNOSTICS
    if (auto* diag = acacia::diagnostics::current ()) {
      diag->aut_states = aut->num_states ();
      diag->aut_edges = aut->num_edges ();
    }
#endif
    const bool winning = solve (std::move (aut), arguments, all_inputs, all_outputs);
    acacia::diagnostics::finish (winning,
                                 winning ? "hoa-replay-winning" : "hoa-replay-inconclusive");
    all_inputs = bddtrue;
    all_outputs = bddtrue;
    dictionary->unregister_all_my_variables (&owner);
    if (not winning) {
      std::cout << "UNKNOWN\n";
      return EXIT_CODE_UNKNOWN;
    }
    if (arguments.worker == orientation::unreal_formula or
        arguments.worker == orientation::unreal_automaton) {
      std::cout << "UNREALIZABLE\n";
      return EXIT_CODE_UNREAL;
    }
    if (arguments.worker == orientation::real) {
      std::cout << "REALIZABLE\n";
      return EXIT_CODE_REAL;
    }
    std::cout << "WINNING\n";
    return EXIT_CODE_REAL;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return EXIT_CODE_ERROR;
  }
}
