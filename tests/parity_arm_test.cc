#include "phase_records.hh"
#include "solver/parity/route.hh"
#include "utils/verbose.hh"
#include <spot/twaalgos/mealy_machine.hh>

#include <fstream>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/synthesis.hh>
#include <spot/twaalgos/translate.hh>
#include <sstream>
#include <stdexcept>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;

namespace route = acacia::parity_route;

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

void check_reductions () {
  for (auto reduction : {route::mealy_reduction::none, route::mealy_reduction::bisim,
                         route::mealy_reduction::bisim_output, route::mealy_reduction::reselect}) {
    auto machine = spot::make_twa_graph (spot::make_bdd_dict ());
    const auto i = bdd_ithvar (machine->register_ap (spot::formula::ap ("i")));
    const auto o = bdd_ithvar (machine->register_ap (spot::formula::ap ("o")));
    spot::set_synthesis_outputs (machine, o);
    machine->new_states (2);
    machine->set_init_state (0);
    machine->new_edge (0, 1, !i);
    machine->new_edge (0, 0, i & o);
    machine->new_edge (1, 1, (!i) & (!o));
    machine->new_edge (1, 0, i & o);
    const auto original = spot::split_separated_mealy (machine);
    route::request input;
    input.inputs = {"i"};
    input.outputs = {"o"};
    route::options config;
    config.reduction = reduction;
    route::result incumbent;
    incumbent.controller = machine;
    incumbent.circuit = spot::mealy_machine_to_aig (machine, "isop", input.inputs, input.outputs);
    const auto reduce = [&] {
      incumbent.reduced_aiger.clear ();
      route::reduce_controller (incumbent, input, config,
                                acacia::phase_clock (CLOCK_MONOTONIC) + 200000000);
    };
    reduce ();
    require (machine->num_states () == 2, "reduction mutated the incumbent");
    require (incumbent.reduced_aiger.empty () == (reduction == route::mealy_reduction::none ||
                                                  reduction == route::mealy_reduction::bisim),
             "output assignment did not improve the controller");
    const auto circuit = incumbent.reduced_aiger.empty ()
                             ? incumbent.circuit
                             : spot::aig::parse_aag (incumbent.reduced_aiger.c_str (), "reduced",
                                                     machine->get_dict ());
    auto behavior = circuit->as_automaton (false);
    require (spot::is_split_mealy_specialization (original, spot::split_2step (behavior)),
             "reduction did not specialize the original controller");
    check_controller (circuit, formula ("G(i -> o)"), false);
    if (reduction == route::mealy_reduction::bisim_output) {
      const auto accepted = incumbent.reduced_aiger;
      for (unsigned failure = 0; failure < 4; ++failure) {
        config.adapt_circuit = [&, failure] (auto) -> spot::aig_ptr {
          if (failure == 0)
            throw std::bad_alloc ();
          if (failure == 1)
            throw std::runtime_error ("optional reduction failed");
          if (failure == 2)
            raise (SIGKILL);
          return incumbent.circuit;
        };
        route::reduce_controller (incumbent, input, config,
                                  acacia::phase_clock (CLOCK_MONOTONIC) + 200000000);
        require (incumbent.reduced_aiger == accepted,
                 "failure or larger result lost the best circuit");
      }
      config.adapt_circuit = {};
    }
    if (reduction == route::mealy_reduction::none)
      continue;
    // Each size guard must preserve the original graph, including unreachable states.
    machine->new_states (513 - machine->num_states ());
    auto edges = machine->num_edges ();
    reduce ();
    require (incumbent.reduced_aiger.empty (), "size cap replaced the circuit");
    require (machine->num_states () == 513 && machine->num_edges () == edges,
             "state cap changed the controller");
    machine->purge_unreachable_states ();
    while (machine->num_edges () <= 8192)
      machine->new_edge (0, 0, bddtrue);
    reduce ();
    require (incumbent.reduced_aiger.empty (), "size cap replaced the circuit");
    require (machine->num_edges () == 8193, "edge cap changed the controller");
    auto labels = spot::make_twa_graph (machine->get_dict ());
    labels->copy_ap_of (machine);
    spot::set_synthesis_outputs (labels, o);
    labels->new_states (2);
    labels->set_init_state (0);
    bdd condition = o;
    for (unsigned bit = 0; bit < 16; ++bit)
      condition &= bdd_ithvar (labels->register_ap ("v" + std::to_string (bit)));
    for (unsigned edge = 0; edge < 4096; ++edge)
      labels->new_edge (0, 1, condition);
    incumbent.controller = labels;
    reduce ();
    require (incumbent.reduced_aiger.empty (), "BDD cap replaced the circuit");
    require (labels->num_states () == 2 && labels->num_edges () == 4096,
             "BDD node cap changed the controller");
  }
}

