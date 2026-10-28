#include "solver/obligation_helper.hh"
#include "solver/controller_check.hh"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <tuple>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>

namespace {
  bool check_controller (spot::formula objective, const spot::aig_ptr& controller,
                         bool moore = false) {
    if (!objective || !controller)
      return false;
    if (moore) {
      std::vector<bool> input_dependent (controller->max_var () / 2 + 1, false);
      for (unsigned i = 0; i < controller->num_inputs (); ++i)
        input_dependent[controller->input_var (i) / 2] = true;
      for (unsigned i = 0; i < controller->num_gates (); ++i) {
        const auto [left, right] = controller->gates ()[i];
        input_dependent[controller->gate_var (i) / 2] =
            input_dependent[left / 2] || input_dependent[right / 2];
      }
      for (const unsigned output : controller->outputs ())
        if (input_dependent[output / 2])
          return false;
    }
    const auto machine = controller->as_automaton (false);
    spot::translator translator (machine->get_dict ());
    translator.set_type (spot::postprocessor::BA);
    const auto negated = translator.run (spot::formula::Not (objective));
    return acacia::synthesis::controller_satisfies (negated, controller);
  }

  void require (bool condition, const char* message) {
    if (!condition) {
      std::cerr << message << '\n';
      std::exit (1);
    }
  }
  spot::formula parse (const std::string& text) {
    const auto parsed = spot::parse_infix_psl (text);
    require (parsed.f && parsed.errors.empty (), "parse failed");
    return parsed.f;
  }

  void check_memory_share () {
    using namespace acacia::obligation;
    namespace fs = std::filesystem;
    char directory[] = "obligation-memory-XXXXXX";
    require (mkdtemp (directory), "cannot create cgroup fixture");
    const fs::path scope = directory;
    const auto write = [&] (const char* file, const std::string& contents) {
      std::ofstream output (scope / file);
      require (bool (output << contents), "cannot write cgroup fixture");
    };
    constexpr uint64_t share = 100;
    const auto exceeded = [&] (uint64_t resident) {
      return rss_share_exceeded (resident, share,
                                 scope_memory ("memory.max", directory).value_or (0), directory);
    };
    write ("memory.max", "800\n");
    require (scope_memory ("memory.max", directory) == 800, "finite scope limit not read");
    for (const auto& [used, enforce] : std::vector<std::pair<uint64_t, bool>> {
             {0, false}, {699, false}, {700, true}, {701, true}, {800, true}, {UINT64_MAX, true}}) {
      write ("memory.current", std::to_string (used) + "\n");
      require (scope_memory ("memory.current", directory) == used, "scope usage not read");
      require (exceeded (share + 1) == enforce, "headroom enforcement boundary changed");
      require (!exceeded (share) && !exceeded (share - 1), "worker within its share killed");
      require (rss_share_exceeded (share + 1, share, 0, directory), "equal share not enforced");
    }
    write ("memory.current", "0\n");
    for (const char* limit : {"max\n", "", "invalid\n"}) {
      write ("memory.max", limit);
      require (!scope_memory ("memory.max", directory), "unbounded/invalid limit accepted");
      require (exceeded (share + 1) && !exceeded (share), "unbounded limit differs from equal");
    }
    for (const char* file : {"memory.max", "memory.current"}) {
      write ("memory.max", "800\n");
      write ("memory.current", "0\n");
      fs::remove (scope / file);
      require (!scope_memory (file, directory), "missing scope file accepted");
      require (exceeded (share + 1) && !exceeded (share), "missing scope file differs from equal");
      // A directory gives a real read error even when tests run as root.
      fs::create_directory (scope / file);
      require (!scope_memory (file, directory), "unreadable scope file accepted");
      require (exceeded (share + 1), "unreadable scope file differs from equal");
      fs::remove (scope / file);
      if (geteuid () != 0) {
        write (file, "800\n");
        fs::permissions (scope / file, fs::perms::none);
        require (!scope_memory (file, directory), "scope file permissions ignored");
        require (exceeded (share + 1), "inaccessible scope file differs from equal");
        fs::permissions (scope / file, fs::perms::owner_all);
      }
    }
    fs::remove_all (scope);
  }
}

