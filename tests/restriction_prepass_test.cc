#include "solver/restriction_prepass.hh"

#include "error_msg.hh"
#include "phase_records.hh"
#include "solver/formula_input.hh"
#include "solver/syntactic_bypass.hh"
#include "tlsf_frontend.hh"

#include <iostream>
#include <stdexcept>

namespace {
  void check (bool condition) {
    if (!condition)
      throw std::runtime_error ("restriction prepass check failed");
  }
}

int main () {
  unsetenv ("ACACIA_PHASE_RECORDS");
  using namespace acacia::restriction;
  using acacia::formula_input::parse;
  const auto p = spot::formula::ap ("p"), q = spot::formula::ap ("q");
  const std::vector<std::string> inputs {"p", "q"}, outputs {"o"};
  check (substitute (p, {{"p", q}, {"q", spot::formula::ff ()}}, inputs, outputs, true) == q);
  check (substitute (parse ("G(p -> X q)"), {{"p", q}, {"q", p}}, inputs, outputs, true) ==
         parse ("G(q -> X p)"));
  check (!substitute (p, {{"p", spot::formula::ap ("o")}}, inputs, outputs, true));
  check (!substitute (p, {{"o", p}}, inputs, outputs, false));
  check (!substitute (p, {{"missing", p}}, inputs, outputs, true));
  check (!substitute (parse ("missing"), {}, inputs, outputs, true));
  check (!substitute (p, {}, inputs, {"p"}, true));
  check (!substitute (p, {{"p", parse ("X q")}}, inputs, outputs, true));
  check (substitute (parse ("p & o"), {{"o", spot::formula::tt ()}}, inputs, outputs, false) == p);

  // All six sections, including assumptions/guards, share one simultaneous substitution.
  for (const auto* semantics : {"Mealy", "Mealy,Strict"}) {
    const auto source = std::string {"INFO { TITLE: \"sections\" SEMANTICS: "} + semantics +
                        " TARGET: Mealy } MAIN { INPUTS { a[3]; } OUTPUTS { b[3]; }"
                        " INITIALLY { a[0]; } PRESET { b[0]; } REQUIRE { a[1]; }"
                        " ASSUME { a[2]; } ASSERT { b[1]; } GUARANTEE { b[2]; } }";
    const auto spec = acacia::tlsf_frontend::parse (source);
    check (spec.metadata.tlsf_indexed_families.empty ());
    check (spec.metadata.tlsf_declared_families.size () == 2);
    const auto items = candidates (spec.metadata, spec.outputs, true);
    check (items.size () == 1 && items[0].values.size () == 3);
    check (candidates (spec.metadata, spec.outputs, false).empty ());
    const auto rewritten =
        substitute (parse (spec.formula), items[0].values, spec.inputs, spec.outputs, true);
    const auto expected = std::string (semantics) == "Mealy"
                              ? "a_0 -> (b_0 & ((G a_0 & a_0) -> (G b_1 & b_2)))"
                              : "a_0 -> (b_0 & (b_1 W !a_0) & ((G a_0 & a_0) -> b_2))";
    check (rewritten == parse (expected));
  }

  specification_metadata metadata;
  for (unsigned i = 0; i < 10; ++i) {
    symmetry::indexed_family_hint family;
    family.is_input = true;
    family.members = {"p", "q"};
    metadata.tlsf_declared_families.push_back (family);
  }
  check (candidates (metadata, outputs, true).size () == 3);
  check (candidates (metadata, outputs, false).size () == 1);
  check (accepts (true, EXIT_CODE_UNREAL) && !accepts (true, EXIT_CODE_REAL));
  check (accepts (false, EXIT_CODE_REAL) && !accepts (false, EXIT_CODE_UNREAL));
  check (!accepts (true, EXIT_CODE_UNKNOWN) && !accepts (false, EXIT_CODE_ERROR));
  metadata.tlsf_declared_families.resize (1);
  // Lean counterexamples: a realizable input diagonal and an unrealizable output constant.
  check (!try_prepass (parse ("p <-> q"), metadata, inputs, outputs, true, 0, [] (auto f) {
    check (f == spot::formula::tt ());
    return EXIT_CODE_REAL;
  }));
  check (!try_prepass (parse ("o"), metadata, inputs, outputs, false, 0, [] (auto f) {
    check (f == spot::formula::ff ());
    return EXIT_CODE_UNREAL;
  }));
  check (try_prepass (parse ("p"), metadata, inputs, outputs, true, 0,
                      [] (auto) { return EXIT_CODE_UNREAL; }));
  check (try_prepass (parse ("!o"), metadata, inputs, outputs, false, 0, [] (auto) {
#ifndef __SANITIZE_ADDRESS__
    rlimit limit {};
    check (getrlimit (RLIMIT_AS, &limit) == 0 && limit.rlim_cur <= rlim_t {512} * 1024 * 1024);
#endif
    return EXIT_CODE_REAL;
  }));
  auto two_families = metadata;
  two_families.tlsf_declared_families.push_back (metadata.tlsf_declared_families.front ());
  two_families.tlsf_declared_families.back ().members = {"u", "v"};
  check (try_prepass (
      parse ("q ^ v"), two_families, {"p", "q", "u", "v"}, outputs, true, 0,
      [] (auto f) { return f == parse ("q ^ u") ? EXIT_CODE_UNREAL : EXIT_CODE_UNKNOWN; }));
  auto mixed = metadata;
  mixed.tlsf_declared_families.front ().is_input = false;
  mixed.tlsf_declared_families.front ().members = {"x", "y"};
  check (try_prepass (parse ("!o"), mixed, inputs, {"o", "x", "y"}, false, 0, [] (auto f) {
    return f == spot::formula::tt () ? EXIT_CODE_REAL : EXIT_CODE_ERROR;
  }));
  check (!try_prepass (parse ("x"), mixed, inputs, {"x", "y"}, false, 0,
                       [] (auto) { return EXIT_CODE_REAL; }));
  check (!try_prepass (p, metadata, inputs, outputs, true, 1,
                       [] (auto) { return EXIT_CODE_UNREAL; }));
  check (!try_prepass (p, metadata, inputs, outputs, true, 0,
                       [] (auto) -> int { throw std::bad_alloc (); }));
  check (!try_prepass (p, metadata, inputs, outputs, true, 0,
                       [] (auto) -> int { throw std::runtime_error ("test"); }));
  check (try_prepass (p, metadata, inputs, outputs, true, 0, [] (auto) {
    usleep (300000);
    return EXIT_CODE_UNREAL;
  }));
  check (!try_prepass (parse ("!o"), metadata, inputs, outputs, false, 0, [] (auto) {
    usleep (300000);
    return EXIT_CODE_REAL;
  }));
  check (!try_prepass (p, metadata, inputs, outputs, true, 0, [] (auto) {
    sleep (2);
    return EXIT_CODE_UNREAL;
  }));
#ifdef __linux__
  check (!acacia::phase_records_enabled ());
  check (acacia::resource_snapshot (getpid ()).rss_kb > 0);
  rlimit original_limit {};
  check (getrlimit (RLIMIT_AS, &original_limit) == 0);
  acacia::worker_record worker;
  acacia::active_worker_record () = &worker;
  const auto objective = parse ("G(o <-> (p | q))");
  int ready[2];
  check (pipe (ready) == 0);
  const auto started = acacia::phase_clock (CLOCK_MONOTONIC);
  check (!try_prepass (objective, metadata, inputs, outputs, true, 0, [&] (auto) {
    // Exercise the RSS watchdog even in builds that also enforce RLIMIT_AS.
    check (setrlimit (RLIMIT_AS, &original_limit) == 0);
    constexpr size_t bytes = size_t {640} * 1024 * 1024;
    auto* memory = static_cast<volatile char*> (
        mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    check (memory != MAP_FAILED);
    const auto page_size = sysconf (_SC_PAGESIZE);
    check (page_size > 0);
    check (write (ready[1], "r", 1) == 1);
    for (size_t offset = 0; offset < bytes; offset += size_t (page_size))
      memory[offset] = 1;
    usleep (20000);
    return EXIT_CODE_UNREAL;
  }));
  close (ready[1]);
  char allocated = 0;
  check (read (ready[0], &allocated, 1) == 1 && allocated == 'r');
  close (ready[0]);
  // A deadline or an allocation failure must not masquerade as RSS enforcement.
  check (acacia::phase_clock (CLOCK_MONOTONIC) - started < 500000000);
  check (worker.stops == 1 && worker.declines == 0 && !worker.stopped && !worker.verified &&
         worker.descendant_pid == -1);
  acacia::active_worker_record () = nullptr;
  rlimit after_limit {};
  check (getrlimit (RLIMIT_AS, &after_limit) == 0 &&
         after_limit.rlim_cur == original_limit.rlim_cur &&
         after_limit.rlim_max == original_limit.rlim_max);
  // Resume the ordinary direct solve on the untouched positive objective.
  check (acacia::syntactic_bypass::try_direct (objective, outputs).value ==
         acacia::syntactic_bypass::verdict::realizable);
#endif
  int status;
  check (waitpid (-1, &status, WNOHANG) == -1 && errno == ECHILD);
  std::cout << "simultaneous full-objective substitution, ownership, polarity and bounded "
               "fallback checked\n";
}
