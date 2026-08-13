#include "utils/push_aps.hh"

#include <iostream>
#include <string>

#include <bddx.h>
#include <spot/twa/twagraph.hh>

namespace {

  bool expect (const std::string& name, bool condition) {
    if (condition)
      return true;
    std::cerr << "failed: " << name << '\n';
    return false;
  }

  spot::twa_graph_ptr make_chain (unsigned states) {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_states (states);
    aut->set_init_state (0);
    for (unsigned state = 1; state < states; ++state)
      aut->new_edge (state - 1, state, bddtrue);
    aut->new_edge (states - 1, states - 1, bddtrue);
    return aut;
  }

  bool handles_deep_graph_without_recursion () {
    constexpr unsigned states = 20000;
    const auto pushed = utils::push_aps (make_chain (states), bddtrue, bddtrue);
    return expect ("deep graph completed", pushed != nullptr) and
           expect ("deep graph state count", pushed->num_states () == states) and
           expect ("deep graph initial state", pushed->get_init_state_number () == 0);
  }

  bool calls_are_independent () {
    const auto first = utils::push_aps (make_chain (2), bddtrue, bddtrue);
    const auto second = utils::push_aps (make_chain (3), bddtrue, bddtrue);
    return expect ("both calls completed", first != nullptr and second != nullptr) and
           expect ("first call state count", first->num_states () == 2) and
           expect ("second call state count", second->num_states () == 3);
  }

  bool reports_expansion_limit () {
    const auto pushed = utils::push_aps (make_chain (3), bddtrue, bddtrue, 2, 10);
    return expect ("state budget returns no automaton", pushed == nullptr);
  }

}  // namespace

int main () {
  bool ok = true;
  ok &= handles_deep_graph_without_recursion ();
  ok &= calls_are_independent ();
  ok &= reports_expansion_limit ();
  return ok ? 0 : 1;
}
