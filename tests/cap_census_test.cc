#include "aut_preprocessors/cap_census.hh"

#include <bddx.h>
#include <spot/twa/twagraph.hh>

#include <iostream>
#include <string>
#include <vector>

namespace {

  bool expect (const std::string& name, bool condition) {
    if (condition)
      return true;
    std::cerr << "failed: " << name << '\n';
    return false;
  }

  spot::twa_graph_ptr make_aut () {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->prop_state_acc (true);
    aut->new_states (12);
    aut->set_init_state (0);

    aut->new_acc_edge (0, 1, bddtrue, false);
    aut->new_acc_edge (0, 5, bddtrue, false);
    aut->new_acc_edge (0, 8, bddtrue, false);

    aut->new_acc_edge (1, 2, bddtrue, false);
    aut->new_acc_edge (2, 1, bddtrue);
    aut->new_acc_edge (2, 3, bddtrue);
    aut->new_acc_edge (3, 4, bddtrue);
    aut->new_acc_edge (4, 4, bddtrue, false);

    aut->new_acc_edge (5, 6, bddtrue);
    aut->new_acc_edge (6, 7, bddtrue);
    aut->new_acc_edge (7, 7, bddtrue, false);

    aut->new_acc_edge (8, 9, bddtrue, false);
    aut->new_acc_edge (9, 8, bddtrue, false);
    aut->new_acc_edge (9, 10, bddtrue, false);
    aut->new_acc_edge (10, 11, bddtrue);
    aut->new_acc_edge (11, 11, bddtrue, false);
    return aut;
  }

}  // namespace

int main () {
  const auto report = aut_preprocessors::future_visit_cap_census (make_aut (), 4);
  const std::vector<unsigned> expected {4, 4, 4, 1, 0, 2, 1, 0, 1, 1, 1, 0};

  bool ok = true;
  ok &= expect ("per-state caps", report.caps == expected);
  ok &= expect ("states at K", report.states_at_k == 3);
  ok &= expect ("positive finite states", report.finite_states == 6);
  ok &= expect ("zero states", report.zero_states == 3);
  ok &= expect ("currently counting states", report.counting_states == 4);
  ok &= expect ("finite currently counting states", report.finite_counting_states == 2);
  return ok ? 0 : 1;
}
