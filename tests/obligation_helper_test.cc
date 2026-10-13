#include "solver/obligation_helper.hh"

#include <cstdlib>
#include <iostream>
#include <spot/tl/parse.hh>

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
      require ((value.value == outcome::real) == value.same_library_checked,
               "REAL missing same-library check");
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
    const bool accepted = check_strategy (parse ("G o"), wrong);
    require (!accepted, "mutated strategy accepted");
  }
  empty.reset ();
  dict->unregister_all_my_variables (&owner);
  std::cout << "obligation helper checks passed\n";
}
