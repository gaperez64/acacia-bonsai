#include "solver/unreal_safety_core_witnesses.hh"

#include "acacia_build_config.hh"
#include "record_transport_controls.hh"

#include <cassert>
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
    assert (b.until == 2 * second && b.per_attempt == second / 4);
    b = make_budget (second, 21 * second, {});
    assert (b.until == 5 * second && b.per_attempt == second);
    const allowances research {2000, 8000};
    b = make_budget (second, 0, research);
    assert (b.until == 9 * second && b.per_attempt == 2 * second);
    b = make_budget (second, 21 * second, research);
    assert (b.until == 5 * second && b.per_attempt == second);
    b = make_budget (second, 101 * second, research);
    assert (b.until == 9 * second && b.per_attempt == 2 * second);
    b = make_budget (second, second - 1, research);
    assert (b.until == second && b.per_attempt == 0);
    b = make_budget (second, 0, {0, 0});
    assert (b.until == second && b.per_attempt == 0);
    b = make_budget (second, 0, {2000, std::nullopt});
    assert (b.until == 2 * second && b.per_attempt == 2 * second);
    b = make_budget (second, 0, {std::nullopt, 8000});
    assert (b.until == 9 * second && b.per_attempt == second / 4);
    const auto maximum = std::numeric_limits<uint64_t>::max ();
    b = make_budget (maximum - 1, 0, {maximum, maximum});
    assert (b.until == maximum && b.per_attempt <= maximum);
  }

  void extended_contract_tests () {
    using namespace acacia::unreal_witnesses;
    const auto parse = [] (const std::string& text) {
      auto f = spot::parse_infix_psl (text);
      assert (f.f && f.errors.empty ());
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
      assert (exact (spot::formula::Implies (a, first), moore));
      assert (exact (spot::formula::Implies (a, second), moore));
      assert (!exact (spot::formula::Implies (a, spot::formula::And ({first, second})), moore));
      const auto safety_first = parse ("G(a <-> X b)");
      const auto safety_second = parse ("G(!a <-> X b)");
      assert (exact (spot::formula::Implies (a, safety_first), moore));
      assert (exact (spot::formula::Implies (a, safety_second), moore));
      assert (!exact (
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
      assert (std::string (p.reason) == "eligible" && p.guarantees.size () >= 2);
      auto groups = dependency_groups (p);
      assert (groups && groups->size () <= group_limit);
      std::vector<std::vector<size_t>> subsets = *groups;
      for (size_t i = 0; i < p.guarantees.size (); ++i)
        subsets.push_back ({i});
      for (auto kept : subsets) {
        derivation d {p, kept, derive (p, kept)};
        assert (verify_derivation (d, original, source));
        for (bool moore : {false, true})
          if (!exact (d.objective, moore))
            assert (!exact (original, moore));
        assert (simplify.implication (original, d.objective));
        // Exact language emptiness is independent of the structural replay.
        assert (spot::translator {}
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
        assert (accept (d, original, source, receipt));
        auto changed = d;
        changed.objective = spot::formula::tt ();
        assert (!accept (changed, original, source, receipt));
        changed = d;
        changed.kept.push_back (changed.kept.back ());
        assert (!accept (changed, original, source, receipt));
        changed = d;
        changed.premise.guarantees[0] = parse ("X a");
        assert (!accept (changed, original, source, receipt));
        changed = d;
        changed.premise.frames[0].child ^= 1;
        assert (!accept (changed, original, source, receipt));
        changed = d;
        changed.premise.frames[0].parent = parse ("GF !a -> GF b");
        assert (!accept (changed, original, source, receipt));
        auto context = source;
        context.inputs.push_back ("changed_partition");
        assert (!accept (d, original, context, receipt));
        context = source;
        context.semantics = "Moore";
        assert (!accept (d, original, context, receipt));
        context = source;
        context.source_sha256 = "mutated_source_digest";
        assert (!accept (d, original, context, receipt));
        context = source;
        context.effective_target = "Moore";
        assert (!accept (d, original, context, receipt));
        context = source;
        context.source += " & a";
        assert (!accept (d, original, context, receipt));
        auto damaged = receipt;
        damaged.proof ^= 1;
        assert (!accept (d, original, source, damaged));
        damaged = receipt;
        damaged.game ^= 1;
        assert (!accept (d, original, source, damaged));
        damaged = receipt;
        damaged.runner_objective ^= 1;
        assert (!accept (d, original, source, damaged));
        damaged = receipt;
        damaged.verified = false;
        assert (!accept (d, original, source, damaged));
      }
    }
    for (const auto& text : {"true", "false", "GF a -> true", "GF a -> GF b"}) {
      const auto f = parse (text);
      assert (make_plan (f, bind (f)).guarantees.empty ());
    }
    for (const auto& text : {"!G(a -> (b & c))", "(b & c) | a", "G(a -> X(b & c))",
                             "G(a -> (b U (b & c)))", "F(!(b & c))", "F((b & c) | a)",
                             "G(a -> !(b & c))", "G((a & X a) -> F b)", "(b & c) <-> a"}) {
      const auto f = parse (text);
      assert (std::string (make_plan (f, bind (f)).reason) == "no_guarantee_conjunction");
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
          assert (std::string (p.reason) == "eligible" && p.guarantees.size () == 3);
          for (const auto& kept :
               std::vector<std::vector<size_t>> {{0}, {1}, {2}, {0, 1}, {1, 2}}) {
            const derivation d {p, kept, derive (p, kept)};
            assert (verify_derivation (d, f, source));
            assert (spot::translator {}
                        .run (spot::formula::And ({f, spot::formula::Not (d.objective)}))
                        ->is_empty ());
          }
        }
    const auto shared_future = parse ("F(b & c & X !b)");
    const auto future_source = bind (shared_future);
    const auto future_plan = make_plan (shared_future, future_source);
    assert (std::string (future_plan.reason) == "eligible");
    derivation future {future_plan, {0, 1}, derive (future_plan, {0, 1})};
    assert (future.objective.is (spot::op::F) && future.objective[0].is (spot::op::And));
    assert (verify_derivation (future, shared_future, future_source));
    future.objective = spot::formula::And ({spot::formula::F (future_plan.guarantees[0]),
                                            spot::formula::F (future_plan.guarantees[1])});
    assert (!verify_derivation (future, shared_future, future_source));
    // Only consequents are exposed; a conjunction inside an antecedent remains exact.
    const auto scoped = parse ("GF a -> G((a & X a) -> (F b & G !b & F c))");
    auto scoped_source = bind (scoped);
    auto scoped_plan = make_plan (scoped, scoped_source);
    assert (scoped_plan.frames.size () == 3 && scoped_plan.guarantees.size () == 3);
    for (const auto& semantics : {"Mealy", "Moore", "Mealy,Strict", "Moore,Strict"}) {
      auto source = scoped_source;
      source.format = "tlsf";
      source.semantics = semantics;
      source.normalization = source.source;
      source.effective_target = semantics;
      const auto p = make_plan (scoped, source);
      const derivation d {p, {0}, derive (p, {0})};
      assert (verify_derivation (d, scoped, source));
      assert (spot::translator {}
                  .run (spot::formula::And ({scoped, spot::formula::Not (d.objective)}))
                  ->is_empty ());
      auto mutated = d;
      mutated.premise.frames.back ().parent = parse ("!a -> (F b & G !b & F c)");
      assert (!verify_derivation (mutated, scoped, source));
    }
    const auto formula = parse ("GF a -> (GF b & FG !b & GF c)");
    auto strict = bind (formula);
    strict.format = "tlsf";
    strict.semantics = "Strict,Mealy";
    assert (std::string (make_plan (formula, strict).reason) == "missing_exact_normalization");
    strict.normalization = strict.source;
    assert (!make_plan (formula, strict).guarantees.empty ());
    const auto until = parse ("a -> ((b W !a) & (GF a -> (GF b & FG !b & GF c)))");
    strict.source = strict.normalization = spot::str_psl (until);
    assert (std::string (make_plan (until, strict).reason) == "strict_weak_until_context");
    const auto twin = parse (
        "GF renamed_input -> (GF renamed_output & FG !renamed_output & "
        "GF renamed_other)");
    const auto p = make_plan (formula, bind (formula));
    const auto renamed = make_plan (twin, bind (twin));
    assert (p.guarantees.size () == renamed.guarantees.size ());
    auto left = dependency_groups (p), right = dependency_groups (renamed);
    assert (left && right && left->size () == right->size ());
    for (size_t i = 0; i < left->size (); ++i)
      assert ((*left)[i].size () == (*right)[i].size ());
    const derivation d {p, {0}, derive (p, {0})};
    auto blocked = [] (spot::formula) -> bool {
      while (true)
        pause ();
    };
    const auto deadline = acacia::phase_clock (CLOCK_MONOTONIC) + 50000000;
    auto cancelled = bounded_attempt (d, blocked, acacia::phase_clock (CLOCK_MONOTONIC) + 5000000);
    assert (cancelled.state == attempt_result::cancelled);
    assert (acacia::phase_clock (CLOCK_MONOTONIC) < deadline);
    int status;
    assert (waitpid (-1, &status, WNOHANG) == -1 && errno == ECHILD);
    auto oversized = [] (spot::formula) -> bool { throw std::length_error ("acceptance limit"); };
    assert (bounded_attempt (d, oversized, deadline).state == attempt_result::exception);
    auto unverified = [] (spot::formula) { return true; };
    assert (bounded_attempt (d, unverified, acacia::phase_clock (CLOCK_MONOTONIC)).state ==
            attempt_result::cancelled);
    auto result = bounded_attempt (d, unverified, deadline);
    assert (result.result && !accept (d, formula, p.source, result.proof));
    // Both failed attempts are reaped before the original fallback starts.
    bool full_started = false;
    auto fallback = [&] (spot::formula f) {
      full_started = true;
      return f == formula;
    };
    const auto invocation = acacia::phase_clock (CLOCK_MONOTONIC) + 200000000;
    records silent;
    assert (!try_extended_witnesses (formula, p.source, true, blocked, silent, invocation,
                                     {2000, 8000}));
    assert (acacia::phase_clock (CLOCK_MONOTONIC) < invocation);
    assert (fallback (formula) && full_started && !exact (formula, false));
    const auto short_entry = acacia::phase_clock (CLOCK_MONOTONIC);
    assert (!try_extended_witnesses (formula, p.source, true, blocked, silent, 0, {5, 20}));
    assert (acacia::phase_clock (CLOCK_MONOTONIC) - short_entry < 200000000);
    assert (waitpid (-1, &status, WNOHANG) == -1 && errno == ECHILD);
    assert (fallback (formula));
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
    assert (p.legacy && p.core.size () == 1 && p.guarantees.size () == 65);
    for (size_t i = 0; i < witnesses.size (); ++i)
      assert (derive (p, {i}) == witnesses[i]);
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
