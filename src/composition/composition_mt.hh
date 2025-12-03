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


  spot::formula bdd_to_formula (bdd f) const; // for debugging

  // void solve_game (safety_game& game); // use the k-bounded safety aut to solve a game
  int epilogue (); // look at the final result and return whether it was realizable

  using aut_t = decltype (trans_.run (spot::formula::ff ()));
  aut_t push_outputs (const aut_t& aut, bdd all_inputs, bdd all_outputs);

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

spot::formula composition_mt::bdd_to_formula (bdd f) const {
  return spot::bdd_to_formula (f, dict);
}



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
  // safety_game game = prepare_formula (formula_);
  safety_game game = prepare_formula(formula_, trans_, all_inputs, all_outputs, opt_K, opt_Kmin);
  stored_result = std::make_shared<safety_game> (game);
  // add_result (game);
  return epilogue ();
}

////////////////



// Changes q -> <i', o'> -> q' with saved o to
// q -> <i', o> -> {q' saved o}
composition_mt::aut_t composition_mt::push_outputs (const composition_mt::aut_t& aut, bdd all_inputs, bdd all_outputs) {
  auto ret = spot::make_twa_graph (aut->get_dict ());
  ret->copy_acceptance_of (aut);
  ret->copy_ap_of (aut);
  ret->prop_copy (aut, spot::twa::prop_set::all());
  ret->prop_universal (spot::trival::maybe ());

  static auto cache = utils::make_cache<unsigned> (0u, 0u);
  std::stack<std::pair<unsigned, bdd>> to_treat;
  to_treat.push ({ aut->get_init_state_number (), bddtrue });
  cache (ret->new_state (), aut->get_init_state_number (), bddtrue.id ());
  while (not to_treat.empty ()) {
    auto [state, saved_o]  = to_treat.top ();
    to_treat.pop ();
    auto ret_state = *cache.get (state, saved_o.id ());
    for (auto& e : aut->out (state)) {
      for (auto&& one_input_bdd : minterms_of (e.cond, all_inputs)) {
        // Pick one satisfying assignment where outputs all have values
        auto nxt_bdd = bdd_exist (e.cond & one_input_bdd, all_inputs);
        auto cached = cache.get (e.dst, nxt_bdd.id ());
        unsigned nxt_state;
        if (cached)
          nxt_state = *cached;
        else {
          nxt_state = ret->new_state ();
          cache (nxt_state, e.dst, nxt_bdd.id ());
          to_treat.push ({ e.dst, nxt_bdd });
        }
        ret->new_edge (ret_state, nxt_state, saved_o & one_input_bdd, e.acc);
      }
    }
  }

  return ret;
}

