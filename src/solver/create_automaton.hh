#pragma once

#include "solver/transition_payload.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/sbacc.hh>
#include <spot/twaalgos/translate.hh>

// Output preference passed to Spot's translator. `Small` is the correctness
// baseline inherited from main; faster preferences such as `Any` are exposed as
// Meson presets for ablation because they can change solver conclusiveness.
#ifndef ACACIA_TRANSLATION_PREF
#  define ACACIA_TRANSLATION_PREF spot::postprocessor::Small
#endif

spot::twa_graph_ptr create_automaton (
    spot::formula& f, spot::translator& trans,
    spot::postprocessor::output_pref preference = ACACIA_TRANSLATION_PREF) {
  verb_do (1, vout << "Formula: " << f << std::endl);

#if ACACIA_TRANSITION_ACCEPTANCE
  // Keep acceptance on transitions.  `BA` below is documented by Spot as
  // implying `SBAcc`, and that lowering duplicates states precisely to move
  // acceptance onto them -- every duplicate then becomes another numeric rank
  // coordinate.  `Buchi` is the same ordinary Büchi acceptance without the
  // state-based requirement, so the counting core measured by
  // `boolean_states::transition_core` is the smaller one.
  trans.set_type (spot::postprocessor::Buchi);
  trans.set_pref (preference);
  return trans.run (f);
#else
  // To Universal co-Büchi Automaton
  trans.set_type (spot::postprocessor::BA);
  // "Desired characteristics": state-based acceptance (implied by BA) plus the
  // configurable size/determinism preference above.
  trans.set_pref (
      preference |
      // spot::postprocessor::Complete | // TODO: We did not need that originally; do we now?
      spot::postprocessor::SBAcc);  // state-based acceptacen
  auto aut = trans.run (f);
  if (aut->num_states () > 0 and not aut->prop_state_acc ().is_true ()) {
    [[maybe_unused]] const auto old_states = aut->num_states ();
    aut = spot::sbacc (aut);
    verb_do (1, vout << "Converted automaton to state-based acceptance: "
                     << old_states << " -> " << aut->num_states ()
                     << " states." << std::endl);
  }
  return aut;
#endif
}
