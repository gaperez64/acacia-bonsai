// Exercise the release recording policy with real searches, without timing a
// benchmark. The counters exist only in this translation unit's test binary.
#include "solver/forward_k_bounded_safety_aut.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "solver/spot_lazy_worker.hh"
#include "actioners/standard.hh"
#include "input_pickers/critical.hh"
#include <posets/downsets/vector_backed.hh>
#include <spot/tl/parse.hh>

namespace {
  bool count_allocations = false;
  size_t allocations = 0;
  size_t steady_clock_reads = 0;
}
// Interpose the actual clock in this test executable, independently of the
// recording helper. Direct calls and calls through clock aliases must count too.
// Deterministic ticks keep elapsed-time-based K scheduling reproducible.
std::chrono::steady_clock::time_point std::chrono::steady_clock::now () noexcept {
  static duration::rep ticks = 0;
  ++steady_clock_reads;
  return time_point {duration {++ticks}};
}
void* operator new (size_t size) {
  if (count_allocations) ++allocations;
  if (void* p = std::malloc (size ? size : 1)) return p;
  throw std::bad_alloc ();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 1;
}
namespace {
  namespace records = acacia::spot_records;
  using State = posets::vectors::vector_backed<VECTOR_ELT_T>;
  using Downset = posets::downsets::vector_backed<State>;
  struct NoPrecomputation {
      auto make (const spot::twa_graph_ptr&, bdd, bdd) const { return [] { return 0; }; }
  };
  struct Actions {
      bool losing;
      auto make (const spot::twa_graph_ptr& aut, int, VECTOR_ELT_T k) const {
        std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>> empty;
        auto result = actioners::standard<State>::make (aut, empty, k);
        // One real action: stay put, incrementing exactly in the losing game.
        result.actions ().push_back ({bddtrue, {{{{0, losing}}}}});
        return result;
      }
  };
  void check (bool ok, const char* message) {
    if (!ok) throw std::runtime_error (message);
  }
  void reset () {
    records::record_calls = records::timer_reads = records::report_values = 0;
    steady_clock_reads = 0;
  }

  template <class Run>
  void compare (const std::filesystem::path& root, const char* name,
                size_t win_clocks, size_t loss_clocks, Run run) {
    for (bool losing : {false, true}) {
      unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
      reset ();
      {
        records::Record disabled;
        check (!disabled && !records::active, "unexpected sink");
        check (run (losing) == !losing, "incorrect no-sink verdict");
      }
      check (records::record_calls == 0, "no-sink path entered recording code");
      check (records::timer_reads == 0, "no-sink path read a recording clock");
      check (records::report_values == 0, "no-sink path serialized a report value");
      const auto expected_clocks = losing ? loss_clocks : win_clocks;
      if (steady_clock_reads != expected_clocks) {
        std::cerr << name << (losing ? " loss" : " win") << ": expected " << expected_clocks
                  << " pre-P0 clock reads, got " << steady_clock_reads << '\n';
        check (false, "no-sink path changed the pre-P0 clock budget");
      }
      const auto directory = root / (std::string (name) + (losing ? "-loss" : "-win"));
      setenv ("ACACIA_SPOT_CAPTURE_DIR", directory.c_str (), 1);
      reset ();
      {
        records::Record enabled;
        check (run (losing) == !losing, "recording changed verdict");
      }
      check (records::record_calls > 0 && records::timer_reads > 0,
             "active sink did not exercise the recording probes");
      check (steady_clock_reads > expected_clocks, "active sink did not exercise the clock probe");
      if (std::string_view (name) == "sparse-lazy")
        check (records::report_values > 0, "sparse reporter was not exercised");
      std::ifstream input (std::filesystem::directory_iterator (directory)->path ());
      const std::string json {std::istreambuf_iterator<char> (input), {}};
      check (json.find (losing ? "\"cumulative_attempts_ended\":\"3.000000\""
                               : "\"cumulative_attempts_ended\":\"1.000000\"") != std::string::npos,
             "did not traverse expected K attempts");
    }
    std::cout << "PASS " << name << ": win/loss clocks = " << win_clocks << '/' << loss_clocks
              << " (pre-P0 only); 0 record calls/clocks/values without sink; sink active\n";
  }
}
int main () {
  static_assert (!ACACIA_ENABLE_DIAGNOSTICS);
  reset ();
  (void) std::chrono::steady_clock::now ();
  check (steady_clock_reads == 1 && records::timer_reads == 0, "raw clock probe is inactive");
  (void) records::now ();
  check (steady_clock_reads == 2 && records::timer_reads == 1, "record clock bypassed raw probe");
  std::cout << "PASS raw and recording clock probes\n";
  unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
  unsetenv ("ACACIA_SPOT_CAPTURE_HISTORY");
  count_allocations = true;
  {
    records::Record disabled;
    acacia::diagnostics::finish (false, "a reason that exceeds the small string buffer");
  }
  count_allocations = false;
  check (allocations == 0, "disabled worker lifecycle allocated");
  std::cout << "PASS disabled worker construction/finish: 0 heap allocations\n";
  const auto root = std::filesystem::temp_directory_path () /
                    ("acacia-record-cost-" + std::to_string (getpid ()));
  std::filesystem::create_directories (root);
  const auto dict = spot::make_bdd_dict ();
  const auto graph = spot::make_twa_graph (dict);
  graph->new_state ();
  graph->set_init_state (0);
  graph->set_buchi ();
  graph->new_edge (0, 0, bddtrue);
  const NoPrecomputation precomputer;
  const input_pickers::critical picker;
  // Pre-P0 forward timers: total start/end (2), one attempt start per K,
  // verification start/end on a win (2), and loss evidence at each K raise (2).
  // Eager mode additionally times each minimised action (1 win; 1+2+3 loss).
  constexpr size_t eager_clocks = ACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS ? 2 : 0;
  compare (root, "forward", 5 + eager_clocks, 7 + 6 * eager_clocks, [&] (bool losing) {
    const Actions actions {losing};
    acacia::solver_detail::forward_k_bounded_safety_aut_detail<
        Downset, NoPrecomputation, Actions, input_pickers::critical> solver {
        graph, 1, 3, 1, bddtrue, bddtrue, precomputer, actions, picker};
    return solver.solve ().has_value ();
  });
  // Pre-P0 backward timers: one bound start per K, plus loss evidence per loss.
  compare (root, "backward", 1, 6, [&] (bool losing) {
    const Actions actions {losing};
    k_bounded_safety_aut_detail<Downset, NoPrecomputation, Actions, input_pickers::critical> solver {
        graph, 1, 3, 1, bddtrue, bddtrue, precomputer, actions, picker};
    return solver.solve ().has_value ();
  });
  // Pre-P0 lazy timers: worker/factory start/end (4), plus search/verification
  // start/end per K (4). Row generation and loss accounting get no allowance.
  compare (root, "sparse-lazy", 8, 16, [&] (bool losing) {
    const auto formula = spot::parse_infix_psl (losing ? "1" : "G a").f;
    const int a = dict->register_proposition (spot::formula::ap ("a"), &dict);
    const auto outcome = acacia::spot_lazy_worker::solve (
        formula, dict, bddtrue, bdd_ithvar (a), 1, 3, 1);
    dict->unregister_all_my_variables (&dict);
    check (outcome != acacia::spot_lazy_worker::Outcome::unknown, "lazy search declined");
    return outcome == acacia::spot_lazy_worker::Outcome::win;
  });
  unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
  std::filesystem::remove_all (root);
}
