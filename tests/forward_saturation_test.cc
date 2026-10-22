#include "boolean_states/forward_saturation.hh"

#include <iostream>
#include <string_view>
#include <tuple>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace {
  spot::twa_graph_ptr make_buchi (unsigned states) {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->set_buchi ();
    aut->prop_state_acc (true);
    aut->new_states (states);
    if (states)
      aut->set_init_state (0);
    return aut;
  }

  bool check (std::string_view name, const spot::twa_graph_ptr& aut,
              const std::vector<bool>& expected) {
    bool ok = boolean_states::detail::unbounded_states (aut) == expected;
    const unsigned count = std::count (expected.begin (), expected.end (), true);
    std::vector<unsigned> rename (expected.size ());
    unsigned unbounded = 0, bounded = count;
    for (unsigned src = 0; src < expected.size (); ++src)
      rename[src] = expected[src] ? unbounded++ : bounded++;

    using edge = std::tuple<unsigned, unsigned, int, spot::acc_cond::mark_t>;
    std::vector<edge> wanted, got;
    for (unsigned src = 0; src < expected.size (); ++src)
      for (const auto& e : aut->out (src))
        wanted.emplace_back (rename[src], rename[e.dst], e.cond.id (),
                             expected[src] ? e.acc : spot::acc_cond::mark_t {});
    const unsigned init = expected.empty () ? 0 : rename[aut->get_init_state_number ()];
    ok &= boolean_states::forward_saturation::make (aut, 10) () == count;
    if (not expected.empty ()) {
      ok &= aut->get_init_state_number () == init;
      for (unsigned src = 0; src < expected.size (); ++src)
        for (const auto& e : aut->out (src))
          got.emplace_back (src, e.dst, e.cond.id (), e.acc);
    }
    std::ranges::sort (wanted);
    std::ranges::sort (got);
    ok &= got == wanted;
    if (not ok)
      std::cerr << name << ": mask, stable renaming or acceptance mismatch\n";
    return ok;
  }
}

int main () {
  bool ok = true;
  {
    auto aut = make_buchi (6);
    aut->new_edge (0, 3, bddtrue);
    aut->new_edge (1, 2, bddtrue, {0});
    aut->new_edge (2, 1, bddtrue);
    aut->new_edge (2, 4, bddtrue);
    aut->new_edge (3, 1, bddtrue, {0});
    aut->new_edge (4, 4, bddtrue);
    aut->new_edge (5, 5, bddtrue);
    ok &= check ("accepting cycle and downstream sink, bounded accepting predecessor",
                 aut, {false, true, true, false, true, false});
  }
  {
    auto aut = make_buchi (5);
    aut->new_edge (0, 0, bddtrue);
    aut->new_edge (1, 2, bddtrue, {0});
    aut->new_edge (2, 1, bddtrue);
    aut->new_edge (2, 3, bddtrue);
    aut->new_edge (4, 4, bddtrue);
    ok &= check ("unreachable accepting cycle and dead-end descendant",
                 aut, {false, true, true, true, false});
  }
  {
    auto aut = make_buchi (3);
    aut->new_edge (0, 0, bddtrue);
    aut->new_edge (1, 1, bddtrue, {0});
    aut->new_edge (1, 2, bddtrue, {0});
    ok &= check ("unreachable accepting singleton self-loop", aut, {false, true, true});
  }
  {
    auto aut = make_buchi (3);
    aut->new_edge (0, 1, bddtrue, {0});
    aut->new_edge (1, 2, bddtrue);
    aut->new_edge (2, 1, bddtrue);
    ok &= check ("accepting initial transient retains its extra count", aut, {false, true, true});
  }
  {
    auto aut = make_buchi (4);
    aut->new_edge (0, 1, bddtrue);
    aut->new_edge (1, 2, bddtrue, {0});
    aut->new_edge (2, 3, bddtrue);
    aut->new_edge (3, 2, bddtrue);
    ok &= check ("noninitial accepting transient before nonaccepting cycle",
                 aut, {false, false, false, false});
  }
  {
    auto aut = make_buchi (4);
    aut->new_edge (0, 1, bddtrue, {0});
    aut->new_edge (1, 2, bddtrue, {0});
    aut->new_edge (2, 3, bddtrue);
    ok &= check ("initial count along an accepting chain", aut, {false, false, true, true});
  }
  {
    auto aut = make_buchi (5);
    aut->new_edge (0, 1, bddtrue, {0});
    aut->new_edge (1, 2, bddtrue, {0});
    aut->new_edge (3, 4, bddtrue, {0});
    ok &= check ("unreachable accepting transient still contributes to the threshold",
                 aut, {false, false, false, false, false});
  }
  {
    auto aut = make_buchi (3);
    aut->new_edge (0, 0, bddfalse, {0});
    aut->new_edge (0, 1, bddfalse, {0});
    aut->new_edge (1, 2, bddtrue);
    ok &= check ("false guards retain the old structural classification", aut, {true, true, true});
  }
  ok &= check ("single dead end", make_buchi (1), {false});
  ok &= check ("empty automaton", make_buchi (0), {});
  return ok ? 0 : 1;
}
