#include "solver/parity/route.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;

namespace route = acacia::parity_route;
namespace reference = acacia::parity_route;

void require (bool value, const char* reason) {
  if (!value)
    throw std::runtime_error (reason);
}

spot::formula formula (const char* text) {
  auto parsed = spot::parse_infix_psl (text);
  require (parsed.f && parsed.errors.empty (), "invalid fixture formula");
  return parsed.f;
}

spot::twa_graph_ptr negated_spec_aut (unsigned tier) {
  auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
  const auto x = bdd_ithvar (aut->register_ap (spot::formula::ap ("x")));
  const auto y = bdd_ithvar (aut->register_ap (spot::formula::ap ("y")));
  aut->new_states (tier == 3 ? 2 : 1);
  aut->set_init_state (0);
  if (tier == 3) {
    aut->set_acceptance (2, spot::acc_cond::acc_code ("Fin(0) & Inf(1)"));
    aut->new_edge (0, 0, x, {0});
    aut->new_edge (0, 1, !x);
    aut->new_edge (1, 1, bddtrue, {1});
  }
  else if (tier == 2) {
    aut->set_acceptance (2, spot::acc_cond::acc_code::generalized_buchi (2));
    aut->new_edge (0, 0, x & y, {0, 1});
    aut->new_edge (0, 0, x & !y, {0});
    aut->new_edge (0, 0, !x & y, {1});
    aut->new_edge (0, 0, !x & !y);
  }
  else {
    aut->set_acceptance (
        1, tier == 1 ? spot::acc_cond::acc_code::buchi () : spot::acc_cond::acc_code::cobuchi ());
    aut->new_edge (0, 0, x, {0});
    aut->new_edge (0, 0, !x);
  }
  return aut;
}

void check_controller (const spot::aig_ptr& circuit, spot::formula specification, bool moore) {
  if (moore) {
    std::vector<bool> dependent (circuit->max_var () / 2 + 1);
    for (unsigned i = 0; i < circuit->num_inputs (); ++i)
      dependent[circuit->input_var (i) / 2] = true;
    for (unsigned i = 0; i < circuit->gates ().size (); ++i) {
      const auto gate = circuit->gates ()[i];
      dependent[circuit->gate_var (i) / 2] =
          dependent[gate.first / 2] || dependent[gate.second / 2];
    }
    for (const auto output : circuit->outputs ())
      require (!dependent[output / 2], "Moore output depends on the current input");
  }
  auto aut = circuit->as_automaton (false);
  spot::translator checker (aut->get_dict ());
  checker.set_type (spot::postprocessor::BA);
  require (!checker.run (spot::formula::Not (specification))->intersects (aut),
           "exported controller violates the specification");
}

int main (int argc, char** argv) {
  try {
    if (argc == 5 && std::string (argv[1]) == "--check-controller") {
      auto circuit = spot::aig::parse_aag (std::string (argv[2]));
      check_controller (circuit, formula (argv[3]), std::string (argv[4]) == "moore");
      return 0;
    }
    const char* specifications[] = {"", "FG !x", "FG !x | FG !y", "G x", "GF x"};
    for (unsigned tier = 1; tier <= 4; ++tier)
      for (bool moore : {false, true})
        for (bool real : {false, true}) {
          route::request input;
          input.automaton = negated_spec_aut (tier);
          input.specification = formula (specifications[tier]);
          input.source = route::source_language::real_worker;
          input.inputs = real ? std::vector<std::string> {"unused_input"}
                              : std::vector<std::string> {"x", "y", "unused_input"};
          input.outputs = real ? std::vector<std::string> {"x", "y", "unused_output"}
                               : std::vector<std::string> {"unused_output"};
          input.moore = moore;
          bool exported = false, verification = false, published = false;
          route::options decision;
          decision.synthesis = false;
          decision.on_verdict = [&] (bool winner) {
            require (winner == real, "published wrong verdict");
            published = true;
          };
          auto answer = route::run (input, decision, [&] (const char* stage, const auto&, double) {
            exported |=
                std::string (stage) == "strategy" || std::string (stage) == "encode-controller";
            verification |= std::string (stage).starts_with ("verify-");
          });
          require (published && !exported && !verification && !answer.circuit,
                   "decision allocated a controller or checked a strategy");
          require (answer.tier == tier, "fixture missed its tier");
          require (answer.verdict == (real ? "REALIZABLE" : "UNREALIZABLE"),
                   "decision disagrees with the fixture oracle");
          if (tier < 4) {
            require (!answer.game && answer.solution.winner.empty (),
                     "production decision retained an explicit game or winning region");
            bool selected = false;
            auto synthesis = decision;
            synthesis.synthesis = true;
            synthesis.on_verdict = [&] (bool winner) {
              require (winner == real, "synthesis published wrong verdict");
              selected = true;
            };
            auto made =
                route::run (input, synthesis, [&] (const char* stage, const auto&, double) {
                  if (std::string (stage) == "strategy" ||
                      std::string (stage) == "encode-controller")
                    require (selected, "controller construction preceded selection");
                });
            require (selected && made.verdict == answer.verdict &&
                         bool (made.circuit) == real && !made.game &&
                         made.solution.winner.empty (),
                     "production synthesis changed the verdict or retained diagnostics");
            if (real)
              check_controller (made.circuit, input.specification, moore);
            for (bool solution_output : {false, true}) {
              auto observed = decision;
              bool visited = false;
              auto check_game = [&] (const auto& game) {
                require (!game.extract ().winner.empty (), "diagnostic lost its winning region");
                visited = true;
              };
              if (solution_output)
                observed.on_solution = [&] (const auto& game, const auto&) { check_game (game); };
              else
                observed.on_game = check_game;
              auto diagnostic = route::run (input, observed);
              require (visited && diagnostic.game && diagnostic.verdict == answer.verdict,
                       "game or solution observer changed the verdict or lost the arena");
            }
          }
          reference::options diagnostic;
          diagnostic.verify = true;
          diagnostic.cross_check = true;
          auto checked = reference::run (input, diagnostic);
          require (checked.verdict == answer.verdict && checked.checked,
                   "production decision differs from the checked research route");
          diagnostic.use_spot = true;
          require (reference::run (input, diagnostic).verdict == answer.verdict,
                   "Spot disagrees on the same arena");
          require (bool (checked.circuit) == real, "incorrect controller presence");
        }
    // Selection precedes strategy extraction and encoding, in every tier.
    for (unsigned tier = 1; tier <= 4; ++tier) {
      route::request input;
      input.automaton = negated_spec_aut (tier);
      input.source = route::source_language::real_worker;
      input.outputs = {"x", "y"};
      bool selected = false;
      route::options synthesis;
      synthesis.on_verdict = [&] (bool real) {
        require (real, "expected REAL before export");
        selected = true;
      };
      auto answer = route::run (input, synthesis, [&] (const char* stage, const auto&, double) {
        if (std::string (stage) == "strategy" || std::string (stage) == "encode-controller")
          require (selected, "controller construction preceded selection");
      });
      require (selected && answer.circuit, "missing selected controller");
    }
    std::cout
        << "All four tiers: REAL/UNREAL, Mealy/Moore, decision and checked synthesis passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