void check_aiger_structure () {
  route::request input;
  input.inputs = {"i"};
  input.outputs = {"o"};
  const std::string valid = "aag 3 1 1 1 1\n2\n4 6\n6\n6 2 4\ni0 i\no0 o";
  require (route::checked_aiger_size (valid, input) == 2, "valid AIGER rejected");
  for (auto text :
       {"", "aag 999999999 1 1 1 1\n", "aag 1 1 0 1 0\n2\n4\ni0 i\no0 o",
        "aag 2 1 0 1 1\n2\n4\n4 4 2\ni0 i\no0 o", "aag 1 1 0 1 0\n2\n2\ni0 i\no0 wrong"})
    require (!route::checked_aiger_size (text, input), "invalid AIGER accepted");
  require (!route::checked_aiger_size (valid + "\nextra", input), "trailing AIGER accepted");
  require (!route::checked_aiger_size (std::string (1024 * 1024 + 1, 'x'), input),
           "oversized AIGER accepted");
  input.inputs = {"z", "a"};
  input.outputs = {"p", "q"};
  const std::string reordered = "aag 2 2 0 2 0\n2\n4\n2\n4\ni0 a\ni1 z\no0 q\no1 p";
  require (route::checked_aiger_size (reordered, input) == 0, "reordered signals rejected");
  require (!route::checked_aiger_size ("aag 2 2 0 2 0\n2\n4\n2\n4\ni0 a\ni1 a\no0 q\no1 p", input),
           "duplicate signals accepted");
}

// All states are reachable and input complete, yet the nested signatures explode in flatten_aux.
route::request nested_outputs () {
  route::request input;
  auto machine = spot::make_twa_graph (spot::make_bdd_dict ());
  machine->new_states (41);
  machine->set_init_state (0);
  std::vector<bdd> selector;
  for (unsigned bit = 0; bit < 6; ++bit) {
    input.inputs.push_back ("i" + std::to_string (bit));
    selector.push_back (bdd_ithvar (machine->register_ap (input.inputs.back ())));
  }
  bdd outputs = bddtrue, nested = bddtrue, covered = bddfalse;
  for (unsigned n = 0; n < 40; ++n) {
    input.outputs.push_back ("o" + std::to_string (n));
    nested &= bdd_ithvar (machine->register_ap (input.outputs.back ()));
    outputs = nested;
    machine->new_edge (n + 1, 0, nested);
    bdd condition = bddtrue;
    for (unsigned bit = 0; bit < selector.size (); ++bit)
      condition &= (n & (1u << bit)) ? selector[bit] : !selector[bit];
    machine->new_edge (0, n + 1, condition);
    covered |= condition;
  }
  machine->new_edge (0, 1, !covered);
  spot::set_synthesis_outputs (machine, outputs);
  input.automaton = machine;
  return input;
}

void check_budget_fallback (const std::string& path) {
  auto input = nested_outputs ();
  route::result incumbent;
  incumbent.controller = input.automaton;
  incumbent.circuit =
      spot::mealy_machine_to_aig (input.automaton, "isop", input.inputs, input.outputs);
  unsigned nodes = 0;
  for (const auto& edge : input.automaton->edges ())
    nodes += bdd_nodecount (edge.cond);
  require (input.automaton->num_states () < 512 && input.automaton->num_edges () < 8192 &&
               nodes < 65536,
           "adversarial fixture exceeds size caps");
  route::options config;
  acacia::worker_record worker;
  acacia::active_worker_record () = &worker;
  rlimit before {}, after {};
  require (getrlimit (RLIMIT_AS, &before) == 0, "cannot read parent limit");
  const auto start = acacia::phase_clock (CLOCK_MONOTONIC);
  route::reduce_controller (incumbent, input, config, start + 50000000);
  require (acacia::phase_clock (CLOCK_MONOTONIC) - start < 500000000,
           "adversarial reduction did not return within its budget");
  require (incumbent.reduced_aiger.empty () && incumbent.controller == input.automaton &&
               input.automaton->num_states () == 41,
           "timeout lost the incumbent");
  require (getrlimit (RLIMIT_AS, &after) == 0 && before.rlim_cur == after.rlim_cur &&
               before.rlim_max == after.rlim_max && !worker.stopped && worker.stops == 0 &&
               worker.descendant_pid == -1,
           "child contaminated parent limits or cause");
  acacia::active_worker_record () = nullptr;
  if (!path.empty ()) {
    std::ofstream file (path);
    spot::print_aiger (file, incumbent.circuit);
  }
}

