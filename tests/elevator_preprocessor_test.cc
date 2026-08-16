#include "aut_preprocessors/elevator.hh"
#include "utils/verbose.hh"

#include <bddx.h>
#include <spot/twa/twagraph.hh>

#include <iostream>
#include <string>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace {

  spot::twa_graph_ptr make_aut (unsigned states) {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->prop_state_acc (true);
    aut->new_states (states);
    aut->set_init_state (0);
    return aut;
  }

  unsigned count_edges_from (const spot::twa_graph_ptr& aut, unsigned state) {
    unsigned count = 0;
    for ([[maybe_unused]] const auto& e : aut->out (state))
      ++count;
    return count;
  }

  bool has_edge (const spot::twa_graph_ptr& aut, unsigned src, unsigned dst, bdd cond) {
    for (const auto& e : aut->out (src))
      if (e.dst == dst and e.cond == cond)
        return true;
    return false;
  }

  bool expect (std::string name, bool condition) {
    if (condition)
      return true;

    std::cerr << "failed: " << name << '\n';
    return false;
  }

  void run_elevator (spot::twa_graph_ptr& aut, bdd inputs = bddtrue,
                     bdd outputs = bddtrue) {
    aut_preprocessors::elevator::make (aut, inputs, outputs, 10) ();
  }

  bool winning_closed_scc_becomes_safe_trap () {
    auto aut = make_aut (1);
    aut->new_acc_edge (0, 0, bddtrue, false);

    run_elevator (aut);

    return expect ("winning SCC keeps original plus safe trap", aut->num_states () == 2) and
           expect ("old state is non-accepting", not aut->state_is_accepting (0u)) and
           expect ("safe trap is non-accepting", not aut->state_is_accepting (1u)) and
           expect ("old state points to safe trap", has_edge (aut, 0, 1, bddtrue)) and
           expect ("safe trap loops", has_edge (aut, 1, 1, bddtrue));
  }

  bool losing_closed_scc_becomes_accepting_trap () {
    auto aut = make_aut (1);
    aut->new_acc_edge (0, 0, bddtrue);

    run_elevator (aut);

    return expect ("losing SCC remains one state", aut->num_states () == 1) and
           expect ("losing trap is accepting", aut->state_is_accepting (0u)) and
           expect ("losing trap loops", has_edge (aut, 0, 0, bddtrue));
  }

  bool strategy_dependent_winning_scc_is_skipped () {
    auto aut = make_aut (2);
    int o_num = aut->register_ap ("o");
    bdd o = bdd_ithvar (o_num);

    aut->new_acc_edge (0, 0, !o, false);
    aut->new_acc_edge (0, 1, o, false);
    aut->new_acc_edge (1, 0, !o);
    aut->new_acc_edge (1, 1, o);

    run_elevator (aut, bddtrue, o);

    return expect ("strategy-dependent winning SCC not collapsed", aut->num_states () == 2) and
           expect ("accepting state remains", aut->state_is_accepting (1u)) and
           expect ("controller-safe edge remains", has_edge (aut, 0, 0, !o)) and
           expect ("accepting edge remains", has_edge (aut, 0, 1, o));
  }

  bool nondeterministic_closed_scc_is_skipped () {
    auto aut = make_aut (2);
    aut->new_acc_edge (0, 0, bddtrue, false);
    aut->new_acc_edge (0, 1, bddtrue, false);
    aut->new_acc_edge (1, 0, bddtrue, false);

    run_elevator (aut);

    return expect ("nondeterministic SCC not collapsed", aut->num_states () == 2) and
           expect ("both overlapping edges remain", count_edges_from (aut, 0) == 2);
  }

  bool nonclosed_deterministic_scc_is_skipped () {
    auto aut = make_aut (2);
    int a_num = aut->register_ap ("a");
    bdd a = bdd_ithvar (a_num);

    aut->new_acc_edge (0, 0, a, false);
    aut->new_acc_edge (0, 1, !a, false);
    aut->new_acc_edge (1, 1, bddtrue, false);

    run_elevator (aut, a);

    return expect ("non-closed source SCC is not collapsed", aut->num_states () == 3) and
           expect ("source keeps self edge", has_edge (aut, 0, 0, a)) and
           expect ("source keeps exit edge", has_edge (aut, 0, 1, !a)) and
           expect ("closed target got safe trap", has_edge (aut, 1, 2, bddtrue));
  }

}  // namespace

int main () {
  bool ok = true;

  ok &= winning_closed_scc_becomes_safe_trap ();
  ok &= losing_closed_scc_becomes_accepting_trap ();
  ok &= strategy_dependent_winning_scc_is_skipped ();
  ok &= nondeterministic_closed_scc_is_skipped ();
  ok &= nonclosed_deterministic_scc_is_skipped ();

  return ok ? 0 : 1;
}
