//
// Created by nils on 04/05/23.
//

#pragma once
#include "types.hh"
#include "composition.hh"
#include <queue>
#include <fcntl.h>
#include <thread>
#include <spot/twaalgos/translate.hh>
#include "pipes.hh"
#include "aut_preprocessors.hh"
#include "k-bounded_safety_aut.hh"


class job_solve;

using job_ptr = std::shared_ptr<job_solve>;


class composition_mt {
  private:
  // TODO: this will become a shared pointer, fixed in the constructor
  std::queue<job_ptr> pending_jobs; // all currently unfinished jobs no worker is working on yet
  std::shared_ptr<safety_game> stored_result; // room for temporary result: if there are 2, merge them
  bool losing = false; // whether the game is already found to be losing (early abort)

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

  // TODO: refactor away
  void enqueue (job_ptr p); // add a new job to the queue
  job_ptr dequeue (); // take a job from the pending jobs queue

  void solve_game (safety_game& game); // use the k-bounded safety aut to solve a game
  int epilogue (); // look at the final result and return whether it was realizable
  // void be_child (int id); // does everything a child process has to do
  void add_result (safety_game& r); // add a new result to the temporary, or add a merge if there is already one stored

  using aut_t = decltype (trans_.run (spot::formula::ff ()));
  aut_t push_outputs (const aut_t& aut, bdd all_inputs, bdd all_outputs);
  safety_game prepare_formula (spot::formula f); // turn a formula into an automaton

  public:
  composition_mt (unsigned opt_K, unsigned opt_Kmin, unsigned opt_Kinc,
      spot::bdd_dict_ptr dict, spot::translator& trans, bdd all_inputs, bdd
      all_outputs, std::vector<std::string> input_aps_,
      std::vector<std::string> output_aps_, std::vector<int> init_state, spot::formula&& formula):
    opt_K(opt_K), opt_Kmin(opt_Kmin), opt_Kinc(opt_Kinc), dict(dict),
    trans_(trans), all_inputs(all_inputs), all_outputs(all_outputs),
    input_aps_(input_aps_), output_aps_(output_aps_), init_state(init_state), formula_(std::move(formula)) {
    // TODO: pass job ptr here
  }

  int run_one (); // solve only one formula, with no subprocesses
};

// solve the safety game, changing the downset to the actual safe region instead of
// an overapproximation
class job_solve {
  public:
  safety_game starting_point;
  bdd invariant;

  public:
  explicit job_solve (safety_game& game);
};


//////////////////////////////////////////////////



job_solve::job_solve (safety_game& game) {
  starting_point = game;
  invariant = bddtrue;
}

//////////////////////////////////////////////////



spot::formula composition_mt::bdd_to_formula (bdd f) const {
  return spot::bdd_to_formula (f, dict);
}

void composition_mt::enqueue (job_ptr p) {
  pending_jobs.push(p);
}

job_ptr composition_mt::dequeue () {
  if (pending_jobs.empty ()) {
    // done
    return nullptr;
  }
  auto val = pending_jobs.front ();
  pending_jobs.pop ();
  return val;
}

void composition_mt::add_result (safety_game& r) {
  if (!stored_result) {
    stored_result = std::make_shared<safety_game> (r);
  }
  else {
    // TODO: I think we can remove all of this. There should not yet be a result.
    // merge the stored result, and the new result r
    safety_game inputs[2];
    inputs[0] = *stored_result;
    inputs[1] = r;

    assert (inputs[0].safe);
    assert (inputs[1].safe);

    verb_do (2, vout << "Merging " << *inputs[0].safe << " and " << *inputs[1].safe);

    auto composer = composition ();
    composer.merge_aut (inputs[0], inputs[1]);
    inputs[0].safe = std::make_shared<GenericDownset> (composer.merge_saferegions (*inputs[0].safe, *inputs[1].safe));
    inputs[0].solved = false;

    assert (inputs[0].safe);
    verb_do (2, vout << "Merge res: " << *(inputs[0].safe));
    verb_do (1, vout << "Done with merge, adding solve job\n");
    enqueue (std::make_shared<job_solve> (inputs[0]));

    stored_result = nullptr;
  }
}


