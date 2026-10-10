#include "solver/unreal_safety_core_witnesses.hh"

#include "acacia_build_config.hh"
#include "record_transport_controls.hh"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/tl/print.hh>
#include <spot/tl/simplify.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>
#include <string>

namespace {

  // Keep test actions and their contracts active in release builds.
  void check (bool condition) {
    assert (condition);
    if (!condition)
      std::abort ();
  }

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

  void allowance_tests () {
    using namespace acacia::unreal_witnesses;
    constexpr uint64_t second = 1000000000;
    auto b = make_budget (second, 0, {});
    check (b.until == 2 * second && b.per_attempt == second / 4);
    b = make_budget (second, 21 * second, {});
    check (b.until == 5 * second && b.per_attempt == second);
    const allowances research {2000, 8000};
    b = make_budget (second, 0, research);
    check (b.until == 9 * second && b.per_attempt == 2 * second);
    b = make_budget (second, 21 * second, research);
    check (b.until == 5 * second && b.per_attempt == second);
    b = make_budget (second, 101 * second, research);
    check (b.until == 9 * second && b.per_attempt == 2 * second);
    b = make_budget (second, second - 1, research);
    check (b.until == second && b.per_attempt == 0);
    b = make_budget (second, 0, {0, 0});
    check (b.until == second && b.per_attempt == 0);
    b = make_budget (second, 0, {2000, std::nullopt});
    check (b.until == 2 * second && b.per_attempt == 2 * second);
    b = make_budget (second, 0, {std::nullopt, 8000});
    check (b.until == 9 * second && b.per_attempt == second / 4);
    const auto maximum = std::numeric_limits<uint64_t>::max ();
    b = make_budget (maximum - 1, 0, {maximum, maximum});
    check (b.until == maximum && b.per_attempt <= maximum);
  }

