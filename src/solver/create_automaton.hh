#pragma once

#include "utils/verbose.hh"

#include <iostream>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/translate.hh>

// Output preference passed to Spot's translator. Historically this was
// `Small`, which makes Spot try several constructions and keep the smallest --
// on some specs (notably bounded-response formulas with long X-chains) this
// minimization dominates the whole solve, with no coverage benefit measured
// over `Any` (A/B on 106 instances: 82 vs 81 solved, -9% total time, zero
// regressions). Override at build time with e.g.
// -DACACIA_TRANSLATION_PREF='spot::postprocessor::Small'.
#ifndef ACACIA_TRANSLATION_PREF
#  define ACACIA_TRANSLATION_PREF spot::postprocessor::Any
#endif

spot::twa_graph_ptr create_automaton (spot::formula& f, spot::translator& trans) {
  // To Universal co-Büchi Automaton
  trans.set_type (spot::postprocessor::BA);
  // "Desired characteristics": state-based acceptance (implied by BA) plus the
  // configurable size/determinism preference above.
  trans.set_pref (
      ACACIA_TRANSLATION_PREF |
      // spot::postprocessor::Complete | // TODO: We did not need that originally; do we now?
      spot::postprocessor::SBAcc);  // state-based acceptacen
  verb_do (1, vout << "Formula: " << f << std::endl);
  auto aut = trans.run (f);
  if (aut->num_states () > 0 and not aut->prop_state_acc ().is_true ()) {
    [[maybe_unused]] const auto old_states = aut->num_states ();
    aut = spot::sbacc (aut);
    verb_do (1, vout << "Converted automaton to state-based acceptance: "
                     << old_states << " -> " << aut->num_states ()
                     << " states." << std::endl);
  }
  return aut;
}
