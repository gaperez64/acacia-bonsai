#include "acacia_build_config.hh"
#include "solver/unreal_safety_core_witnesses.hh"

#include "record_transport_controls.hh"

#include <cassert>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/tl/print.hh>
#include <spot/tl/simplify.hh>
#include <stdexcept>
#include <string>

namespace {

  spot::formula make_formula (size_t obligations, bool include_safety) {
    std::string nested = include_safety ? "(!g_0 | !g_1)" : "(r_s -> Fg_0)";
    for (size_t i = 0; i < obligations; ++i)
      nested += " & (r_" + std::to_string (i) + " -> Fg_0)";
    auto parsed = spot::parse_infix_psl ("G(" + nested + ") & G(r_extra -> Fg_1)");
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      return {};
    }
    return parsed.f;
  }

}  // namespace

int main (int argc, char** argv) {
  if (argc == 2) {
    const std::string mode = argv[1];
    if (mode == "--solver-arm") {
      std::cout << "unreal:formula:"
                << (ACACIA_SPOT_GUARDED_BACKEND ? "spot-guarded-sparse" : "backward")
                << std::endl;
      return 0;
    }
    acacia::record_transport_test_start ();
    auto* record = static_cast<acacia::worker_record*> (
        mmap (nullptr, sizeof (acacia::worker_record), PROT_READ | PROT_WRITE,
              MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    assert (record != MAP_FAILED);
    *record = {};
    record->pid = getpid ();
    acacia::active_worker_record () = record;
    acacia::worker_record_text (record->requested_backend, "spot-guarded-sparse");
    acacia::worker_record_text (record->effective_backend, "spot-guarded-sparse");
    acacia::worker_record_text (record->original_polarity, "UNREAL");
    acacia::worker_record_text (record->proof_polarity, "REAL");
    acacia::worker_record_text (record->route, "unreal_formula");
    pid_t child = -1;
    if (mode == "kill") {
      child = fork ();
      assert (child >= 0);
      if (child == 0)
        record->pid = getpid ();
      else {
        std::cin.get ();
        kill (child, SIGKILL);
        int status;
        assert (waitpid (child, &status, 0) == child && WIFSIGNALED (status));
        acacia::weakening_cancelled (*record, "interrupted");
        acacia::worker_event (*record, "parent_terminal", "interrupted", -1, SIGKILL,
                              "incomplete");
        acacia::phase_records_summary ("weakening_test");
        return 0;
      }
    }
    acacia::worker_event (*record, "worker_start");
    if (mode == "flood") {
      constexpr char line[] = "{\"event\":\"flood\"}\n";
      for (int i = 0; i < 20000; ++i)
        acacia::phase_records_send (line, sizeof line - 1);
      std::cout << "flooded" << std::endl;
      std::cin.get ();
    }
    auto formula = make_formula (64, true);
    if (mode == "not_top_level_and")
      formula = spot::formula::G (spot::formula::ap ("p"));
    if (mode == "no_global_conjunction")
      formula = spot::parse_infix_psl ("G(a) & GF b & GF c").f;
    if (mode == "global_conjunction_threshold_not_met")
      formula = make_formula (63, true);
    if (mode == "absent_safety_conjuncts")
      formula = make_formula (65, false);
    if (mode == "no_non_safety_obligations") {
      std::vector<spot::formula> atoms;
      for (int i = 0; i < 65; ++i)
        atoms.push_back (spot::formula::ap ("a" + std::to_string (i)));
      formula = spot::formula::And (
          {spot::formula::G (spot::formula::And (atoms)), spot::formula::ap ("b")});
    }
    acacia::unreal_witnesses::records telemetry;
    telemetry.bind_source (spot::str_psl (formula), {"r", "unused_input"}, {"g", "unused_output"});
    telemetry.enter (formula, acacia::phase_clock (CLOCK_MONOTONIC) + 10000000000ULL, {}, "ltl",
                     "-", "-", "Mealy", false, 2, 2);
    size_t started = 0;
    const auto expected = acacia::unreal_witnesses::make_safety_core_witnesses (formula);
    auto runner = [&] (const spot::formula& candidate) {
      assert (candidate == expected[started++]);
      telemetry.objective (candidate, "runner_objective");
      if (mode == "kill") {
        std::cout << "candidate" << std::endl;
        while (true)
          pause ();
      }
      if (mode == "exception")
        throw std::runtime_error ("candidate exception");
      if (mode == "cancellation")
        acacia::worker_stopped ("cancelled");
      if (mode == "decline" || (mode == "mixed" && started == 2))
        acacia::worker_decline ("not_applicable");
      if (mode == "mixed" && started == 1)
        acacia::worker_stopped ("resource");
      return mode == "success";
    };
    try {
      const auto answer = acacia::unreal_witnesses::try_safety_core_witnesses (
          formula, mode != "not_unreal_worker", mode == "synthesis_requested", runner, telemetry);
      if (!answer) {
        telemetry.fallback (formula);
        telemetry.objective (formula, "runner_objective");
      }
      assert (answer.has_value () == (mode == "success"));
      std::cout << (answer ? "proof" : "fallback") << std::endl;
    } catch (const std::runtime_error&) {
      assert (mode == "exception" && started == 1);
      std::cout << "exception" << std::endl;
    }
    acacia::worker_terminal (mode == "success" ? 1 : 2);
    acacia::worker_event (*record, "parent_terminal", "exit", mode == "success" ? 1 : 2, 0,
                          record->dropped ? "dropped" : "complete");
    acacia::phase_records_summary ("weakening_test");
    return 0;
  }
  const auto oversized = make_formula (64, true);
  const auto witnesses = acacia::unreal_witnesses::make_safety_core_witnesses (oversized);
  if (witnesses.size () != 8)
    return 1;
  for (const auto& witness : witnesses)
    if (not witness.is (spot::op::And) or witness.size () != 2)
      return 2;

  const auto at_limit = make_formula (63, true);
  if (not acacia::unreal_witnesses::make_safety_core_witnesses (at_limit).empty ())
    return 3;

  const auto no_safety = make_formula (65, false);
  if (not acacia::unreal_witnesses::make_safety_core_witnesses (no_safety).empty ())
    return 4;

  if (not acacia::unreal_witnesses::make_safety_core_witnesses (oversized, 0).empty ())
    return 5;
  acacia::unreal_witnesses::census observed;
  assert (acacia::unreal_witnesses::make_safety_core_witnesses (oversized, 8, &observed) ==
          witnesses);
  assert (observed.oversized_globals == 1 && observed.largest_global_conjunction_size == 65 &&
          observed.safety_conjuncts == 1 && observed.obligations == 65);
  const auto absent = spot::parse_infix_psl ("G(a) & GF b & GF c").f;
  const auto below = spot::parse_infix_psl ("G(a & F b) & GF c").f;
  for (const auto& formula : {absent, below, at_limit}) {
    assert (acacia::unreal_witnesses::make_safety_core_witnesses (formula, 8, &observed).empty ());
    assert (acacia::unreal_witnesses::make_safety_core_witnesses (formula).empty ());
    assert (observed.oversized_globals == 0 && observed.safety_conjuncts == 1);
    assert (observed.largest_global_conjunction_size == (formula == absent  ? 0
                                                         : formula == below ? 2
                                                                            : 64));
    assert (
        std::string (observed.reason) ==
        (formula == absent ? "no_global_conjunction" : "global_conjunction_threshold_not_met"));
    assert (observed.obligations == (formula == at_limit ? 64 : 2));
  }
  const auto multiple_globals = spot::formula::And ({oversized, below});
  assert (not acacia::unreal_witnesses::make_safety_core_witnesses (multiple_globals, 8, &observed)
                  .empty ());
  assert (observed.largest_global_conjunction_size == 65 && observed.oversized_globals == 1);
  acacia::unreal_witnesses::make_safety_core_witnesses (oversized, 0, &observed);
  assert (std::string (observed.reason) == "zero_allowance");
  spot::tl_simplifier simplify;
  for (const auto& witness : witnesses)
    assert (simplify.implication (oversized, witness));
  return 0;
}