  void extended_contract_tests () {
    using namespace acacia::unreal_witnesses;
    const auto parse = [] (const std::string& text) {
      auto f = spot::parse_infix_psl (text);
      check (f.f && f.errors.empty ());
      return f.f;
    };
    const auto bind = [] (spot::formula f) {
      return source_binding {
          spot::str_psl (f),          "ltl", "-", "Mealy", "Mealy", {}, {"a", "unused_input"},
          {"b", "c", "unused_output"}};
    };
    const auto exact = [] (spot::formula f, bool moore) {
      spot::synthesis_info info;
      info.moore = moore;
      info.s = spot::synthesis_info::algo::DPA_SPLIT;
      return spot::solve_game (spot::ltl_to_game (f, {"b", "c", "unused_output"}, info));
    };
    spot::tl_simplifier simplify;
    const auto a = parse ("GF a");
    const auto first = parse ("G(a -> F b)"), second = parse ("G(a -> G !b)");
    for (bool moore : {false, true}) {
      check (exact (spot::formula::Implies (a, first), moore));
      check (exact (spot::formula::Implies (a, second), moore));
      check (!exact (spot::formula::Implies (a, spot::formula::And ({first, second})), moore));
      const auto safety_first = parse ("G(a <-> X b)");
      const auto safety_second = parse ("G(!a <-> X b)");
      check (exact (spot::formula::Implies (a, safety_first), moore));
      check (exact (spot::formula::Implies (a, safety_second), moore));
      check (!exact (
          spot::formula::Implies (a, spot::formula::And ({safety_first, safety_second})), moore));
    }
    for (const auto& text :
         {"GF a -> (G(a -> F b) & G(a -> G !b) & GF c)",
          "GF a -> ((a -> b) & G(a -> G !b) & GF c)",
          "(GF a & GF !a) -> (G(a <-> X b) & G(!a <-> X b) & GF c)",
          "(GF a & GF !a) -> (G(a -> F b) & FG !b & GF c)",
          "(G a & G !a) -> (GF b & FG !b & GF c)",
          "a -> (b & (GF a -> (G(a -> F b) & FG !b & GF c)))",
          "GF a -> G((a -> F b) & (a -> G !b) & F c)", "GF a -> G(a -> (F b & G !b & F c))",
          "G(a -> (X b & X !b & F c))", "GF a -> G(a -> G(b & c & X !b))",
          "GF a -> GF(b & c & X !b)", "GF a -> F(G((a -> b) & (a -> !b) & c))",
          "GF a -> F(b & c & X !b)", "G(a -> F(b & c))"}) {
      auto original = parse (text);
      auto source = bind (original);
      auto p = make_plan (original, source);
      check (std::string (p.reason) == "eligible" && p.guarantees.size () >= 2);
      auto groups = dependency_groups (p);
      check (groups && groups->size () <= group_limit);
      std::vector<std::vector<size_t>> subsets = *groups;
      for (size_t i = 0; i < p.guarantees.size (); ++i)
        subsets.push_back ({i});
      for (auto kept : subsets) {
        derivation d {p, kept, derive (p, kept)};
        check (verify_derivation (d, original, source));
        for (bool moore : {false, true})
          if (!exact (d.objective, moore))
            check (!exact (original, moore));
        check (simplify.implication (original, d.objective));
        // Exact language emptiness is independent of the structural replay.
        check (spot::translator {}
                    .run (spot::formula::And ({original, spot::formula::Not (d.objective)}))
                    ->is_empty ());
        checked_game receipt;
        receipt.objective = binding_hash (spot::str_psl (d.objective));
        receipt.runner_objective = receipt.objective;
        receipt.context = source.hash ();
        receipt.game = 1;
        receipt.proof = 2;
        receipt.verified = true;
        receipt.seal = receipt.binding ();
        check (accept (d, original, source, receipt));
        auto changed = d;
        changed.objective = spot::formula::tt ();
        check (!accept (changed, original, source, receipt));
        changed = d;
        changed.kept.push_back (changed.kept.back ());
        check (!accept (changed, original, source, receipt));
        changed = d;
        changed.premise.guarantees[0] = parse ("X a");
        check (!accept (changed, original, source, receipt));
        changed = d;
        changed.premise.frames[0].child ^= 1;
        check (!accept (changed, original, source, receipt));
        changed = d;
        changed.premise.frames[0].parent = parse ("GF !a -> GF b");
        check (!accept (changed, original, source, receipt));
        auto context = source;
        context.inputs.push_back ("changed_partition");
        check (!accept (d, original, context, receipt));
        context = source;
        context.semantics = "Moore";
        check (!accept (d, original, context, receipt));
        context = source;
        context.source_sha256 = "mutated_source_digest";
        check (!accept (d, original, context, receipt));
        context = source;
        context.effective_target = "Moore";
        check (!accept (d, original, context, receipt));
        context = source;
        context.source += " & a";
        check (!accept (d, original, context, receipt));
        auto damaged = receipt;
        damaged.proof ^= 1;
        check (!accept (d, original, source, damaged));
        damaged = receipt;
        damaged.game ^= 1;
        check (!accept (d, original, source, damaged));
        damaged = receipt;
        damaged.runner_objective ^= 1;
        check (!accept (d, original, source, damaged));
        damaged = receipt;
        damaged.verified = false;
        check (!accept (d, original, source, damaged));
      }
    }
    for (const auto& text : {"true", "false", "GF a -> true", "GF a -> GF b"}) {
      const auto f = parse (text);
      check (make_plan (f, bind (f)).guarantees.empty ());
    }
    for (const auto& text : {"!G(a -> (b & c))", "(b & c) | a", "G(a -> X(b & c))",
                             "G(a -> (b U (b & c)))", "F(!(b & c))", "F((b & c) | a)",
                             "G(a -> !(b & c))", "G((a & X a) -> F b)", "(b & c) <-> a"}) {
      const auto f = parse (text);
      check (std::string (make_plan (f, bind (f)).reason) == "no_guarantee_conjunction");
    }
    // Generated controls exercise the proof rule independently of replay.
    for (bool eventually : {false, true})
      for (const auto& antecedent : {"a", "GF a", "a & X a"})
        for (const auto& guarantees : {"F b & G !b & F c", "X b & X !b & F c", "b & c & X !b"}) {
          const auto consequent = spot::formula::Implies (parse (antecedent), parse (guarantees));
          const auto f = spot::formula::Implies (
              a, eventually ? spot::formula::F (consequent) : spot::formula::G (consequent));
          const auto source = bind (f);
          const auto p = make_plan (f, source);
          check (std::string (p.reason) == "eligible" && p.guarantees.size () == 3);
          for (const auto& kept :
               std::vector<std::vector<size_t>> {{0}, {1}, {2}, {0, 1}, {1, 2}}) {
            const derivation d {p, kept, derive (p, kept)};
            check (verify_derivation (d, f, source));
            check (spot::translator {}
                        .run (spot::formula::And ({f, spot::formula::Not (d.objective)}))
                        ->is_empty ());
          }
        }
    const auto shared_future = parse ("F(b & c & X !b)");
    const auto future_source = bind (shared_future);
    const auto future_plan = make_plan (shared_future, future_source);
    check (std::string (future_plan.reason) == "eligible");
    derivation future {future_plan, {0, 1}, derive (future_plan, {0, 1})};
    check (future.objective.is (spot::op::F) && future.objective[0].is (spot::op::And));
    check (verify_derivation (future, shared_future, future_source));
    future.objective = spot::formula::And ({spot::formula::F (future_plan.guarantees[0]),
                                            spot::formula::F (future_plan.guarantees[1])});
    check (!verify_derivation (future, shared_future, future_source));
    // Only consequents are exposed; a conjunction inside an antecedent remains exact.
    const auto scoped = parse ("GF a -> G((a & X a) -> (F b & G !b & F c))");
    auto scoped_source = bind (scoped);
    auto scoped_plan = make_plan (scoped, scoped_source);
    check (scoped_plan.frames.size () == 3 && scoped_plan.guarantees.size () == 3);
    for (const auto& semantics : {"Mealy", "Moore", "Mealy,Strict", "Moore,Strict"}) {
      auto source = scoped_source;
      source.format = "tlsf";
      source.semantics = semantics;
      source.normalization = source.source;
      source.effective_target = semantics;
      const auto p = make_plan (scoped, source);
      const derivation d {p, {0}, derive (p, {0})};
      check (verify_derivation (d, scoped, source));
      check (spot::translator {}
                  .run (spot::formula::And ({scoped, spot::formula::Not (d.objective)}))
                  ->is_empty ());
      auto mutated = d;
      mutated.premise.frames.back ().parent = parse ("!a -> (F b & G !b & F c)");
      check (!verify_derivation (mutated, scoped, source));
    }
    const auto formula = parse ("GF a -> (GF b & FG !b & GF c)");
    auto strict = bind (formula);
    strict.format = "tlsf";
    strict.semantics = "Strict,Mealy";
    check (std::string (make_plan (formula, strict).reason) == "missing_exact_normalization");
    strict.normalization = strict.source;
    check (!make_plan (formula, strict).guarantees.empty ());
    const auto until = parse ("a -> ((b W !a) & (GF a -> (GF b & FG !b & GF c)))");
    strict.source = strict.normalization = spot::str_psl (until);
    check (std::string (make_plan (until, strict).reason) == "strict_weak_until_context");
    const auto twin = parse (
        "GF renamed_input -> (GF renamed_output & FG !renamed_output & "
        "GF renamed_other)");
    const auto p = make_plan (formula, bind (formula));
    const auto renamed = make_plan (twin, bind (twin));
    check (p.guarantees.size () == renamed.guarantees.size ());
    auto left = dependency_groups (p), right = dependency_groups (renamed);
    check (left && right && left->size () == right->size ());
    for (size_t i = 0; i < left->size (); ++i)
      check ((*left)[i].size () == (*right)[i].size ());
    const derivation d {p, {0}, derive (p, {0})};
    auto blocked = [] (spot::formula) -> bool {
      while (true)
        pause ();
    };
    const auto deadline = acacia::phase_clock (CLOCK_MONOTONIC) + 50000000;
    auto cancelled = bounded_attempt (d, blocked, acacia::phase_clock (CLOCK_MONOTONIC) + 5000000);
    check (cancelled.state == attempt_result::cancelled);
    check (acacia::phase_clock (CLOCK_MONOTONIC) < deadline);
    int status;
    const auto reaped = waitpid (-1, &status, WNOHANG);
    const auto reap_error = errno;
    check (reaped == -1 && reap_error == ECHILD);
    auto oversized = [] (spot::formula) -> bool { throw std::length_error ("acceptance limit"); };
    const auto exception = bounded_attempt (d, oversized, deadline);
    check (exception.state == attempt_result::exception);
    auto unverified = [] (spot::formula) { return true; };
    const auto expired = bounded_attempt (d, unverified, acacia::phase_clock (CLOCK_MONOTONIC));
    check (expired.state == attempt_result::cancelled);
    auto result = bounded_attempt (d, unverified, deadline);
    check (result.result && !accept (d, formula, p.source, result.proof));
    // Both failed attempts are reaped before the original fallback starts.
    bool full_started = false;
    auto fallback = [&] (spot::formula f) {
      full_started = true;
      return f == formula;
    };
    const auto invocation = acacia::phase_clock (CLOCK_MONOTONIC) + 200000000;
    records silent;
    const auto bounded = try_extended_witnesses (formula, p.source, true, blocked, silent,
                                               invocation, {2000, 8000});
    check (!bounded);
    check (acacia::phase_clock (CLOCK_MONOTONIC) < invocation);
    const auto full = fallback (formula);
    const auto realizable = exact (formula, false);
    check (full && full_started && !realizable);
    const auto short_entry = acacia::phase_clock (CLOCK_MONOTONIC);
    const auto short_attempt =
        try_extended_witnesses (formula, p.source, true, blocked, silent, 0, {5, 20});
    check (!short_attempt);
    check (acacia::phase_clock (CLOCK_MONOTONIC) - short_entry < 200000000);
    const auto final_reaped = waitpid (-1, &status, WNOHANG);
    const auto final_reap_error = errno;
    check (final_reaped == -1 && final_reap_error == ECHILD);
    const auto final_fallback = fallback (formula);
    check (final_fallback);
  }

}  // namespace

