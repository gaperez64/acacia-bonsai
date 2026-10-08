#include "actioners/standard.hh"
#include "ios_precomputers/mona.hh"
#include "solver/certificate_verifier.hh"
#include "solver/spot_lazy_game.hh"
#include "utils/push_aps.hh"

#include <iostream>
#include <spot/twa/bdddict.hh>

namespace posets::vectors {
  size_t bool_threshold = 0;
}

static size_t pid_calls = 0, clock_calls = 0, usage_calls = 0, exception_calls = 0;
extern "C" pid_t __real_getpid ();
extern "C" int __real_clock_gettime (clockid_t, timespec*);
extern "C" int __real_getrusage (__rusage_who_t, rusage*);
extern "C" int __real__ZSt19uncaught_exceptionsv ();
extern "C" pid_t __wrap_getpid () {
  ++pid_calls;
  return __real_getpid ();
}
extern "C" int __wrap_clock_gettime (clockid_t clock, timespec* value) {
  ++clock_calls;
  return __real_clock_gettime (clock, value);
}
extern "C" int __wrap_getrusage (__rusage_who_t who, rusage* value) {
  ++usage_calls;
  return __real_getrusage (who, value);
}
extern "C" int __wrap__ZSt19uncaught_exceptionsv () {
  ++exception_calls;
  return __real__ZSt19uncaught_exceptionsv ();
}

struct OneStateSet {
    using value_type = int;
    std::vector<int> values {0};
    bool contains (int state) const { return state == 0; }
    auto begin () const { return values.begin (); }
    auto end () const { return values.end (); }
};
struct ReplayActioner {
    int apply (int state, int, actioners::direction) { return state; }
};

int main () {
  unsetenv ("ACACIA_PHASE_RECORDS");
  auto dict = spot::make_bdd_dict ();
  auto graph = spot::make_twa_graph (dict);
  graph->new_state ();
  graph->set_init_state (0);
  graph->set_buchi ();
  graph->prop_state_acc (true);
  graph->new_edge (0, 0, bddtrue);
  using State = posets::utils::vector_mm<VECTOR_ELT_T>;
  using Inputs = std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>>;
  Inputs inputs;
  for (size_t i = 0; i < 1000; ++i)
    inputs.push_back ({bddtrue, {{{0, 0}}}});
  pid_calls = clock_calls = usage_calls = exception_calls = 0;
  acacia::phase_records_init ();
  auto actioner = actioners::standard<State>::make (graph, inputs, 2);
  {
    acacia::legacy_phase scope ("search");
    acacia::legacy_count ("queries", 1000);
    acacia::legacy_metric ("status", "complete");
    acacia::legacy_graph (graph);
    acacia::legacy_bdd_gc gc;
    gc.publish ();
  }
  auto decoded = ios_precomputers::mona::make (graph, bddtrue, bddtrue);
  const auto ios = decoded ();
  auto pushed = utils::push_aps (graph, bddtrue, bddtrue);
  OneStateSet candidate;
  ReplayActioner replay;
  std::vector<std::pair<int, std::vector<int>>> replay_actions {{0, {1}}};
  const bool verified = acacia::solver_detail::verify_winning_certificate (candidate, candidate, 0,
                                                                           replay_actions, replay);
  std::cout << "getpid=" << pid_calls << " clock=" << clock_calls << " getrusage=" << usage_calls
            << " exceptions=" << exception_calls << '\n';
  if (pid_calls || clock_calls || usage_calls || exception_calls || !pushed || !verified ||
      actioner.actions ().size () != 1 || ios.empty ())
    return 1;

  namespace lazy = acacia::spot_lazy_game;
  using Rank = lazy::Rank;
  acacia::spot_letters::WorkerAlphabet alphabet {bddtrue, bddtrue, bddtrue, {}};
  lazy::RowStore store {acacia::spot_rows::FrozenAcacia {graph, 0}, {}, {}};
  lazy::Reader reader {store, false, {}};
  lazy::Oracle oracle {reader, store, alphabet, 2};
  const Rank rank {{{0, 0}}, 2};
  for (size_t i = 0; i < 1000; ++i)
    if (!oracle.eq (rank, rank).value || !oracle.down (rank, rank).value)
      return 1;
  lazy::Search search {store, alphabet, 2};
  const auto result = search.solve ();
  if (result.status != acacia::solver_detail::forward_result_status::win_k)
    return 1;
  std::map<std::string, std::string> metrics;
  lazy::Reporter report;
  report.sink = [&] (const auto& key, const auto& value) { metrics[key] = value; };
  oracle.report (report, "search_");
  for (const auto* key : {"threshold_calls", "threshold_misses", "threshold_shortcuts",
                          "preimage_calls", "preimage_misses"})
    if (metrics[std::string ("search_") + key] != "0")
      return 1;
  // The solver's ordinary elapsed-time clocks remain; observation adds no PID,
  // resource or exception samples during oracle/search/independent replay.
  return pid_calls || usage_calls || exception_calls;
}
