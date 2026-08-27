// The transition-acceptance booleanization must agree with the state-based one
// wherever both are meaningful, and must satisfy the two invariants the solver
// depends on: counting states occupy [0, threshold), and no edge entering the
// Boolean tail carries an acceptance mark.
#include "boolean_states/forward_saturation.hh"
#include "boolean_states/transition_core.hh"
#include "solver/acceptance_core.hh"
#include "solver/create_automaton.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <spot/tl/parse.hh>
#include <bddx.h>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/postproc.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/translate.hh>
#include <string>
#include <string_view>
#include <vector>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace {

  // Always state-based, whatever the build's frontend setting is: these tests
  // compare the two booleanizations, so the state-based side must exist even
  // when create_automaton is configured to keep acceptance on transitions.
  spot::twa_graph_ptr build_state_based (std::string_view text) {
    spot::parsed_formula parsed = spot::parse_infix_psl (std::string {text});
    if (not parsed.f or not parsed.errors.empty ())
      return nullptr;
    auto dictionary = spot::make_bdd_dict ();
    spot::translator translator (dictionary);
    translator.set_type (spot::postprocessor::BA);
    translator.set_pref (spot::postprocessor::Small | spot::postprocessor::SBAcc);
    auto aut = translator.run (parsed.f);
    if (aut->num_states () > 0 and not aut->prop_state_acc ().is_true ())
      aut = spot::sbacc (aut);
    return aut;
  }


  constexpr std::string_view formulas[] = {
    "GF a",
    "FG a",
    "G(a -> F b)",
    "a U b",
    "GF a & GF b",
    "G F (a & X b)",
    "F G (a | b)",
    "G a",
    "F a",
    "G(a -> X b) & GF c",
    "(GF a) -> (GF b)",
    "X X G F a",
  };


  // A state-based Buchi automaton is also a legitimate transition-based one,
  // so the two passes must select the same number of counting states.
  bool check_agreement () {
    bool ok = true;
    for (std::string_view text : formulas) {
      auto state_based = build_state_based (text);
      auto transition_based = build_state_based (text);
      if (not state_based or not transition_based) {
        std::cerr << "could not build " << text << '\n';
        ok = false;
        continue;
      }
      const size_t saturation =
          boolean_states::forward_saturation::make (state_based, 10) ();
      const size_t core =
          boolean_states::transition_core::make (transition_based, 10) ();
      if (saturation != core) {
        std::cerr << text << ": forward_saturation=" << saturation
                  << " but transition_core=" << core << '\n';
        ok = false;
      }
    }
    return ok;
  }

  // After the pass, states [0, threshold) must be exactly the counting core.
  bool check_ordering () {
    bool ok = true;
    for (std::string_view text : formulas) {
      auto aut = build_state_based (text);
      if (not aut)
        continue;
      const size_t threshold = boolean_states::transition_core::make (aut, 10) ();
      const auto census = acacia::acceptance_core::compute (aut, true);
      if (census.global_core != threshold) {
        std::cerr << text << ": census disagrees after renaming, "
                  << census.global_core << " vs " << threshold << '\n';
        ok = false;
        continue;
      }
      for (unsigned q = 0; q < aut->num_states (); ++q)
        if (census.in_global_core[q] != (q < threshold)) {
          std::cerr << text << ": state " << q << " is "
                    << (census.in_global_core[q] ? "counting" : "Boolean")
                    << " but sits " << (q < threshold ? "below" : "at or above")
                    << " the threshold " << threshold << '\n';
          ok = false;
          break;
        }
    }
    return ok;
  }

  // A Boolean coordinate's safe value is 0 and it is reset to 0 on every k
  // step, so an edge entering the tail must never carry an increment.
  bool check_tail_never_increments () {
    bool ok = true;
    for (std::string_view text : formulas) {
      auto aut = build_state_based (text);
      if (not aut)
        continue;
      const size_t threshold = boolean_states::transition_core::make (aut, 10) ();
      for (unsigned p = 0; p < aut->num_states (); ++p)
        for (const auto& e : aut->out (p))
          if (e.dst >= threshold and e.acc != spot::acc_cond::mark_t {}) {
            std::cerr << text << ": edge " << p << " -> " << e.dst
                      << " enters the Boolean tail still marked accepting\n";
            ok = false;
          }
    }
    return ok;
  }

  // The formula-derived automata above never happen to put a marked edge into
  // the Boolean tail, so they cannot tell whether the clearing is performed or
  // merely unnecessary.  Build the case explicitly: a marked edge between two
  // trivial SCCs, with no accepting SCC anywhere, so the core is empty, every
  // state is Boolean, and the mark would increment a Boolean coordinate.
  bool check_tail_clearing_is_performed () {
    auto dictionary = spot::make_bdd_dict ();
    auto aut = spot::make_twa_graph (dictionary);
    aut->set_buchi ();
    aut->new_states (3);
    aut->set_init_state (0U);
    const bdd t = bddtrue;
    aut->new_edge (0, 1, t, aut->acc ().all_sets ());  // marked, enters the tail
    aut->new_edge (1, 2, t);
    aut->new_edge (2, 2, t);                           // nonaccepting sink loop

    unsigned marked_before = 0;
    for (unsigned p = 0; p < aut->num_states (); ++p)
      for (const auto& e : aut->out (p))
        if (e.acc != spot::acc_cond::mark_t {})
          ++marked_before;
    if (marked_before == 0) {
      std::cerr << "tail-clearing fixture built no marked edge\n";
      return false;
    }

    const size_t threshold = boolean_states::transition_core::make (aut, 10) ();
    if (threshold != 0) {
      std::cerr << "tail-clearing fixture: expected an empty counting core, got "
                << threshold << '\n';
      return false;
    }
    for (unsigned p = 0; p < aut->num_states (); ++p)
      for (const auto& e : aut->out (p))
        if (e.dst >= threshold and e.acc != spot::acc_cond::mark_t {}) {
          std::cerr << "tail-clearing fixture: edge " << p << " -> " << e.dst
                    << " still marked after the pass\n";
          return false;
        }
    return true;
  }

}  // namespace

int main () {
  bool ok = true;
  ok &= check_agreement ();
  ok &= check_ordering ();
  ok &= check_tail_never_increments ();
  ok &= check_tail_clearing_is_performed ();
  return ok ? 0 : 1;
}
