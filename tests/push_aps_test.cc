#include "utils/push_aps.hh"

#include <bddx.h>
#include <iostream>
#include <spot/twa/twagraph.hh>
#include <string>

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

  bool observations_preserve_graph () {
    const auto aut = make_chain (3);
    utils::push_aps_observation observed;
    const auto plain = utils::push_aps (aut, bddtrue, bddtrue);
    const auto counted = utils::push_aps (aut, bddtrue, bddtrue, 10, 10, &observed);
    bool ok = expect ("observed graph size", plain->num_states () == counted->num_states ());
    for (unsigned q = 0; q < plain->num_states (); ++q) {
      const auto& a = *plain->out (q).begin ();
      const auto& b = *counted->out (q).begin ();
      ok &= expect ("observed graph transition",
                    a.dst == b.dst && a.cond == b.cond && a.acc == b.acc);
    }
    ok &= expect ("key and partition counts",
                  observed.interned_keys == 3 && observed.expanded_keys == 3 &&
                      observed.partition_computations == 3 && observed.sources.size () == 3 &&
                      observed.emitted_tuples == 3);
    utils::push_aps_observation limited;
    ok &= expect ("observed limit", !utils::push_aps (aut, bddtrue, bddtrue, 2, 10, &limited) &&
                                        std::string (limited.limit_reason) == "states");
    return ok;
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
  ok &= observations_preserve_graph ();
  return ok ? 0 : 1;
}
