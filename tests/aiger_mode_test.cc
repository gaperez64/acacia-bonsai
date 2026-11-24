#include "solver/obligation_helper.hh"
#include "solver/parity/route.hh"
#include "solver/solver_invoker.hh"
#include "utils/verbose.hh"
#include <string_view>

#include <iostream>
#include <spot/twaalgos/game.hh>
#include <stdexcept>

unsigned utils::verbose = 0;
utils::voutstream utils::vout;
namespace posets::vectors {
  size_t bool_threshold = 0;
}

int main (int argc, char** argv) {
  if (argc != 4)
    return 1;
  const std::string_view entry = argv[1];
  const std::string mode = argv[2];
  const bool synthesis = std::string_view (argv[3]) == "synthesis";
  try {
    // Empty objectives require mode rejection before parsing, eligibility or solver work.
    if (entry == "legacy")
      run_ltl ({}, {}, 1, 1, 1, "", {}, spot::postprocessor::Small, spot::postprocessor::High,
               SPOT_FAST_OFF, acacia::game_backend::backward,
               synthesis ? std::make_optional<std::string> ("unused.aag") : std::nullopt, {},
               acacia::automaton_provider::frozen_graph, acacia::candidate_mode::only, true, 0, {},
               weakening_mode::basic, nullptr, 1, false, mode);
    else if (entry == "obligation")
      acacia::obligation::solve ({}, {}, {}, acacia::obligation::timing::mealy, synthesis,
                                 acacia::obligation::conversion::none, mode);
    else if (entry == "parity" || entry == "parity-dpa") {
      const auto solve =
          entry == "parity" ? acacia::parity_route::run : acacia::parity_route::run_positive_dpa;
      solve ({}, {.synthesis = synthesis, .aiger_mode = mode}, {});
    }
    else if (entry == "spot") {
      auto machine = spot::make_twa_graph (spot::make_bdd_dict ());
      machine->new_state ();
      machine->set_init_state (0);
      machine->set_acceptance (0, spot::acc_cond::acc_code::t ());
      const auto i = bdd_ithvar (machine->register_ap ("i"));
      const auto o = bdd_ithvar (machine->register_ap ("o"));
      spot::set_synthesis_outputs (machine, o);
      machine->new_edge (0, 0, i & o);
      machine->new_edge (0, 0, (!i) & (!o));
      const auto circuit = spot::mealy_machine_to_aig (machine, mode.c_str (), {"i"}, {"o"});
      if (!circuit || circuit->num_inputs () != 1 || circuit->num_outputs () != 1)
        return 1;
      spot::print_aiger (std::cout, circuit);
    }
    else
      return 1;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