void composition_mt::solve_game (safety_game& game) {
  spot::stopwatch sw;
  sw.start ();

  auto [nbitsetbools, actual_nonbools] = game.set_globals ();

#define UNREACHABLE [] (int x) { assert (false); }

  constexpr auto STATIC_ARRAY_CAP_MAX =
    posets::vectors::traits<posets::vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (STATIC_ARRAY_MAX);

  if (actual_nonbools <= STATIC_ARRAY_CAP_MAX) { // Array & Bitsets
    static_switch_t<STATIC_ARRAY_CAP_MAX> {} (
    [&] (auto vnonbools) {
      static_switch_t<STATIC_MAX_BITSETS> {} (
      [&] (auto vbitsets) {
        using SpecializedDownset = posets::downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<
          posets::vectors::x_and_bitset<
            posets::vectors::ARRAY_IMPL<VECTOR_ELT_T, std::max (vnonbools.value, 1UL)>,
            vbitsets.value>>;
        auto skn = K_BOUNDED_SAFETY_AUT_IMPL<SpecializedDownset>
        (game.aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
        assert (game.safe);
        auto current_safe = cast_downset<SpecializedDownset> (*game.safe);
        auto safe = skn.solve (current_safe, invariant, init_state);
        if (safe.has_value ()) {
          game.safe = std::make_shared<GenericDownset> (cast_downset<GenericDownset> (safe.value ()));
        } else game.safe = nullptr;
      },
      UNREACHABLE,
      posets::vectors::nbools_to_nbitsets (nbitsetbools));
    },
    UNREACHABLE,
    actual_nonbools);
  }
  else {                                  // Vectors & Bitsets
    static_switch_t<STATIC_MAX_BITSETS> {} (
    [&] (auto vbitsets) {
      using SpecializedDownset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<
      posets::vectors::x_and_bitset<
      posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>,
      vbitsets.value>>;
      auto skn = K_BOUNDED_SAFETY_AUT_IMPL<SpecializedDownset>
      (game.aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
      assert (game.safe);
      auto current_safe = cast_downset<SpecializedDownset> (*game.safe);
      auto safe = skn.solve (current_safe, invariant, init_state);
      if (safe.has_value ()) {
        game.safe = std::make_shared<GenericDownset> (cast_downset<GenericDownset> (safe.value ()));
      } else game.safe = nullptr;
    },
    UNREACHABLE,
    posets::vectors::nbools_to_nbitsets (nbitsetbools));
  }

  game.solved = true;
  game.invariant = invariant;

  double solve_time = sw.stop ();
  verb_do (1, vout << "Safety game solved in " << solve_time << " seconds\n");
}

int composition_mt::epilogue () {
  // TODO: this should either return true (real) or false (unknown). This will require some changes.
  if (losing) {
    utils::vout << "(part of) safety game is not winning!\n";
    return 0;
  }

  // check stored_result
  // TODO: this branch should be impossible?
  if (!stored_result) {
    // can happen if there are only invariants -> make a dummy automaton with 1 non-accepting state
    safety_game r;

    spot::twa_graph_ptr aut = new_automaton (dict);
    aut->new_states (1);
    aut->set_init_state (0);
    aut->new_edge (0, 0, bddtrue);

    r.solved = true;

    auto safe = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), 0);
    safe[0] = 0;
    r.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (safe));
    r.aut = aut;
    r.invariant = invariant;

    stored_result = std::make_shared<safety_game> (r);
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
    solve_game (r);
  }

  // call synthesis if needed
  // TODO remove
  // if ((r.safe != nullptr) and (not synth_fname.empty () or not winreg_fname.empty ())) {
  //   r.set_globals ();
  //   auto skn = K_BOUNDED_SAFETY_AUT_IMPL<GenericDownset>
  //     (r.aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
  //   if (!winreg_fname.empty ())
  //     skn.winregion (*r.safe, winreg_fname, invariant, init_state);
  //   if (!synth_fname.empty ())
  //     skn.synthesis (*r.safe, synth_fname, invariant, init_state);
  // }

  // if there is no safe region: return 0 (not winning)
  return r.safe != nullptr;
}