void check_reselection (const std::string& path) {
  route::request input;
  input.inputs = {"i"};
  input.outputs = {"o"};
  input.automaton = spot::make_twa_graph (spot::make_bdd_dict ());
  auto aut = input.automaton;
  const auto o = bdd_ithvar (aut->register_ap ("o"));
  aut->new_states (2);
  aut->set_init_state (0);
  aut->set_acceptance (2, spot::acc_cond::acc_code::parity (true, true, 2));
  aut->new_edge (0, 1, !o, {0});
  aut->new_edge (0, 0, o, {1});
  aut->new_edge (1, 0, !o, {0});
  aut->new_edge (1, 0, o, {1});
  route::options config;
  // Seed the other winning positional strategy: alternate !o, o instead of constant o.
  config.on_game = [o] (const acacia::parity::arena_game& game) {
    auto arena = game.arena;
    auto strategy = spot::get_strategy (arena);
    for (unsigned state = 0; state < 2; ++state) {
      const auto player = game.arena->out (state).begin ()->dst;
      bool selected = false;
      for (const auto& edge : game.arena->out (player))
        if ((edge.cond & (state ? o : !o)) != bddfalse) {
          strategy[player] = game.arena->edge_number (edge);
          selected = true;
          break;
        }
      require (selected, "missing fixture strategy edge");
    }
    spot::set_strategy (arena, std::move (strategy));
  };
  config.reduction = route::mealy_reduction::none;
  const auto unreduced = route::run_positive_dpa (input, config);
  config.reduction = route::mealy_reduction::reselect;
  config.deadline_mono_ns = 1;
  const auto expired = route::run_positive_dpa (input, config);
  std::ostringstream expected, actual;
  spot::print_aiger (expected, unreduced.circuit);
  spot::print_aiger (actual, expired.circuit);
  require (expired.reduced_aiger.empty () && actual.str () == expected.str (),
           "expired optional budget changed the none controller");
  config.deadline_mono_ns = 0;
  config.reduction = route::mealy_reduction::bisim_output;
  auto incumbent = route::run_positive_dpa (input, config);
  config.reduction = route::mealy_reduction::reselect;
  auto improved = route::run_positive_dpa (input, config);
  const auto size = [&] (const route::result& value) {
    return value.reduced_aiger.empty ()
               ? value.circuit->num_gates () + value.circuit->num_latches ()
               : route::checked_aiger_size (value.reduced_aiger, input).value ();
  };
  require (!improved.reduced_aiger.empty () && size (improved) < size (incumbent),
           "reselection must replace the incumbent");
  require (size (incumbent) > 0 && size (improved) == 0, "unexpected fixture circuit sizes");
  if (!path.empty ()) {
    std::ofstream file (path);
    file << improved.reduced_aiger;
    std::ofstream original (path + ".incumbent");
    if (incumbent.reduced_aiger.empty ())
      spot::print_aiger (original, incumbent.circuit);
    else
      original << incumbent.reduced_aiger;
  }
}

int main (int argc, char** argv) {
  try {
    if (argc == 3 && std::string (argv[1]) == "--budget-fallback") {
      acacia::phase_records_start_writer ();
      check_budget_fallback (argv[2]);
      acacia::phase_records_close ();
      int status;
      require (waitpid (-1, &status, WNOHANG) == -1 && errno == ECHILD, "child was not reaped");
      return 0;
    }
    if (argc == 3 && std::string (argv[1]) == "--reselection") {
      acacia::phase_records_start_writer ();
      check_reselection (argv[2]);
      acacia::phase_records_close ();
      return 0;
    }
    if (argc == 5 && std::string (argv[1]) == "--check-controller") {
      auto circuit = spot::aig::parse_aag (std::string (argv[2]));
      check_controller (circuit, formula (argv[3]), std::string (argv[4]) == "moore");
      return 0;
    }
    check_aiger_structure ();
    check_reductions ();
    check_budget_fallback ("");
    check_reselection ("");
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
            exported |= std::string (stage) == "strategy" ||
                        std::string (stage) == "reduce-controller" ||
                        std::string (stage) == "encode-controller";
            verification |= std::string (stage).starts_with ("verify-");
          });
          require (published && !exported && !verification && !answer.circuit,
                   "decision allocated a controller or checked a strategy");
          require (answer.tier == tier, "fixture missed its tier");
          require (answer.verdict == (real ? "REALIZABLE" : "UNREALIZABLE"),
                   "decision disagrees with the fixture oracle");
          require (!answer.game && answer.solution.winner.empty (),
                   "production decision retained an explicit game or winning region");
          bool selected = false;
          auto synthesis = decision;
          synthesis.synthesis = true;
          synthesis.on_verdict = [&] (bool winner) {
            require (winner == real, "synthesis published wrong verdict");
            selected = true;
          };
          auto made = route::run (input, synthesis, [&] (const char* stage, const auto&, double) {
            if (std::string (stage) == "strategy" || std::string (stage) == "reduce-controller" ||
                std::string (stage) == "encode-controller")
              require (selected, "controller construction preceded selection");
          });
          require (selected && made.verdict == answer.verdict && bool (made.circuit) == real &&
                       !made.game && made.solution.winner.empty (),
                   "production synthesis changed the verdict or retained diagnostics");
          if (real)
            check_controller (made.reduced_aiger.empty ()
                                  ? made.circuit
                                  : spot::aig::parse_aag (made.reduced_aiger.c_str (), "reduced"),
                              input.specification, moore);
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
          route::options diagnostic;
          diagnostic.verify = true;
          auto checked = route::run (input, diagnostic);
          require (checked.verdict == answer.verdict && checked.checked,
                   "production decision differs from the checked route");
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
        if (std::string (stage) == "strategy" || std::string (stage) == "reduce-controller" ||
            std::string (stage) == "encode-controller")
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
