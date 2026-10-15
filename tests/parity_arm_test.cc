#include "research/parity_route.hh"
#include "solver/parity/buchi_antichain.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/translate.hh>
#include <stdexcept>

#include <posets/vectors/traits.hh>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
size_t posets::vectors::bool_threshold = 0;

namespace route = acacia::parity_route;
namespace reference = acacia::research::parity_route;

void require (bool value, const char* reason) {
  if (!value)
    throw std::runtime_error (reason);
}

spot::formula formula (const char* text) {
  auto parsed = spot::parse_infix_psl (text);
  require (parsed.f && parsed.errors.empty (), "invalid fixture formula");
  return parsed.f;
}

spot::twa_graph_ptr forbidden (unsigned tier) {
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

void use_antichain (route::options& options, const route::request& input) {
  route::antichain_options solver;
  solver.synthesis = options.synthesis;
  options.solve_buchi = [input, solver] (auto buchi, bool moore) {
    return route::solve_buchi_antichain (buchi, input.inputs, input.outputs, moore, solver);
  };
}

int main (int argc, char** argv) {
  std::string context;
  try {
    if (argc == 5 && std::string (argv[1]) == "--check-controller") {
      auto circuit = spot::aig::parse_aag (std::string (argv[2]));
      if (std::string (argv[4]) == "moore") {
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
      require (!checker.run (spot::formula::Not (formula (argv[3])))->intersects (aut),
               "exported controller violates the specification");
      return 0;
    }
    const char* specifications[] = {"", "FG !x", "FG !x | FG !y", "G x", "GF x"};
    for (unsigned tier = 1; tier <= 4; ++tier)
      for (bool moore : {false, true})
        for (bool real : {false, true})
          for (bool antichain : {false, true}) {
            context = "tier=" + std::to_string (tier) + " moore=" + std::to_string (moore) +
                      " real=" + std::to_string (real) +
                      " antichain=" + std::to_string (antichain);
            route::request input;
            input.automaton = forbidden (tier);
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
            if (antichain)
              use_antichain (decision, input);
            decision.on_verdict = [&] (bool winner) {
              require (winner == real, "published wrong verdict");
              published = true;
            };
            auto answer =
                route::run (input, decision, [&] (const char* stage, const auto&, double) {
                  exported |= std::string (stage) == "strategy" ||
                              std::string (stage) == "encode-controller";
                  verification |= std::string (stage).starts_with ("verify-");
                });
            require (published && !exported && !verification && !answer.circuit,
                     "decision allocated a controller or checked a strategy");
            require (answer.tier == tier, "fixture missed its tier");
            if (antichain and tier < 4) {
              require (answer.solution.stats.k >= unsigned (DEFAULT_KMIN),
                       "antichain omitted the winning bound");
              require (answer.route == "antichain" && !answer.game,
                       "antichain used an explicit game");
            }
            require (answer.verdict == (real ? "REALIZABLE" : "UNREALIZABLE"),
                     "decision disagrees with the fixture oracle");
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
            if (antichain) {
              route::options synthesis;
              synthesis.verify = true;
              use_antichain (synthesis, input);
              auto made = route::run (input, synthesis);
              require (made.verdict == answer.verdict && made.checked,
                       "antichain synthesis differs from decision");
              require (bool (made.circuit) == real, "incorrect antichain controller presence");
            }
          }
    // Indexed hints retain their declaration roles when the UNREAL check swaps
    // players. Force backward to exercise equivariance in forward-default builds.
    for (bool moore : {false, true})
      for (const auto* objective : {"G(i_0 & i_1 & i_2)", "G(o_0 <-> i_0)"}) {
        context = "indexed hints: moore=" + std::to_string (moore) + " objective=" + objective;
        route::request input;
        input.formula = formula (objective);
        input.inputs = {"i_0", "i_1", "i_2"};
        input.outputs = {"o_0", "o_1", "o_2"};
        input.moore = moore;
        route::options options;
        options.synthesis = false;
        auto expected = route::run (input, options);
        require (expected.tier == 3, "indexed fixture missed tier three");
        const bool real = !moore && std::string (objective) == "G(o_0 <-> i_0)";
        require (expected.verdict == (real ? "REALIZABLE" : "UNREALIZABLE"),
                 "indexed fixture disagrees with its oracle");
        route::antichain_options solver;
        solver.kmin = solver.kmax = 5;
        solver.real_backend = solver.unreal_backend = acacia::game_backend::backward;
        solver.equivariance = true;
        solver.hints = {{.is_input = true, .lo = 0, .hi = 2,
                         .members = {"i_0", "i_1", "i_2"}},
                        {.is_input = false, .lo = 0, .hi = 2,
                         .members = {"o_0", "o_1", "o_2"}}};
        options.solve_buchi = [&] (auto buchi, bool timing) {
          return route::solve_buchi_antichain (buchi, input.inputs, input.outputs, timing, solver);
        };
        auto actual = route::run (input, options);
        require (actual.verdict == expected.verdict, "indexed hints changed the verdict");
        require (solver.hints[0].is_input && !solver.hints[1].is_input,
                 "polarity check mutated the original hints");
      }
    // Selection precedes strategy extraction and encoding, in every tier.
    for (unsigned tier = 1; tier <= 4; ++tier)
      for (bool antichain : {false, true}) {
        route::request input;
        input.automaton = forbidden (tier);
        input.source = route::source_language::real_worker;
        input.outputs = {"x", "y"};
        bool selected = false;
        route::options synthesis;
        if (antichain)
          use_antichain (synthesis, input);
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
    // A current-input dependency distinguishes Mealy from Moore. Test partial
    // forbidden DBAs too: complementation must complete their missing letters.
    for (bool moore : {false, true})
      for (bool partial : {false, true})
        for (bool output_control : {false, true}) {
          context = "timing: moore=" + std::to_string (moore) +
                    " partial=" + std::to_string (partial) +
                    " output_control=" + std::to_string (output_control);
          route::request input;
          input.formula = formula (partial ? "G o" : "G(i <-> o)");
          input.specification = input.formula;
          input.inputs = {"i"};
          input.outputs = {"o"};
          input.moore = moore;
          if (partial) {
            auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
            const auto o = bdd_ithvar (aut->register_ap (spot::formula::ap ("o")));
            aut->new_state ();
            aut->set_init_state (0);
            aut->set_buchi ();
            aut->new_edge (0, 0, !o, {0});
            input.automaton = aut;
            input.source = route::source_language::real_worker;
            input.specification = formula ("F o");
            if (not output_control) {
              input.inputs = {"i", "o"};
              input.outputs = {"unused_output"};
            }
          }
          route::options options;
          options.verify = true;
          auto expected = route::run (input, options);
          use_antichain (options, input);
          auto actual = route::run (input, options);
          if (actual.verdict != expected.verdict)
            throw std::runtime_error ("move order or partial DBA mismatch: expected " +
                                      expected.verdict + ", got " + actual.verdict);
        }
    // Deliberately register the output before the input, reversing the
    // ordinary worker's variable layout. Both polarities still use the same game.
    for (bool moore : {false, true}) {
      route::request input;
      auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
      const auto o = bdd_ithvar (aut->register_ap (spot::formula::ap ("o")));
      const auto i = bdd_ithvar (aut->register_ap (spot::formula::ap ("i")));
      aut->new_states (2);
      aut->set_init_state (0);
      aut->set_buchi ();
      aut->new_edge (0, 0, bdd_biimp (i, o));
      aut->new_edge (0, 1, bdd_xor (i, o), {0});
      aut->new_edge (1, 1, bddtrue, {0});
      input.automaton = aut;
      input.source = route::source_language::real_worker;
      input.specification = formula ("G(i <-> o)");
      input.inputs = {"i"};
      input.outputs = {"o"};
      input.moore = moore;
      context = "reversed AP registration: moore=" + std::to_string (moore);
      route::options options;
      options.verify = true;
      auto expected = route::run (input, options);
      use_antichain (options, input);
      auto actual = route::run (input, options);
      require (actual.verdict == expected.verdict, "AP registration changed the verdict");
    }
    std::cout
        << "All four tiers: REAL/UNREAL, Mealy/Moore, decision and checked synthesis passed\n";
  } catch (const std::exception& error) {
    std::cerr << context << ": " << error.what () << '\n';
    return 1;
  }
}
