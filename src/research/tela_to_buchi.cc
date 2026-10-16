// Uninstalled, language-preserving acceptance conversion for saved worker HOAs.
#include "research/exact_buchi.hh"
#include "solver/spot_worker_record.hh"
#include "version.hh"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <spot/parseaut/public.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/isdet.hh>
#include <string>

int main (int argc, char** argv) {
  try {
    std::string input, output, metrics, stage = "all";
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help") {
        std::cout << "Usage: acacia-tela-to-buchi --hoa FILE --output FILE --metrics FILE\n"
                     "  --stage all|route|generalized|state (default all)\n";
        return 0;
      }
      if (arg == "--version") {
        std::cout << "acacia-tela-to-buchi " << acacia_version () << '\n';
        return 0;
      }
      if (++i >= argc)
        throw std::runtime_error (arg + " requires an argument");
      if (arg == "--hoa")
        input = argv[i];
      else if (arg == "--output")
        output = argv[i];
      else if (arg == "--metrics")
        metrics = argv[i];
      else if (arg == "--stage")
        stage = argv[i];
      else
        throw std::runtime_error ("unknown option " + arg);
    }
    if (input.empty () or output.empty () or metrics.empty ())
      throw std::runtime_error ("--hoa, --output, and --metrics are required");
    if (stage != "all" and stage != "route" and stage != "generalized" and stage != "state")
      throw std::runtime_error ("--stage must be all, route, generalized, or state");
    const auto input_path = std::filesystem::weakly_canonical (input);
    const auto output_path = std::filesystem::weakly_canonical (output);
    const auto metrics_path = std::filesystem::weakly_canonical (metrics);
    if (input_path == output_path or input_path == metrics_path or output_path == metrics_path)
      throw std::runtime_error ("input, output, and metrics paths must differ");
    std::ofstream report (metrics);
    if (not report)
      throw std::runtime_error ("cannot open metrics file");
    bool determinism_preserved = true;
    auto observe = [&] (const char* name, const auto& aut, double seconds, unsigned tier = 0,
                        bool available = true) {
      aut->prop_universal (spot::trival::maybe ());
      const bool deterministic = spot::is_deterministic (aut);
      determinism_preserved &= deterministic;
      std::ostringstream acceptance;
      acceptance << aut->get_acceptance ();
      report << std::setprecision (17) << "{\"stage\":" << acacia::spot_records::quote (name)
             << ",\"event\":\"" << (seconds < 0 ? "begin" : "complete") << "\",\"seconds\":";
      if (seconds < 0)
        report << "null";
      else
        report << seconds;
      report << ",\"tier\":";
      if (tier)
        report << tier;
      else
        report << "null";
      report << ",\"available\":" << (available ? "true" : "false")
             << ",\"determinism_preserved\":" << (determinism_preserved ? "true" : "false")
             << ",\"states\":" << aut->num_states () << ",\"edges\":" << aut->num_edges ()
             << ",\"acceptance_sets\":" << aut->num_sets ()
             << ",\"acceptance\":" << acacia::spot_records::quote (acceptance.str ())
             << ",\"deterministic\":" << (deterministic ? "true" : "false")
             << ",\"state_acceptance\":" << (aut->prop_state_acc ().is_true () ? "true" : "false")
             << "}\n";
      report.flush ();
      if (not report)
        throw std::runtime_error ("cannot write metrics file");
    };
    auto dict = spot::make_bdd_dict ();
    const auto start = std::chrono::steady_clock::now ();
    auto parsed = spot::parse_aut (input, dict);
    if (parsed->format_errors (std::cerr) or not parsed->aut)
      return 2;
    auto aut = std::move (parsed->aut);
    parsed.reset ();
    observe ("parse", aut,
             std::chrono::duration<double> (std::chrono::steady_clock::now () - start).count ());
    if (stage != "state" and not spot::is_deterministic (aut))
      throw std::runtime_error ("input TELA must be deterministic");
    auto routed = acacia::research::route_buchi (
        std::move (aut), stage == "route" or stage == "generalized", stage == "state", observe);
    aut = std::move (routed.aut);
    observe ("write", aut, -1, routed.tier);
    const auto writing = std::chrono::steady_clock::now ();
    std::ofstream hoa (output);
    // Some structural conversions retain a completeness hint from dead states.
    aut->prop_complete (spot::trival::maybe ());
    spot::print_hoa (hoa, aut);
    hoa.close ();
    if (not hoa)
      throw std::runtime_error ("cannot write output HOA");
    observe ("write", aut,
             std::chrono::duration<double> (std::chrono::steady_clock::now () - writing).count (),
             routed.tier);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "acacia-tela-to-buchi: " << error.what () << '\n';
    return 2;
  }
}
