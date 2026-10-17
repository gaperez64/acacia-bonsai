#include "solver/parity/route.hh"

#include <algorithm>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/dualize.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>

namespace route = acacia::parity_route;
void require (bool condition, const char* message) {
  if (not condition)
    throw std::runtime_error (message);
}
spot::formula formula (const std::string& text) {
  auto parsed = spot::parse_infix_psl (text);
  require (parsed.f and parsed.errors.empty (), "formula parse failed");
  return parsed.f;
}
spot::formula shift_inputs (spot::formula f, const std::vector<std::string>& inputs) {
  if (f.is (spot::op::ap) and std::ranges::find (inputs, f.ap_name ()) != inputs.end ())
    return spot::formula::X (f);
  return f.map ([&] (spot::formula child) { return shift_inputs (child, inputs); });
}
int main () {
  try {
    unsigned tier_four = 0;
    for (bool moore : {false, true}) {
      for (const std::string text :
           {"1", "0", "G o", "F o", "GF o", "GF i", "FG o", "G(i <-> o)", "G(i -> X o)",
            "GF i -> GF o", "(GF i & GF o) | (FG !i & FG !o)"}) {
        auto f = formula (text);
        route::request input;
        input.formula = f;
        input.specification = f;
        input.inputs = {"i", "unused_input"};
        input.outputs = {"o", "unused_output"};
        input.moore = moore;
        route::options options;
        options.verify = true;
        options.cross_check = true;
        auto answer = route::run (input, options);
        require (answer.checked, "route did not verify its verdict");
        require (answer.route == (answer.tier == 4 ? "strpm-simd" : "acacia"),
                 "tier routing is incorrect");
        tier_four += answer.tier == 4;
        options.use_spot = true;
        auto spot_answer = route::run (input, options);
        require (answer.verdict == spot_answer.verdict, "route solvers disagree");
        options.use_lar = true;
        auto lar_answer = route::run (input, options);
        require (answer.verdict == lar_answer.verdict, "ACD and LAR disagree");
        // The REAL worker capture is the negation, after Moore-to-Mealy input shifting.
        input.source = route::source_language::real_worker;
        input.frame = route::timing_frame::effective_mealy;
        input.formula = spot::formula::Not (moore ? shift_inputs (f, input.inputs) : f);
        options.use_spot = false;
        options.use_lar = false;
        auto worker_answer = route::run (input, options);
        require (answer.verdict == worker_answer.verdict, "worker polarity or frame differs");
        // Replay an independently serialized boundary through the same graph API.
        spot::translator translator;
        translator.set_type (spot::postprocessor::Generic);
        translator.set_pref (spot::postprocessor::Deterministic);
        input.automaton = translator.run (input.formula);
        auto hoa_answer = route::run (input, options);
        require (answer.verdict == hoa_answer.verdict, "HOA replay differs");
      }
    }
    require (tier_four > 0, "no actual tier-four games exercised");
    // Exact structural tiers, including an acceptance claim that cannot be trusted.
    auto dict = spot::make_bdd_dict ();
    auto buchi = spot::make_twa_graph (dict);
    buchi->new_states (1);
    buchi->set_init_state (0);
    const auto x = bdd_ithvar (buchi->register_ap (spot::formula::ap ("x")));
    buchi->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    buchi->new_edge (0, 0, x, {0});
    buchi->new_edge (0, 0, !x);
    auto t1 = route::classify (buchi);
    require (t1.tier == 1, "Buchi tier differs");
    auto gba = spot::make_twa_graph (dict);
    gba->copy_ap_of (buchi);
    gba->new_states (1);
    gba->set_init_state (0);
    gba->set_acceptance (2, spot::acc_cond::acc_code::generalized_buchi (2));
    gba->new_edge (0, 0, x, {0});
    gba->new_edge (0, 0, !x, {1});
    auto t2 = route::classify (gba);
    require (t2.tier == 2, "generalized Buchi tier differs");
    require (spot::are_equivalent (gba, t2.buchi), "tier-two conversion changed language");
    auto rabin = spot::make_twa_graph (dict);
    rabin->copy_ap_of (buchi);
    rabin->new_states (2);
    rabin->set_init_state (0);
    rabin->set_acceptance (2, spot::acc_cond::acc_code ("Fin(0) & Inf(1)"));
    rabin->new_edge (0, 0, x, {0});
    rabin->new_edge (0, 1, !x);
    rabin->new_edge (1, 1, bddtrue, {1});
    auto t3 = route::classify (rabin);
    require (t3.tier == 3, "Rabin-realizable tier differs");
    require (spot::are_equivalent (rabin, t3.buchi), "tier-three conversion changed language");
    auto cobuchi = spot::make_twa_graph (buchi, spot::twa::prop_set::all ());
    cobuchi->set_acceptance (1, spot::acc_cond::acc_code::cobuchi ());
    cobuchi->prop_weak (true);
    require (route::classify (cobuchi).tier == 4, "false weak claim was trusted");
    // Complementation must complete the actual graph even when a property lies.
    route::request partial;
    partial.inputs = {"i"};
    partial.outputs = {"o"};
    partial.source = route::source_language::real_worker;
    partial.specification = formula ("F o | GF i");
    partial.automaton = spot::make_twa_graph (spot::make_bdd_dict ());
    partial.automaton->new_states (1);
    partial.automaton->set_init_state (0);
    auto o = bdd_ithvar (partial.automaton->register_ap (spot::formula::ap ("o")));
    auto i = bdd_ithvar (partial.automaton->register_ap (spot::formula::ap ("i")));
    partial.automaton->set_acceptance (1, spot::acc_cond::acc_code::cobuchi ());
    partial.automaton->new_edge (0, 0, !o & i, {0});
    partial.automaton->new_edge (0, 0, !o & !i);
    partial.automaton->prop_complete (true);
    require (route::run (partial, {.verify = true, .cross_check = true}).verdict == "REALIZABLE",
             "false completeness claim changed the verdict");
    auto malformed = spot::make_twa_graph (buchi, spot::twa::prop_set::all ());
    malformed->new_edge (0, 0, bddtrue);
    malformed->prop_universal (true);
    bool nondeterministic_declined = false;
    try {
      route::classify (malformed);
    } catch (const std::exception&) {
      nondeterministic_declined = true;
    }
    require (nondeterministic_declined, "false determinism claim was trusted");
    route::request renamed;
    renamed.formula = formula ("GF request -> GF response");
    renamed.specification = renamed.formula;
    renamed.inputs = {"unused", "request"};
    renamed.outputs = {"response", "extra"};
    require (route::run (renamed, {.verify = true, .cross_check = true}).verdict == "REALIZABLE",
             "renamed and permuted interface differs");
    for (const auto& partition :
         {std::pair {std::vector<std::string> {}, std::vector<std::string> {"o"}},
          std::pair {std::vector<std::string> {"i"}, std::vector<std::string> {}}}) {
      route::request input;
      input.inputs = partition.first;
      input.outputs = partition.second;
      input.formula = formula (input.outputs.empty () ? "GF i" : "GF o");
      input.specification = input.formula;
      auto result = route::run (input, {.verify = true, .cross_check = true});
      require (result.checked, "empty-partition result failed verification");
    }
    // A mismatched original specification must decline a proposed REAL result.
    route::request wrong;
    wrong.formula = formula ("1");
    wrong.specification = formula ("0");
    bool declined = false;
    try {
      route::run (wrong, {.verify = true});
    } catch (const std::exception&) {
      declined = true;
    }
    require (declined, "wrong specification produced a REAL verdict");
    // The trusted route must do neither game nor original-specification checks.
    for (const char* text : {"1", "0", "GF o", "GF i"}) {
      route::request input;
      input.formula = formula (text);
      input.inputs = {"i"};
      input.outputs = {"o"};
      bool verification_ran = false;
      auto answer = route::run (input, {}, [&] (const char* stage, const auto&, double) {
        verification_ran |= std::string (stage).starts_with ("verify-");
      });
      require (not verification_ran and not answer.checked and answer.verification == "none",
               "default route performed verification");
      require (not answer.circuit or answer.verdict == "REALIZABLE",
               "trusted route lost its controller");
    }
    require (route::run (wrong).verdict == "REALIZABLE",
             "default route verified the original specification");
    std::cout << "Original/worker, Mealy/Moore, ACD/LAR, both solvers and HOA routes passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
