#include "solver/create_automaton.hh"
#include "solver/spot_nba_fastpath.hh"
#include "utils/verbose.hh"

#include <bddx.h>
#include <spot/misc/optionmap.hh>
#include <spot/tl/parse.hh>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace {

  using acacia::spot_fastpath::nba_fast_class;

  bool expect_class (std::string name, nba_fast_class got, nba_fast_class expected) {
    if (got == expected)
      return true;

    std::cerr << name << ": expected "
              << acacia::spot_fastpath::detail::class_name (expected)
              << ", got "
              << acacia::spot_fastpath::detail::class_name (got)
              << '\n';
    return false;
  }

  spot::twa_graph_ptr make_deterministic_buchi () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_state ();
    aut->set_init_state (0);
    aut->new_edge (0, 0, bddtrue, spot::acc_cond::mark_t {0});
    return aut;
  }

  spot::twa_graph_ptr make_nondet_gfg_buchi () {
    auto dict = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dict);
    aut->set_acceptance (1, spot::acc_cond::acc_code::buchi ());
    aut->new_states (3);
    aut->set_init_state (0);

    aut->new_edge (0, 1, bddtrue, spot::acc_cond::mark_t {0});
    aut->new_edge (0, 2, bddtrue, spot::acc_cond::mark_t {0});
    aut->new_edge (1, 1, bddtrue, spot::acc_cond::mark_t {0});
    aut->new_edge (2, 2, bddtrue, spot::acc_cond::mark_t {0});
    return aut;
  }

  nba_fast_class classify_forbidden_ltl2dba27 () {
    auto dict = spot::make_bdd_dict ();
    int owner;
    for (const std::string& ap : std::vector<std::string> {"p", "acc"})
      dict->register_proposition (spot::formula::ap (ap), &owner);

    auto parsed = spot::parse_infix_psl ("((F (G (! (p)))) <-> (G (F (acc))))",
                                         spot::default_environment::instance (),
                                         false, false);
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      std::exit (1);
    }

    spot::option_map opts;
    opts.set ("simul", 0);
    opts.set ("ba-simul", 0);
    opts.set ("det-simul", 0);
    opts.set ("tls-impl", 1);
    opts.set ("wdba-minimize", 2);

    auto forbidden = spot::formula::Not (parsed.f);
    spot::translator trans (dict, &opts);
    auto aut = create_automaton (forbidden, trans);
    auto cls = acacia::spot_fastpath::classify_nba_for_fast_path (aut);
    dict->unregister_all_my_variables (&owner);
    return cls;
  }

}  // namespace

int main () {
  bool ok = true;

  ok &= expect_class ("deterministic",
                      acacia::spot_fastpath::classify_nba_for_fast_path (
                          make_deterministic_buchi ()),
                      nba_fast_class::deterministic_buchi);

  ok &= expect_class ("nondet-gfg",
                      acacia::spot_fastpath::classify_nba_for_fast_path (
                          make_nondet_gfg_buchi ()),
                      nba_fast_class::gfg_buchi);

  ok &= expect_class ("ltl2dba27-forbidden",
                      classify_forbidden_ltl2dba27 (),
                      nba_fast_class::non_gfg_buchi);

  return ok ? 0 : 1;
}