int main (int argc, char** argv) {
  if (argc == 3 && std::string (argv[1]) == "--extended-candidates") {
    using namespace acacia::unreal_witnesses;
    const auto f = spot::parse_infix_psl (argv[2]).f;
    source_binding source {spot::str_psl (f), "ltl", "-", "Mealy", "Mealy", {}, {}, {}};
    auto p = make_plan (f, source);
    std::cout << p.reason << '\n';
    if (std::string (p.reason) != "eligible")
      return 0;
    for (size_t i = 0; i < std::min (singleton_limit, p.guarantees.size ()); ++i)
      std::cout << spot::str_psl (derive (p, {i})) << '\n';
    if (auto groups = dependency_groups (p))
      for (const auto& group : *groups)
        std::cout << spot::str_psl (derive (p, group)) << '\n';
    return 0;
  }
  if (argc == 2) {
    const std::string mode = argv[1];
    if (mode == "--solver-arm") {
      std::cout << "unreal:formula:"
                << (ACACIA_SPOT_GUARDED_BACKEND ? "spot-guarded-sparse" : "backward") << std::endl;
      return 0;
    }
    acacia::record_transport_test_start ();
    auto* record = static_cast<acacia::worker_record*> (
        mmap (nullptr, sizeof (acacia::worker_record), PROT_READ | PROT_WRITE,
              MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (record == MAP_FAILED)
      return 1;
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
      if (child < 0)
        return 1;
      if (child == 0)
        record->pid = getpid ();
      else {
        std::cin.get ();
        kill (child, SIGKILL);
        int status;
        if (waitpid (child, &status, 0) != child || !WIFSIGNALED (status))
          return 1;
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
      const auto prior_drops = record->dropped;
      for (int i = 0; i < 20000; ++i)
        acacia::phase_records_send (line, sizeof line - 1);
      const auto flood_drops = record->dropped - prior_drops;
      if (flood_drops == 0 || flood_drops >= 20000)
        return 1;
      std::cout << "flooded " << 20000 - flood_drops << std::endl;
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
      if (started >= expected.size () || candidate != expected[started])
        throw std::logic_error ("unexpected weakening candidate");
      // Attempt numbering drives the runner outcomes even with NDEBUG.
      ++started;
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
      if (answer.has_value () != (mode == "success"))
        return 1;
      std::cout << (answer ? "proof" : "fallback") << std::endl;
    } catch (const std::runtime_error&) {
      if (mode != "exception" || started != 1)
        return 1;
      std::cout << "exception" << std::endl;
    }
    acacia::worker_terminal (mode == "success" ? 1 : 2);
    acacia::worker_event (*record, "parent_terminal", "exit", mode == "success" ? 1 : 2, 0,
                          record->dropped ? "dropped" : "complete");
    acacia::phase_records_summary ("weakening_test");
    return 0;
  }
  allowance_tests ();
  extended_contract_tests ();
  const auto oversized = make_formula (64, true);
  const auto witnesses = acacia::unreal_witnesses::make_safety_core_witnesses (oversized);
  if (witnesses.size () != 8)
    return 1;
  {
    using namespace acacia::unreal_witnesses;
    source_binding source {spot::str_psl (oversized), "ltl", "-", "Mealy", "Mealy", {}, {}, {}};
    const auto p = make_plan (oversized, source);
    check (p.legacy && p.core.size () == 1 && p.guarantees.size () == 65);
    for (size_t i = 0; i < witnesses.size (); ++i)
      check (derive (p, {i}) == witnesses[i]);
  }
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
  const auto observed_witnesses =
      acacia::unreal_witnesses::make_safety_core_witnesses (oversized, 8, &observed);
  check (observed_witnesses == witnesses);
  check (observed.oversized_globals == 1 && observed.largest_global_conjunction_size == 65 &&
          observed.safety_conjuncts == 1 && observed.obligations == 65);
  const auto absent = spot::parse_infix_psl ("G(a) & GF b & GF c").f;
  const auto below = spot::parse_infix_psl ("G(a & F b) & GF c").f;
  for (const auto& formula : {absent, below, at_limit}) {
    const auto observed_empty =
        acacia::unreal_witnesses::make_safety_core_witnesses (formula, 8, &observed);
    const auto empty = acacia::unreal_witnesses::make_safety_core_witnesses (formula);
    check (observed_empty.empty ());
    check (empty.empty ());
    check (observed.oversized_globals == 0 && observed.safety_conjuncts == 1);
    check (observed.largest_global_conjunction_size == (formula == absent  ? 0
                                                         : formula == below ? 2
                                                                            : 64));
    check (
        std::string (observed.reason) ==
        (formula == absent ? "no_global_conjunction" : "global_conjunction_threshold_not_met"));
    check (observed.obligations == (formula == at_limit ? 64 : 2));
  }
  const auto multiple_globals = spot::formula::And ({oversized, below});
  const auto multiple_witnesses =
      acacia::unreal_witnesses::make_safety_core_witnesses (multiple_globals, 8, &observed);
  check (not multiple_witnesses.empty ());
  check (observed.largest_global_conjunction_size == 65 && observed.oversized_globals == 1);
  acacia::unreal_witnesses::make_safety_core_witnesses (oversized, 0, &observed);
  check (std::string (observed.reason) == "zero_allowance");
  spot::tl_simplifier simplify;
  for (const auto& witness : witnesses)
    check (simplify.implication (oversized, witness));
  return 0;
}
