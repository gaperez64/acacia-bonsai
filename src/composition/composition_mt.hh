//
// Created by nils on 04/05/23.
//

#pragma once
#include "types.hh"
#include "composition.hh"
#include <queue>
#include <fcntl.h>
#include <spot/twaalgos/translate.hh>
#include <cassert>
#include <sstream>
#include <spot/tl/parse.hh>
#include "aut_preprocessors.hh"
#include "k-bounded_safety_aut.hh"
#include "create_safety_game.hh"
#include "solve_game.hh"


class composition_mt {
  private:
  std::shared_ptr<safety_game> stored_result; // room for temporary result: if there are 2, merge them

  bdd invariant = bddtrue;

  // fields borrowed from ltl_processor
  unsigned opt_K, opt_Kmin, opt_Kinc;
  spot::bdd_dict_ptr dict;
  spot::translator &trans_;
  bdd all_inputs, all_outputs;
  std::vector<std::string> input_aps_;
  std::vector<std::string> output_aps_;
  spot::formula formula_;

  std::vector<int> init_state;

  int epilogue (); // look at the final result and return whether it was realizable


  public:
  composition_mt (unsigned opt_K, unsigned opt_Kmin, unsigned opt_Kinc,
      spot::bdd_dict_ptr dict, spot::translator& trans, bdd all_inputs, bdd
      all_outputs, std::vector<std::string> input_aps_,
      std::vector<std::string> output_aps_, std::vector<int> init_state, spot::formula&& formula):
    opt_K(opt_K), opt_Kmin(opt_Kmin), opt_Kinc(opt_Kinc), dict(dict),
    trans_(trans), all_inputs(all_inputs), all_outputs(all_outputs),
    input_aps_(input_aps_), output_aps_(output_aps_), init_state(init_state), formula_(std::move(formula)) {
  }

  int run_one (); // solve only one formula, with no subprocesses
};



int composition_mt::epilogue () {
  // TODO: this should either return true (real) or false (unknown). This will require some changes.

  // check stored_result
  // TODO: this branch should be impossible?
  if (!stored_result) {
    error(EXIT_CODE_ERROR, "Error: result should already exist!");
  }

  safety_game& r = *stored_result;

  // if the final result was not solved, or it was solved with the wrong invariant (if the IOs precomputer uses it in the first place)
  // then a final solve is needed before calling synthesis
  bool not_fully_solved = ((r.invariant != invariant) && IOS_PRECOMPUTER::supports_invariant);

  // there is a special case of having had found all states to be bounded
  // this should only happen when checking UNREAL, and it means we can
  // return true
  if (r.aut == nullptr) {
    // assert (synth_fname.empty ());
    return true;
  }

  if ((!r.solved) || not_fully_solved) {
    if (!r.solved) verb_do (1, vout << "Not fully solved -> extra solve\n");
    if (not_fully_solved) verb_do (1, vout << "Solved but not with the right invariant -> extra solve\n");
    solve_game (r, opt_K, opt_Kmin, opt_Kinc, all_inputs, all_outputs, init_state, invariant);
  }
  // if there is no safe region: return 0 (not winning)
  return r.safe != nullptr;
}

// TODO: rename to "check_is_realisable")
int composition_mt::run_one () {
  safety_game game = prepare_formula(formula_, trans_, all_inputs, all_outputs, opt_K, opt_Kmin);
  stored_result = std::make_shared<safety_game> (game);
  return epilogue ();
}
