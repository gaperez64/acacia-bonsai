#pragma once

#include "actioners.hh"
#include "input_pickers.hh"
#include "ios_precomputers.hh"
#include "safety_game.hh"
#include "k-bounded_safety_aut.hh"

#include <bddx.h>


template <class SetOfStates, class IOsPrecomputationMaker, class ActionerMaker,
          class InputPickerMaker>
static auto k_bounded_safety_aut_maker (const spot::twa_graph_ptr& aut, int Kfrom, int Kto,
                                        int Kinc, bdd input_support, bdd output_support,
                                        const IOsPrecomputationMaker& ios_precomputer_maker,
                                        const ActionerMaker& actioner_maker,
                                        const InputPickerMaker& input_picker_maker) {
  return k_bounded_safety_aut_detail<SetOfStates, IOsPrecomputationMaker, ActionerMaker,
                                     InputPickerMaker> (aut, Kfrom, Kto, Kinc, input_support,
                                                        output_support, ios_precomputer_maker,
                                                        actioner_maker, input_picker_maker);
}

template <class SetOfStates>
static auto k_bounded_safety_aut (const spot::twa_graph_ptr& aut, int Kfrom, int Kto, int Kinc,
                                  bdd input_support, bdd output_support) {
  return k_bounded_safety_aut_maker<SetOfStates> (aut, Kfrom, Kto, Kinc, input_support,
                                                  output_support, IOS_PRECOMPUTER (), ACTIONER<typename SetOfStates::value_type> (),
                                                  INPUT_PICKER ());
}


// template <class SetOfStates, class IOsPrecomputationMaker, class ActionerMaker,
//           class InputPickerMaker>
// static auto k_bounded_safety_aut (const spot::twa_graph_ptr& aut, int Kfrom, int Kto, int Kinc,
//                                   bdd input_support, bdd output_support) {
//   return k_bounded_safety_aut_detail<SetOfStates, IOsPrecomputationMaker, ActionerMaker,
//                                      InputPickerMaker> (aut, Kfrom, Kto, Kinc, input_support,
//                                                   output_support, IOS_PRECOMPUTER (), ACTIONER<typename SetOfStates::value_type> (),
//                                                   INPUT_PICKER ());
// }




bool solve_game (safety_game& game, unsigned kmax, unsigned kmin, unsigned kinc, bdd all_inputs,
                 bdd all_outputs);
