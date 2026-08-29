// The direct-simulation relation must hold in the orientation the rank-vector
// closure needs: p <= q means q simulates p, so q is at least as dangerous.
// Getting this backwards would make the closure unsound while still looking
// plausible, so the fixtures below pin the direction explicitly.
#include "solver/direct_simulation.hh"

#include <algorithm>
#include <bddx.h>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <string>

namespace {

  bool simulates (const acacia::direct_simulation::relation& r, unsigned p, unsigned q) {
    if (p >= r.simulators.size ())
      return false;
    const auto& s = r.simulators[p];
    return std::find (s.begin (), s.end (), q) != s.end ();
  }

  bool expect (bool condition, const std::string& what) {
    if (not condition)
      std::cerr << "FAILED: " << what << '\n';
    return condition;
  }

  // 0 --a--> 1 (sink, loops on everything)
  // 0 --a--> 2 (sink, loops only on b)
  // State 1 accepts strictly more continuations than 2, so 2 <= 1 and not 1 <= 2.
  bool check_strict_one_way () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_buchi ();
    const bdd b = bdd_ithvar (aut->register_ap ("b"));
    aut->new_states (3);
    aut->set_init_state (0U);
    aut->new_edge (0, 1, bddtrue);
    aut->new_edge (0, 2, bddtrue);
    aut->new_edge (1, 1, bddtrue);
    aut->new_edge (2, 2, b);
    const auto r = acacia::direct_simulation::compute (aut);
    bool ok = expect (r.computed, "relation computed");
    ok &= expect (simulates (r, 2, 1), "2 <= 1: the more permissive sink simulates the narrower one");
    ok &= expect (not simulates (r, 1, 2), "not 1 <= 2: the narrower sink cannot simulate");
    return ok;
  }

  // An accepting step cannot be simulated by a non-accepting one: nu >= mu.
  // 1 loops accepting on everything, 2 loops non-accepting on everything.
  // So 2 <= 1 but not 1 <= 2.
  bool check_acceptance_direction () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_buchi ();
    aut->new_states (3);
    aut->set_init_state (0U);
    aut->new_edge (0, 1, bddtrue);
    aut->new_edge (0, 2, bddtrue);
    aut->new_edge (1, 1, bddtrue, aut->acc ().all_sets ());
    aut->new_edge (2, 2, bddtrue);
    const auto r = acacia::direct_simulation::compute (aut);
    bool ok = expect (r.computed, "relation computed");
    ok &= expect (simulates (r, 2, 1), "2 <= 1: accepting loop simulates non-accepting loop");
    ok &= expect (not simulates (r, 1, 2),
                  "not 1 <= 2: a non-accepting step cannot match an accepting one");
    return ok;
  }

  // Two structurally identical sinks must simulate each other.
  bool check_equivalence () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_buchi ();
    aut->new_states (3);
    aut->set_init_state (0U);
    aut->new_edge (0, 1, bddtrue);
    aut->new_edge (0, 2, bddtrue);
    aut->new_edge (1, 1, bddtrue);
    aut->new_edge (2, 2, bddtrue);
    const auto r = acacia::direct_simulation::compute (aut);
    bool ok = expect (r.computed, "relation computed");
    ok &= expect (simulates (r, 1, 2) and simulates (r, 2, 1), "1 and 2 are equivalent");
    ok &= expect (r.equivalent_pairs >= 1, "at least one equivalent pair counted");
    return ok;
  }

  // A state with no outgoing edge is simulated by everything reachable-compatible,
  // and the relation must at least be reflexive-free in `simulators` (p excluded).
  bool check_self_excluded () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_buchi ();
    aut->new_states (2);
    aut->set_init_state (0U);
    aut->new_edge (0, 1, bddtrue);
    aut->new_edge (1, 1, bddtrue);
    const auto r = acacia::direct_simulation::compute (aut);
    bool ok = expect (r.computed, "relation computed");
    for (unsigned p = 0; p < r.simulators.size (); ++p)
      ok &= expect (not simulates (r, p, p), "simulators exclude the state itself");
    return ok;
  }

  // Above the cap nothing is computed, and that is reported rather than hidden.
  bool check_cap () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_buchi ();
    aut->new_states (10);
    aut->set_init_state (0U);
    for (unsigned p = 0; p + 1 < 10; ++p)
      aut->new_edge (p, p + 1, bddtrue);
    const auto r = acacia::direct_simulation::compute (aut, 5);
    return expect (not r.computed, "cap reported as not computed")
           and expect (r.states == 10, "state count still reported under the cap");
  }

}  // namespace

int main () {
  bool ok = true;
  ok &= check_strict_one_way ();
  ok &= check_acceptance_direction ();
  ok &= check_equivalence ();
  ok &= check_self_excluded ();
  ok &= check_cap ();
  return ok ? 0 : 1;
}
