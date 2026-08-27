#include "boolean_states/forward_saturation.hh"
#include "solver/acceptance_core.hh"
#include "solver/create_automaton.hh"
#include "utils/verbose.hh"

#include <bddx.h>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace {

  bool expect_census (std::string_view name,
                      const acacia::acceptance_core::census& got,
                      size_t local_core, size_t global_core,
                      size_t accepting_sccs, size_t states) {
    if (got.local_core == local_core and got.global_core == global_core and
        got.accepting_sccs == accepting_sccs and got.states == states)
      return true;

    std::cerr << name << ": expected local=" << local_core
              << ", global=" << global_core
              << ", accepting_sccs=" << accepting_sccs
              << ", states=" << states
              << "; got local=" << got.local_core
              << ", global=" << got.global_core
              << ", accepting_sccs=" << got.accepting_sccs
              << ", states=" << got.states << '\n';
    return false;
  }

  spot::twa_graph_ptr make_buchi (unsigned states) {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->set_buchi ();
    aut->new_states (states);
    if (states != 0)
      aut->set_init_state (0);
    return aut;
  }

  bool hand_built_cases () {
    bool ok = true;

    {
      auto aut = make_buchi (1);
      aut->new_edge (0, 0, bddtrue, aut->acc ().mark (0));
      ok &= expect_census ("single accepting self-loop",
                           acacia::acceptance_core::compute (aut), 1, 1, 1, 1);
    }

    {
      auto aut = make_buchi (2);
      aut->new_edge (0, 0, bddtrue, aut->acc ().mark (0));
      aut->new_edge (0, 1, bddtrue, aut->acc ().mark (0));
      aut->new_edge (1, 1, bddtrue);
      ok &= expect_census ("accepting core reaches nonaccepting sink",
                           acacia::acceptance_core::compute (aut), 1, 2, 1, 2);
    }

    {
      auto aut = make_buchi (2);
      aut->new_edge (0, 1, bddtrue);
      aut->new_edge (0, 1, bddtrue, aut->acc ().mark (0));
      aut->new_edge (1, 0, bddtrue);
      ok &= expect_census ("parallel accepting and nonaccepting edges",
                           acacia::acceptance_core::compute (aut), 2, 2, 1, 2);
    }

    {
      auto aut = make_buchi (4);
      aut->new_edge (0, 1, bddtrue);
      aut->new_edge (1, 0, bddtrue);
      aut->new_edge (1, 2, bddtrue);
      aut->new_edge (2, 3, bddtrue, aut->acc ().mark (0));
      aut->new_edge (3, 2, bddtrue);
      ok &= expect_census ("nonaccepting cycle feeds accepting cycle",
                           acacia::acceptance_core::compute (aut), 2, 2, 1, 4);
    }

    {
      auto aut = make_buchi (2);
      aut->new_edge (0, 1, bddtrue);
      aut->new_edge (1, 0, bddtrue);
      ok &= expect_census ("no accepting SCC",
                           acacia::acceptance_core::compute (aut), 0, 0, 0, 2);
    }

    {
      auto aut = make_buchi (2);
      aut->new_edge (0, 1, bddtrue, aut->acc ().mark (0));
      ok &= expect_census ("accepting incoming edge into trivial SCC",
                           acacia::acceptance_core::compute (aut), 0, 0, 0, 2);
    }

    {
      auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
      aut->set_generalized_buchi (2);
      aut->new_states (2);
      aut->set_init_state (0);
      aut->new_edge (0, 1, bddtrue, aut->acc ().mark (0));
      aut->new_edge (1, 0, bddtrue, aut->acc ().mark (1));
      ok &= expect_census ("generalized Buchi marks cover the SCC",
                           acacia::acceptance_core::compute (aut), 2, 2, 1, 2);
    }

    {
      auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
      aut->set_acceptance (spot::acc_cond::acc_code::t ());
      aut->new_state ();
      aut->set_init_state (0);
      aut->new_edge (0, 0, bddtrue);
      ok &= expect_census ("zero acceptance sets",
                           acacia::acceptance_core::compute (aut), 0, 0, 0, 1);
    }

    {
      auto aut = make_buchi (0);
      ok &= expect_census ("empty automaton",
                           acacia::acceptance_core::compute (aut), 0, 0, 0, 0);
    }

    return ok;
  }

  bool acacia_translation_agreement () {
    const std::vector<std::string> formulas {
      "GF a",
      "FG a",
      "G(a -> F b)",
      "a U b",
      "GF a & GF b",
      "G F (a & X b)",
      "F G (a | b)",
      "G a",
    };
    bool ok = true;

    for (const std::string& text : formulas) {
      spot::parsed_formula parsed = spot::parse_infix_psl (text);
      if (parsed.format_errors (std::cerr) or not parsed.f) {
        std::cerr << "failed to parse agreement formula: " << text << '\n';
        ok = false;
        continue;
      }

      auto dictionary = spot::make_bdd_dict ();
      spot::translator translator (dictionary);
      spot::formula formula = parsed.f;
      auto aut = create_automaton (formula, translator);
      const auto census = acacia::acceptance_core::compute (aut);
      const size_t saturation =
          boolean_states::forward_saturation::make (aut, 10) ();
      if (census.global_core != saturation) {
        std::cerr << "agreement mismatch for " << text
                  << ": census global_core=" << census.global_core
                  << ", forward_saturation=" << saturation << '\n';
        ok = false;
      }
    }
    return ok;
  }

}  // namespace

int main () {
  bool ok = hand_built_cases ();
  ok &= acacia_translation_agreement ();
  return ok ? 0 : 1;
}
