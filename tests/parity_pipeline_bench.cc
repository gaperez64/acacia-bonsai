// Research driver: compare two dispatchers on copies of one completed DPA.
// Forking from the same parent also prevents the first solve warming the second's BDD cache.
#include "solver/parity/route.hh"
#include "solver/parity/tlsf_dpa.hh"
#include "solver/spot_worker_record.hh"
#include "tlsf_frontend.hh"
#include "utils/verbose.hh"

#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <signal.h>
#include <spot/tl/parse.hh>
#include <stdexcept>
#include <sys/wait.h>
#include <tlsf/ast_api.h>
#include <unordered_map>
#include <unistd.h>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
namespace dpa = acacia::tlsf_dpa;
namespace route = acacia::parity_route;
using acacia::spot_records::quote;

namespace {
  struct stamp {
      uint64_t wall = acacia::phase_clock (CLOCK_MONOTONIC);
      uint64_t cpu = acacia::phase_clock (CLOCK_PROCESS_CPUTIME_ID);
  };

  void completed (const std::string& stage, stamp start, stamp end = {}) {
    rusage usage {};
    getrusage (RUSAGE_SELF, &usage);
    std::cout << "{\"event\":\"phase_end\",\"stage\":" << quote (stage)
              << ",\"wall_ns\":" << end.wall - start.wall
              << ",\"cpu_ns\":" << end.cpu - start.cpu
              << ",\"process_peak_rss_kb\":" << usage.ru_maxrss;
#ifdef ACACIA_HAVE_MALLINFO2
    const auto heap = mallinfo2 ();
    std::cout << ",\"glibc_live_bytes\":" << heap.uordblks + heap.hblkhd;
#endif
    std::cout << "}\n" << std::flush;
  }

  template <typename Action> auto phase (const std::string& name, Action action) {
    std::cout << "{\"event\":\"phase_begin\",\"stage\":" << quote (name) << "}\n"
              << std::flush;
    const stamp start;
    auto result = action ();
    completed (name, start);
    return result;
  }

  void features (spot::formula formula, size_t inputs, size_t outputs) {
    const stamp cached_start;
    const std::array flags {formula.is_syntactic_safety (), formula.is_syntactic_guarantee (),
                            formula.is_syntactic_obligation (), formula.is_syntactic_recurrence (),
                            formula.is_syntactic_persistence (), formula.is_delta2 ()};
    const stamp cached_end;
    std::cout << "{\"event\":\"cached_features\",\"inputs\":" << inputs
              << ",\"outputs\":" << outputs
              << ",\"safety\":" << flags[0] << ",\"guarantee\":" << flags[1]
              << ",\"obligation\":" << flags[2] << ",\"recurrence\":" << flags[3]
              << ",\"persistence\":" << flags[4] << ",\"delta2\":" << flags[5] << "}\n";
    completed ("cached_features", cached_start, cached_end);

    const stamp structural_start;
    struct depth { size_t nodes, temporal; };
    std::unordered_map<spot::formula, depth> seen;
    std::map<std::string, size_t> operators;
    std::vector<std::pair<spot::formula, bool>> pending {{formula, false}};
    size_t edges = 0;
    while (!pending.empty ()) {
      const auto [f, ready] = pending.back ();
      pending.pop_back ();
      if (seen.contains (f))
        continue;
      if (!ready) {
        pending.emplace_back (f, true);
        for (auto child : f)
          if (!seen.contains (child))
            pending.emplace_back (child, false);
        continue;
      }
      depth value {0, 0};
      for (auto child : f) {
        value.nodes = std::max (value.nodes, seen.at (child).nodes);
        value.temporal = std::max (value.temporal, seen.at (child).temporal);
      }
      ++value.nodes;
      const auto kind = f.kind ();
      value.temporal += kind == spot::op::X || kind == spot::op::F || kind == spot::op::G ||
                        kind == spot::op::U || kind == spot::op::R || kind == spot::op::W ||
                        kind == spot::op::M;
      seen.emplace (f, value);
      ++operators[f.kindstr ()];
      edges += f.size ();
    }
    const stamp structural_end;
    std::cout << "{\"event\":\"structural_features\",\"dag_nodes\":" << seen.size ()
              << ",\"dag_edges\":" << edges << ",\"depth\":" << seen.at (formula).nodes
              << ",\"temporal_depth\":" << seen.at (formula).temporal << ",\"operators\":{";
    const char* separator = "";
    for (const auto& [name, count] : operators) {
      std::cout << separator << quote (name) << ':' << count;
      separator = ",";
    }
    std::cout << "}}\n";
    completed ("structural_features", structural_start, structural_end);
  }