int main () {
  acacia::phase_records_start_writer ();
  acacia::worker_record accounting;
  accounting.pid = getpid ();
  if (acacia::phase_records_enabled ())
    acacia::active_worker_record () = &accounting;

  using namespace acacia::obligation;
  constexpr uint64_t gib = uint64_t {1024} * 1024 * 1024;
  for (const auto& [limit, workers, equal, alone] :
       std::vector<std::tuple<uint64_t, size_t, uint64_t, uint64_t>> {
           {8 * gib, 6, 8 * gib / 6 / 2, 4 * gib},
           {8 * gib, 1, 4 * gib, 4 * gib},
           {8 * gib, 0, 4 * gib, 4 * gib},
           {3 * gib, 6, gib / 4, 3 * gib / 2},
           {UINT64_MAX, 6, 8 * gib / 6 / 2, 4 * gib},
           {0, 6, 0, 0}, {101, 3, 16, 50}, {1, 1, 0, 0}}) {
    require (rss_share_for_limit (limit, workers, memory_share::equal) == equal,
             "equal RSS computation changed");
    require (rss_share_for_limit (limit, workers, memory_share::alone) == alone,
             "alone RSS computation exceeded the finite limit");
    require (rss_share_for_limit (limit, workers, memory_share::headroom) == equal,
             "headroom RSS computation differs from equal");
  }
  check_memory_share ();
  const auto absent = solve (spot::formula::tt (), {"fresh_input"}, {"fresh_output"});
  require (absent.value == outcome::real, "absent output AP broke decision");
  for (const char* text : {"1", "0", "G o", "F o", "i U o", "i W o", "i R o", "i M o",
                           "G (i <-> o)", "F (i xor o)", "G (i -> X o)"}) {
    const auto f = parse (text);
    require (eligible (f), "obligation declined");
  }
  for (const char* text : {"G F (i <-> o)", "F G (i <-> o)", "G (i -> F o)"}) {
    const auto f = parse (text);
    require (!eligible (f), "non-obligation accepted");
    const auto value = solve (f, {"i"}, {"o"});
    require (value.value == outcome::inapplicable, "non-obligation did not decline");
  }
  const auto shared = parse ("F (i <-> o)");
  const auto dag = spot::formula::And ({shared, spot::formula::X (shared)});
  require (eligible (dag), "shared DAG declined");
  control channel;
  publish_eligibility (&channel, dag);
  require (channel.eligible.load () == eligibility::eligible, "eligibility not published");
  require (!eligible ({}), "empty formula eligible");

  require (completed (nullptr, true) == outcome::error, "null became a decision");
  auto dict = spot::make_bdd_dict ();
  auto empty = std::make_shared<spot::mtdswa> (dict);
  empty->acc = spot::acc_cond::acc_code::f ();
  require (completed (empty, true) == outcome::error, "empty became UNREAL");
  empty->states.push_back (bddfalse);
  require (completed (empty, true) == outcome::error, "malformed colors became UNREAL");
  empty->colors.emplace_back ();
  require (completed (empty, true) == outcome::unreal, "explicit losing result not recognized");
  empty->acc = spot::acc_cond::acc_code::t ();
  require (completed (empty, true) == outcome::error, "invalid REAL decision accepted");
  for (const auto value : {outcome::pending, outcome::inapplicable, outcome::exhausted,
                           outcome::cancelled, outcome::error})
    require (exit_code (value) == EXIT_CODE_UNKNOWN, "nondecision yielded a verdict");

  for (auto semantics : {timing::mealy, timing::moore}) {
    for (const auto& [text, expected] : std::vector<std::pair<std::string, outcome>> {
             {"G (i -> X o)", outcome::real},
             {"G (i <-> X o)", outcome::real},
             {"G (X i <-> o)", outcome::unreal},
             {"G o & F !o", outcome::unreal},
             {"G (i <-> o)", semantics == timing::mealy ? outcome::real : outcome::unreal}}) {
      const auto f = parse (text);
      const auto value = solve (f, {"i", "unused_input"}, {"o", "unused_output"}, semantics);
      require (value.value == expected, "timed obligation verdict mismatch");
      require (!value.controller,
               "decision retained or checked a strategy");
      std::cout << (semantics == timing::mealy ? "mealy" : "moore") << '\t' << text << '\t'
                << name (value.value) << '\n';
    }
  }
  // Registering Moore as Mealy changes this answer. The oracle test independently
  // checks these rows, so removing the Moore registration order is detected.
  for (const auto& [text, expected] :
       std::vector<std::pair<std::string, outcome>> {{"1", outcome::real},
                                                     {"0", outcome::unreal},
                                                     {"F o", outcome::real},
                                                     {"F i", outcome::unreal}}) {
    const auto value = solve (parse (text), {"i"}, {"o"});
    require (value.value == expected, "constant/eventual obligation mismatch");
  }

  int owner = 0;
  dict->register_proposition (spot::formula::ap ("i"), &owner);
  dict->register_proposition (spot::formula::ap ("o"), &owner);
  {
    const auto yes = spot::obligation_synthesis (parse ("G o"), dict, {"o"}, true);
    const auto no = spot::obligation_synthesis (parse ("G i"), dict, {"o"}, true);
    require (completed (yes, true) == outcome::real, "realizability=true lost REAL");
    require (completed (no, true) == outcome::unreal, "realizability=true lost UNREAL");
    const auto wrong = spot::obligation_synthesis (parse ("G !o"), dict, {"o"}, false);
    const auto wrong_mealy = spot::mtdswa_strategy_to_mealy (wrong, false, false);
    const auto wrong_aig = spot::mealy_machines_to_aig (
        std::vector<spot::twa_graph_ptr> {wrong_mealy}, "isop", {"i"}, {{"o"}});
    const bool accepted = check_controller (parse ("G o"), wrong_aig);
    require (!accepted, "mutated strategy accepted");
  }
  for (auto semantics : {timing::mealy, timing::moore}) {
    for (unsigned table = 0; table < 16; ++table) {
      std::vector<spot::formula> terms;
      for (unsigned letter = 0; letter < 4; ++letter)
        if ((table >> letter) & 1U)
          terms.push_back (spot::formula::And (
              {(letter & 1U) ? parse ("i") : parse ("!i"),
               (letter & 2U) ? parse ("unused_input") : parse ("!unused_input")}));
      const auto boolean = spot::formula::Or (terms);
      for (bool delay : {false, true}) {
        const auto objective = spot::formula::And (
            {spot::formula::G (spot::formula::Equiv (
                 boolean, delay ? spot::formula::X (parse ("o")) : parse ("o"))),
             parse ("G (unused_output <-> X o)")});
        const std::vector<std::string> inputs {"i", "unused_input"};
        const std::vector<std::string> outputs {"o", "unused_output"};
        const auto answer = solve (objective, inputs, outputs, semantics);
        require (answer.value == outcome::real || answer.value == outcome::unreal,
                 "generated obligation returned no decision");
        if (answer.value == outcome::real) {
          const auto strategy = solve (objective, inputs, outputs, semantics, true);
          require (strategy.value == outcome::real, "generated synthesis failed");
          const auto& circuit = strategy.controller;
          require (std::ranges::is_permutation (circuit->input_names (), inputs) &&
                       std::ranges::is_permutation (circuit->output_names (), outputs),
                   "generated controller lost declared IO");
          require (check_controller (objective, circuit, semantics == timing::moore),
                   "generated controller failed offline product check");
        }
        for (bool reverse : {false, true}) {
          auto first = semantics == timing::mealy ? inputs : outputs;
          auto second = semantics == timing::mealy ? outputs : inputs;
          if (reverse) {
            std::ranges::reverse (first);
            std::ranges::reverse (second);
          }
          auto reference_dict = spot::make_bdd_dict ();
          int reference_owner = 0;
          for (const auto& block : {first, second})
            for (const auto& ap : block)
              reference_dict->register_proposition (spot::formula::ap (ap), &reference_owner);
          auto reference = spot::obligation_synthesis (objective, reference_dict, outputs, true);
          const auto verdict = completed (reference, true);
          require (answer.value == verdict, "registration-order variant disagrees");
          reference.reset ();
          reference_dict->unregister_all_my_variables (&reference_owner);
        }
        std::cout << (semantics == timing::mealy ? "mealy" : "moore") << '\t' << objective << '\t'
                  << name (answer.value) << '\n';
      }
    }
    for (const char* text : {"1", "G o", "o & G (i <-> X o)"}) {
      const auto objective = parse (text);
      const auto synthesized = solve (objective, {"i"}, {"o"}, semantics, true);
      require (synthesized.value == outcome::real && bool (synthesized.controller),
               "synthesis lacks controller");
      const auto& circuit = synthesized.controller;
      require (check_controller (objective, circuit, semantics == timing::moore),
               "exported controller failed offline check");
      circuit->set_output (0, circuit->output (0) ^ 1U);
      const bool corrupted = check_controller (objective, circuit, semantics == timing::moore);
      if (std::string_view (text) != "1")
        require (!corrupted, "corrupted controller accepted");
    }
    const auto losing = solve (parse ("G i"), {"i"}, {"o"}, semantics, true);
    require (losing.value == outcome::inapplicable && !losing.controller,
             "synthesis emitted a decision without a strategy");
  }
  // This Moore objective becomes a false positive when registered as Mealy.
  // The offline controller checker must catch that timing mutation.
  const auto equality = parse ("G (i <-> o)");
  const auto misregistered = solve (equality, {"i"}, {"o"}, timing::mealy, true);
  require (misregistered.value == outcome::real, "timing mutation produced no controller");
  const auto& mutation = misregistered.controller;
  require (check_controller (equality, mutation), "Mealy reference controller is invalid");
  require (!check_controller (equality, mutation, true),
           "offline check missed Moore registered as Mealy");
  const auto wrong_moore = solve (equality, {"i"}, {"o"}, timing::moore, true);
  require (wrong_moore.value == outcome::inapplicable,
           "test missed Mealy registered as Moore");
  const auto current_input = std::make_shared<spot::aig> (std::vector<std::string> {"i"},
                                                          std::vector<std::string> {"o"}, 0);
  current_input->set_output (0, current_input->input_var (0));
  const bool invalid_moore = check_controller (parse ("1"), current_input, true);
  require (!invalid_moore, "Moore controller used current inputs");
  empty.reset ();
  dict->unregister_all_my_variables (&owner);
  acacia::active_worker_record () = nullptr;
  std::cout << "obligation helper checks passed\n";
}
