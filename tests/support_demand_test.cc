#include "actioners/standard.hh"
#include "boolean_states/forward_saturation.hh"
#include "input_pickers/critical.hh"
#include "ios_precomputers/standard.hh"
#include "solver/forward_k_bounded_safety_aut.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "tiny_game_oracle.hh"
#include "utils/verbose.hh"

#include <array>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using namespace acacia::testing;
  using namespace acacia::solver_detail;
  namespace diag = acacia::diagnostics;
  int failures = 0;

  void expect (const char* message, bool condition) {
    if (not condition) {
      std::cerr << "FAIL: " << message << '\n';
      ++failures;
    }
  }

  spot::twa_graph_ptr graph (bool losing = false) {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->new_states (4);
    aut->set_init_state (0);
    aut->set_buchi ();
    aut->prop_state_acc (true);
    aut->new_edge (0, 1, bddtrue);
    aut->new_edge (0, 1, bddtrue); // Complete source row has two edges.
    aut->new_edge (1, losing ? 1 : 2, bddtrue, {0});
    aut->new_edge (2, 2, bddtrue);
    aut->new_edge (3, 3, bddtrue); // Unreachable Boolean coordinate.
    return aut;
  }

  state rank (std::initializer_list<int> values) {
    posets::utils::vector_mm<VECTOR_ELT_T> vector (values.size (), -1);
    size_t q = 0;
    for (const auto value : values)
      vector[q++] = static_cast<VECTOR_ELT_T> (value);
    return state (vector);
  }

  // Actual wrapper verdicts and published decision counters, plus eager and
  // lazy fixed-K results. Timing and demand fields are deliberately excluded.
  using signature = std::array<unsigned long long, 48>;

  signature solve_cases () {
    signature result {};
    size_t next = 0;
    posets::vectors::bool_threshold = 3;
    for (bool losing : {false, true}) {
      const auto aut = graph (losing);
      const ios_precomputers::standard precomputer;
      const actioners::standard<state> maker;
      const input_pickers::critical picker;
      for (bool forward : {false, true}) {
        diag::scoped_child child {forward ? "gate-forward" : "gate-backward"};
        bool solved;
        if (forward) {
          forward_k_bounded_safety_aut_detail<SetOfStates, decltype(precomputer),
              decltype(maker), decltype(picker)> solver {
              aut, 1, 2, 1, bddtrue, bddtrue, precomputer, maker, picker};
          solved = solver.solve ().has_value ();
        } else {
          k_bounded_safety_aut_detail<SetOfStates, decltype(precomputer),
              decltype(maker), decltype(picker)> solver {
              aut, 1, 2, 1, bddtrue, bddtrue, precomputer, maker, picker};
          solved = solver.solve ().has_value ();
        }
        expect ("wrapper verdict", solved != losing);
        const auto* m = diag::current ();
        result[next++] = solved;
        result[next++] = m->forward_env_nodes;
        result[next++] = m->forward_ctrl_nodes;
        result[next++] = m->actions_seen;
        result[next++] = m->max_f;
        result[next++] = m->k_attempts;
        if (diag::support_demand_enabled ()) {
          expect ("live solver requested source rows", m->support_demand.search_rows > 0);
          if (forward and solved)
            expect ("live verifier has separate demand", m->support_demand.verification_rows > 0);
        } else {
          expect ("disabled collection allocates no histogram",
                  m->support_demand.support_histogram.empty ());
        }
      }

      auto inputs = precomputer.make (aut, bddtrue, bddtrue) ();
      auto actioner = maker.make (aut, inputs, 2);
      const auto initial = rank ({0, -1, -1, -1});
      const auto safe = rank ({1, 1, 1, 0});
      diag::scoped_child child {"gate-eager-lazy"};
      diag::set_support_graph (aut);
      diag::set_support_actions (actioner.actions ());
      const auto lazy = solve_forward_reachable_safety<SetOfStates, false> (
          initial, safe, actioner.actions (), actioner);
      const auto lazy_applications = diag::current ()->support_demand.search_applications;
      const auto eager = solve_forward_reachable_safety<SetOfStates, true> (
          initial, safe, actioner.actions (), actioner);
      if (diag::support_demand_enabled ()) {
        expect ("every lazy application recorded", lazy_applications == lazy.raw_actions);
        expect ("every eager application recorded",
                diag::current ()->support_demand.search_applications - lazy_applications == eager.raw_actions);
      }
      for (const auto* run : {&lazy, &eager}) {
        result[next++] = static_cast<unsigned> (run->status);
        result[next++] = run->env_nodes;
        result[next++] = run->ctrl_nodes;
        result[next++] = run->raw_actions;
        result[next++] = run->choice_switches;
        result[next++] = run->strategy_ranks.size ();
      }
    }
    return result;
  }

  signature run_gate_child (bool enabled) {
    int fds[2];
    if (pipe (fds) != 0)
      std::exit (1);
    const auto pid = fork ();
    if (pid < 0)
      std::exit (1);
    if (pid == 0) {
      close (fds[0]);
      setenv ("ACACIA_DIAG", "1", 1);
      setenv ("ACACIA_DIAG_SUPPORT_DEMAND", enabled ? "1" : "0", 1);
      const auto result = solve_cases ();
      const auto written = write (fds[1], result.data (), sizeof result);
      close (fds[1]);
      _exit (failures == 0 and written == sizeof result ? 0 : 1);
    }
    close (fds[1]);
    signature result {};
    const auto received = read (fds[0], result.data (), sizeof result);
    close (fds[0]);
    int status = 0;
    waitpid (pid, &status, 0);
    expect ("gate child completed all cases", WIFEXITED (status) and WEXITSTATUS (status) == 0);
    expect ("gate child returned all counters", received == sizeof result);
    return result;
  }

  void check_metrics () {
    setenv ("ACACIA_DIAG", "1", 1);
    setenv ("ACACIA_DIAG_SUPPORT_DEMAND", "1", 1);
    posets::vectors::bool_threshold = 3;
    diag::scoped_child child {"metrics"};
    diag::set_support_graph (graph ());
    diag::set_support_k (1);
    input_classes actions {{0, {acacia::research::action_vec (4)}}};
    diag::set_support_actions (actions);
    const auto& action = actions.front ().second.front ();
    auto* m = diag::current ();
    auto& d = m->support_demand;
    diag::observe_support_demand (rank ({0, -1, -1, -1}), action);
    expect ("source row, not destination or dense scan", d.rows[0] == 5 and d.rows[1] == 0
            and d.union_rows == 1 and d.union_edges == 2);
    diag::observe_support_demand (rank ({-1, 0, -1, -1}), action, true);
    expect ("verification-only row", d.search_rows == 1 and d.verification_rows == 1
            and d.verification_only_rows == 1 and d.union_edges == 3);
    const auto before = *m;
    {
      diag::scoped_attempt attempt;
      diag::set_support_k (2);
      diag::observe_support_demand (rank ({0, 0, 0, 0}), action);
    }
    expect ("rollback restores unions, K, histogram and IDs",
            d.rows == before.support_demand.rows and d.k == 1
            and d.support_histogram == before.support_demand.support_histogram
            and d.actions_used == before.support_demand.actions_used
            and d.union_edges == 3 and d.search_applications == 1);
    {
      diag::scoped_attempt attempt;
      diag::set_support_k (2);
      diag::observe_support_demand (rank ({-1, 0, 0, -1}), action);
      attempt.commit ();
    }
    expect ("commit and K bump retain end-to-end union",
            d.k == 2 and d.k_union_rows == 2 and d.k_union_edges == 2
            and d.union_rows == 3 and d.union_edges == 4 and d.verification_only_rows == 0);
    diag::observe_support_demand (rank ({-1, -1, -1, -1}), action);
    expect ("exact quantiles include empty support", d.median () == 1
            and d.percentile (95) == 2 and d.support_max == 2);
    for (int i = 0; i < 1000; ++i)
      diag::observe_support_demand (rank ({-1, -1, -1, 0}), action);
    expect ("Boolean active support and bounded histogram", d.support_histogram.size () == 5
            and d.union_rows == 4 and d.union_edges == 5 and d.percentile (95) == 1);
    expect ("stable action ID", d.used_ids () == "0");
    diag::support_demand_metrics even;
    even.support_histogram = {1, 0, 0, 1};
    even.search_applications = 2;
    expect ("even median averages middle samples", even.median () == 1.5);
  }

  void check_renumbered_graph () {
    diag::scoped_child child {"renumbered-graph"};
    const auto aut = graph (true);
    posets::vectors::bool_threshold = boolean_states::forward_saturation::make (aut, 2) ();
    expect ("fixture renumbers initial source", aut->get_init_state_number () == 1);
    diag::set_support_graph (aut);
    const acacia::research::action_vec action (4);
    diag::observe_support_demand (rank ({-1, 0, -1, -1}), action);
    expect ("row degree follows final rank numbering",
            diag::current ()->support_demand.union_edges == 2);
  }
}

int main () {
  // Fork before either cached env accessor is initialized. This exercises the
  // production off/on gates in separate processes, not a test-only override.
  const auto baseline = run_gate_child (false);
  const auto measured = run_gate_child (true);
  expect ("C0/C1 verdicts and all decision counters identical", baseline == measured);
  check_metrics ();
  check_renumbered_graph ();
  return failures == 0 ? 0 : 1;
}