  int solve_copy (const route::request& original, bool classify, bool verify, unsigned seconds) {
    std::cout.flush ();
    const stamp start;
    const pid_t child = fork ();
    if (child < 0)
      throw std::runtime_error ("fork failed");
    const std::string mode = classify ? "classified" : "positive";
    if (!child) {
      alarm (seconds);
      try {
        std::cout << "{\"event\":\"route_begin\",\"mode\":" << quote (mode) << "}\n"
                  << std::flush;
        route::request input = original;
        input.automaton = phase (mode + ":copy", [&] {
          return spot::make_twa_graph (original.automaton, spot::twa::prop_set::all ());
        });
        route::options options;
        options.synthesis = options.verify = verify;
        std::map<std::string, stamp> starts;
        auto observe = [&] (const char* name, const auto&, double elapsed) {
          const auto stage = mode + ":" + name;
          if (elapsed < 0) {
            starts[stage] = stamp {};
            std::cout << "{\"event\":\"phase_begin\",\"stage\":" << quote (stage) << "}\n"
                      << std::flush;
          }
          else
            completed (stage, starts.at (stage));
        };
        const auto answer = phase (mode + ":dispatch", [&] {
          return classify ? route::run_classified_positive_dpa (input, options, observe)
                          : route::run_positive_dpa (input, options, observe);
        });
        std::cout << "{\"event\":\"route_result\",\"mode\":" << quote (mode)
                  << ",\"verdict\":" << quote (answer.verdict) << ",\"tier\":" << answer.tier
                  << ",\"checked\":" << answer.checked << "}\n" << std::flush;
        if (verify && !answer.checked)
          _exit (2);
        _exit (answer.verdict == "REALIZABLE" ? 0 : answer.verdict == "UNREALIZABLE" ? 1 : 2);
      } catch (const std::exception& error) {
        std::cout << "{\"event\":\"route_error\",\"mode\":" << quote (mode)
                  << ",\"error\":" << quote (error.what ()) << "}\n" << std::flush;
        _exit (2);
      }
    }
    int status;
    rusage usage {};
    while (wait4 (child, &status, 0, &usage) < 0)
      if (errno != EINTR)
        throw std::runtime_error ("wait4 failed");
    const int code = WIFEXITED (status) ? WEXITSTATUS (status) : 128 + WTERMSIG (status);
    const uint64_t cpu_us = uint64_t (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000ULL +
                            usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
    std::cout << "{\"event\":\"route_exit\",\"mode\":" << quote (mode)
              << ",\"exit\":" << code << ",\"wait_status\":" << status
              << ",\"exit_code\":" << (WIFEXITED (status) ? std::to_string (WEXITSTATUS (status))
                                                         : "null")
              << ",\"signal\":" << (WIFSIGNALED (status) ? WTERMSIG (status) : 0)
              << ",\"timeout_censored\":" << (WIFSIGNALED (status) && WTERMSIG (status) == SIGALRM)
              << ",\"wall_ns\":" << acacia::phase_clock (CLOCK_MONOTONIC) - start.wall
              << ",\"cpu_us\":" << cpu_us
              << ",\"process_peak_rss_kb\":" << usage.ru_maxrss
              << "}\n" << std::flush;
    return code;
  }
}

int main (int argc, char** argv) {
  try {
    if (argc < 2)
      throw std::runtime_error (
          "usage: parity-pipeline-bench FILE.tlsf [--features] [--reverse] [--verify] [--seconds N]");
    bool features_only = false, reverse = false, verify = false;
    unsigned seconds = 17;
    for (int i = 2; i < argc; ++i) {
      const std::string flag = argv[i];
      if (flag == "--features")
        features_only = true;
      else if (flag == "--reverse")
        reverse = true;
      else if (flag == "--verify")
        verify = true;
      else if (flag == "--seconds" && i + 1 < argc) {
        size_t end;
        const std::string value = argv[++i];
        const auto parsed = std::stoul (value, &end);
        if (end != value.size () || parsed < 1 || parsed > 3600)
          throw std::runtime_error ("seconds must be an integer from 1 to 3600");
        seconds = unsigned (parsed);
      }
      else
        throw std::runtime_error ("unrecognized argument: " + flag);
    }
    const auto spec = phase ("frontend", [&] { return acacia::tlsf_frontend::load (argv[1]); });
    const auto parsed = phase ("spot_parse", [&] { return spot::parse_infix_psl (spec.formula); });
    if (!parsed.f || !parsed.errors.empty ())
      throw std::runtime_error ("invalid effective-Mealy objective");
    features (parsed.f, spec.inputs.size (), spec.outputs.size ());
    if (features_only)
      return 0;

    std::cout << "{\"event\":\"regime\",\"construction_allowance_s\":" << seconds
              << ",\"each_dispatcher_allowance_s\":" << seconds
              << ",\"constructor_share_bytes\":536870912,"
                 "\"total_pipeline_timing\":false,\"verification\":" << verify << "}\n";

    // Same allowance as the eight-worker portfolio, independent of this driver's process count.
    constexpr uint64_t share = 512ULL << 20;
    dpa::limits budget;
    budget.construction.max_host_bytes = budget.import_bytes = share - share / 8;
    budget.construction.bdd_nodes = share / 128;
    budget.construction.bdd_cache = budget.construction.bdd_nodes / 4;
    budget.construction.deadline_mono_ns =
        acacia::phase_clock (CLOCK_MONOTONIC) + uint64_t (seconds) * 1000000000ULL;
    TlsfDpaStatus status;
    auto context = phase ("create", [&] {
      auto result = dpa::create (tlsf_ast_root (spec.ast.get ()), spec.inputs, spec.outputs,
                                 budget, status);
      if (!result && status.code == TLSF_DPA_DECLINED && status.reason == TLSF_DPA_UNSUPPORTED) {
        budget.construction.allow_unproved = true;
        result = dpa::create (tlsf_ast_root (spec.ast.get ()), spec.inputs, spec.outputs,
                              budget, status);
      }
      return result;
    });
    if (!context || status.code != TLSF_DPA_OK) {
      std::cout << "{\"event\":\"construction_stop\",\"reason\":"
                << quote (dpa::reason (status)) << "}\n";
      return 2;
    }
    dpa::import_metrics metrics;
    auto imported = phase ("expansion_and_import", [&] {
      return dpa::import (*context, budget, spot::make_bdd_dict (), {}, &metrics);
    });
    std::cout << "{\"event\":\"import_components\",\"setup_s\":" << metrics.setup_seconds
              << ",\"first_successor_s\":" << metrics.first_successor_seconds
              << ",\"remaining_successor_s\":" << metrics.remaining_successor_seconds
              << ",\"bridge_s\":" << metrics.bridge_seconds << "}\n";
    context.reset ();
    if (!imported.automaton || imported.status.code != TLSF_DPA_OK) {
      std::cout << "{\"event\":\"import_stop\",\"reason\":"
                << quote (dpa::reason (imported.status)) << "}\n";
      return 2;
    }
    // OxiDD retires its GC thread asynchronously. C++ work after fork is only
    // safe once the parent is single threaded; never infer that from free alone.
    phase ("oxidd_thread_retirement", [&] {
      const auto until = acacia::phase_clock (CLOCK_MONOTONIC) + 5000000000ULL;
      for (;;) {
        const std::filesystem::directory_iterator tasks ("/proc/self/task");
        if (std::distance (tasks, std::filesystem::directory_iterator {}) == 1)
          return true;
        if (acacia::phase_clock (CLOCK_MONOTONIC) >= until)
          throw std::runtime_error ("OxiDD threads did not retire before dispatcher comparison");
        usleep (1000);
      }
    });
    std::cout << "{\"event\":\"shared_graph\",\"states\":" << imported.automaton->num_states ()
              << ",\"edges\":" << imported.automaton->num_edges ()
              << ",\"labels\":" << imported.labels << ",\"import_bytes\":" << imported.import_bytes
              << ",\"allow_unproved\":" << budget.construction.allow_unproved << "}\n";
    route::request request;
    request.formula = request.specification = parsed.f;
    request.automaton = imported.automaton;
    request.inputs = spec.inputs;
    request.outputs = spec.outputs;
    request.frame = route::timing_frame::effective_mealy;
    const int first = solve_copy (request, reverse, verify, seconds);
    const int second = solve_copy (request, !reverse, verify, seconds);
    if (first <= 1 && second <= 1 && first != second)
      throw std::runtime_error ("opposite verdicts from the same completed DPA");
    return first <= 1 && second <= 1 ? 0 : 2;
  } catch (const std::exception& error) {
    std::cout << "{\"event\":\"error\",\"error\":" << quote (error.what ()) << "}\n";
    return 3;
  }
}
