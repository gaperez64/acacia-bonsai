#include "solver/obligation_helper.hh"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <spot/tl/parse.hh>
#include <sstream>

namespace {
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
}

int main () {
  using namespace acacia::obligation;
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
      require (!value.independent_checked && value.controller.empty (),
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
      require (synthesized.value == outcome::real && synthesized.independent_checked,
               "synthesis lacks checked controller");
      std::istringstream bytes (synthesized.controller);
      const auto circuit = spot::aig::parse_aag (bytes, "unit controller");
      circuit->set_output (0, circuit->output (0) ^ 1U);
      const bool corrupted = check_controller (objective, circuit, semantics == timing::moore);
      if (std::string_view (text) != "1")
        require (!corrupted, "corrupted controller accepted");
    }
    const auto losing = solve (parse ("G i"), {"i"}, {"o"}, semantics, true);
    require (losing.value == outcome::inapplicable && losing.controller.empty (),
             "synthesis emitted a decision without a strategy");
  }
  const auto current_input = std::make_shared<spot::aig> (std::vector<std::string> {"i"},
                                                          std::vector<std::string> {"o"}, 0);
  current_input->set_output (0, current_input->input_var (0));
  const bool invalid_moore = check_controller (parse ("1"), current_input, true);
  require (!invalid_moore, "Moore controller used current inputs");
  empty.reset ();
  dict->unregister_all_my_variables (&owner);
  std::cout << "obligation helper checks passed\n";
}