// TODO: rename to "check_is_realisable")
int composition_mt::run_one () {

  safety_game game = prepare_formula (formula_);
  add_result (game);
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

safety_game composition_mt::prepare_formula (spot::formula f) {
  // Note: this function is only run once with unrealizability as there is no composition -> swapping the inputs/outputs only happens once

  spot::process_timer timer;
  timer.start ();

  spot::stopwatch sw, sw_nospot;
  bool want_time = true; // Hardcoded

  // To Universal co-Büchi Automaton
  trans_.set_type(spot::postprocessor::BA);
  // "Desired characteristics": Small and state-based acceptance (implied by BA).
  trans_.set_pref(spot::postprocessor::Small |
                  //spot::postprocessor::Complete | // TODO: We did not need that originally; do we now?
                  spot::postprocessor::SBAcc);

  if (want_time) {
    sw.start ();
  }

  f = spot::formula::Not (f);




  verb_do (1, vout << "Formula: " << f << std::endl);

  auto aut = trans_.run (&f);

  if (want_time) {
    double trans_time = sw.stop ();
    verb_do (1, vout << "Translating formula done in "
                << trans_time << " seconds\n");
    verb_do (1, vout << "Automaton has " << aut->num_states ()
                << " states and " << aut->num_sets () << " colors\n");
  }



  ////////////////////////////////////////////////////////////////////////
  // Preprocess automaton

  if (want_time) {
    sw.start();
    sw_nospot.start ();
  }

  auto aut_preprocessors_maker = AUT_PREPROCESSOR ();
  // NOTE: this warns about non-trivial types going into variadic args. This is only relevant
  //  for the "no_preprocessing" implementation.
  (aut_preprocessors_maker.make (aut, all_inputs, all_outputs, opt_K)) ();
  if (want_time) {
    double merge_time = sw.stop();
    verb_do (1, vout << "Preprocessing done in " << merge_time
                << " seconds\nDPA has " << aut->num_states()
                << " states\n");
  }
  verb_do (2, spot::print_hoa (utils::vout, aut, nullptr));

  ////////////////////////////////////////////////////////////////////////
  // Boolean states

  if (want_time)
    sw.start ();

  auto boolean_states_maker = BOOLEAN_STATES ();
  posets::vectors::bool_threshold = (boolean_states_maker.make (aut, opt_K)) ();

  if (want_time) {
    double boolean_states_time = sw.stop ();
    verb_do (1, vout << "Computation of boolean states in " << boolean_states_time
      /*          */ << "seconds , found " << posets::vectors::bool_threshold << " nonboolean states.\n");
  }


  ////////////////////////////////////////////////////////////////////////
  // Build S^K_N game, solve it.

  //if (want_time)
  //  sw.start ();

  safety_game ret;
  ret.aut = aut;
  ret.bool_threshold = posets::vectors::bool_threshold;
  ret.solved = false;
  ret.set_globals ();

  auto all_k = posets::utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), opt_Kmin - 1);
  for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
    all_k[i] = 0;
  ret.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (all_k));


  if (want_time) {
    double solve_time = sw.stop ();
    verb_do (1, vout << "Safety game created in " << solve_time << " seconds\n");
    verb_do (1, vout << "Time disregarding Spot translation: " << sw_nospot.stop () << " seconds\n");
  }

  timer.stop ();

  return ret;
}
