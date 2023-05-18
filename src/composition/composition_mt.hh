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


class job_base;

using job_ptr = std::shared_ptr<job_base>;

struct worker_t {
  pipe_t to_main, from_main;
  pid_t pid = -1;
  bool active = true;
};

class composition_mt {
  public:
  std::queue<job_ptr> pending_jobs;
  std::shared_ptr<aut_ret> stored_result;
  bool losing = false;

  pipe_t shared_pipe;
  std::vector<worker_t> workers;

  bdd invariant = bddtrue;

  // fields borrowed from ltl_processor
  unsigned opt_K, opt_Kmin, opt_Kinc;
  spot::bdd_dict_ptr dict;
  spot::translator &trans_;
  bdd all_inputs, all_outputs;
  std::vector<std::string> input_aps_;
  std::vector<std::string> output_aps_;



  public:
  composition_mt (unsigned opt_K, unsigned opt_Kmin, unsigned opt_Kinc, spot::bdd_dict_ptr dict, spot::translator& trans, bdd all_inputs, bdd all_outputs,
                      std::vector<std::string> input_aps_, std::vector<std::string> output_aps_): opt_K(opt_K), opt_Kmin(opt_Kmin), opt_Kinc(opt_Kinc),
        dict(dict), trans_(trans), all_inputs(all_inputs), all_outputs(all_outputs), input_aps_(input_aps_), output_aps_(output_aps_) {
  }

  spot::formula bdd_to_formula (bdd f) const;
  void enqueue (job_ptr p);
  void add_invariant (bdd inv);
  void add_formula (spot::formula f);
  void finish_invariant ();
  void solve_game (aut_ret& game);
  int epilogue (std::string synth_fname);
  void be_child (int id);

  job_ptr dequeue ();

  void add_result (aut_ret& r);

  int run (int workers, std::string synth_fname);

  using aut_t = decltype (trans_.run (spot::formula::ff ()));
  aut_t push_outputs (const aut_t& aut, bdd all_inputs, bdd all_outputs);
  aut_ret prepare_formula (spot::formula f, bool check_real = true, unreal_x_t opt_unreal_x = UNREAL_X_BOTH);
};

class job_base {
  public:
  virtual void to_pipe(pipe_t&) = 0;
  virtual ~job_base () = default;
};


// solve the safety game, changing the downset to the actual safe region instead of
// an overapproximation
class job_solve: public job_base {
  public:
  aut_ret starting_point;

  public:
  explicit job_solve (aut_ret& game);

  void to_pipe(pipe_t&) override;
  ~job_solve () override = default;
};

// turn a formula into an automaton with a starting all-k safe region
class job_formula: public job_base {
  public:
  spot::formula f;

  public:
  explicit job_formula (spot::formula f);

  void to_pipe(pipe_t&) override;
  ~job_formula () override = default;
};

//////////////////////////////////////////////////



job_solve::job_solve (aut_ret& game) {
  starting_point = game;
}

void job_solve::to_pipe (pipe_t& pipe) {
  // send this job to a subprocess
  pipe.write_obj<job_type> (e_solve);
  pipe.write_result (starting_point);
  verb_do(1, vout << "Solve job sent: wrote " << pipe.get_bytes_count () << " bytes to pipe\n");
}


job_formula::job_formula (spot::formula f): f(f) {

}

void job_formula::to_pipe (pipe_t& pipe) {
  pipe.write_obj<job_type> (e_formula);
  pipe.write_formula (f);
  verb_do(1, vout << "Formula job sent: wrote " << pipe.get_bytes_count () << " bytes to pipe\n");
}



bool is_invariant(spot::twa_graph_ptr aut, bdd& condition) {
  if (aut->num_states () != 2) return false;
  unsigned init = aut->get_init_state_number ();
  unsigned other = 1-init;
  if (aut->state_is_accepting (init)) return false;
  if (!aut->state_is_accepting (other)) return false;

  bdd condition_neg;

  int edges = 0;

  for(auto& edge: aut->edges ()) {
    if ((edge.src == init) && (edge.dst == init)) {
      condition = edge.cond;
      edges |= 1;
    }
    else if ((edge.src == init) && (edge.dst == other)) {
      condition_neg = edge.cond;
      edges |= 2;
    }
    else if ((edge.src == other) && (edge.dst == other)) {
      if (edge.cond != bddtrue) return false;
      edges |= 4;
    }
    else return false;
  }

  return ((edges == 7) && (condition == !condition_neg));
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

void composition_mt::add_result (aut_ret& r) {
  if (!stored_result) {
    stored_result = std::make_shared<aut_ret> (r);
  }
  else {
    aut_ret inputs[2];
    inputs[0] = *stored_result;
    inputs[1] = r;

    assert (inputs[0].safe);
    assert (inputs[1].safe);

    verb_do(2, vout << "Merging " << *inputs[0].safe << " and " << *inputs[1].safe);

    auto composer = composition ();
    composer.merge_aut (inputs[0], inputs[1]);
    inputs[0].safe = std::make_shared<GenericDownset> (composer.merge_saferegions (*inputs[0].safe, *inputs[1].safe));
    inputs[0].solved = false;

    assert (inputs[0].safe);
    verb_do(2, vout << "Merge res: " << *(inputs[0].safe));
    verb_do(1, vout << "Done with merge, adding solve job\n");
    enqueue (std::make_shared<job_solve> (inputs[0]));

    stored_result = nullptr;
  }
}

void composition_mt::add_invariant (bdd inv) {
  invariant &= inv;
  if (invariant == bddfalse) losing = true;
}

void composition_mt::add_formula (spot::formula f) {
  enqueue (std::make_shared<job_formula> (f));
}

void composition_mt::finish_invariant() {
  // create 2-state solved automaton for all invariants found
  if (invariant != bddtrue) {
    aut_ret invariant_aut;
    invariant_aut.bool_threshold = 1;

    spot::twa_graph_ptr aut = std::make_shared<spot::twa_graph> (dict);
    aut->set_generalized_buchi (1);
    aut->set_acceptance (spot::acc_cond::inf ({0}));
    aut->prop_state_acc (true);
    aut->new_states (2);
    aut->set_init_state (1);
    verb_do(1, vout << "Gathered invariants: adding invariant " << spot::bdd_to_formula (invariant, dict) << "\n");

    aut->new_edge (1, 1, invariant);
    aut->new_edge (1, 0, !invariant);
    aut->new_acc_edge (0, 0, bddtrue);

    invariant_aut.solved = true;

    auto safe = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), 0);
    safe[0] = -1;
    invariant_aut.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (safe));
    invariant_aut.aut = aut;

    add_result (invariant_aut);
  }
}

void composition_mt::solve_game (aut_ret& game) {
  spot::stopwatch sw;
  sw.start ();

  auto [nbitsetbools, actual_nonbools] = game.set_globals ();

#define UNREACHABLE [] (int x) { assert (false); }

  constexpr auto STATIC_ARRAY_CAP_MAX =
  vectors::traits<vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (STATIC_ARRAY_MAX);

  if (actual_nonbools <= STATIC_ARRAY_CAP_MAX) { // Array & Bitsets
    static_switch_t<STATIC_ARRAY_CAP_MAX> {} (
    [&] (auto vnonbools) {
      static_switch_t<STATIC_MAX_BITSETS> {} (
      [&] (auto vbitsets) {
        using SpecializedDownset = downsets::ARRAY_AND_BITSET_DOWNSET_IMPL<
        vectors::X_and_bitset<
        vectors::ARRAY_IMPL<VECTOR_ELT_T, vnonbools.value>,
        vbitsets.value>>;
        auto skn = K_BOUNDED_SAFETY_AUT_IMPL<SpecializedDownset>
        (game.aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
        assert(game.safe);
        auto current_safe = cast_downset<SpecializedDownset> (*game.safe);
        auto safe = skn.solve (current_safe);
        if (safe.has_value ()) {
          game.safe = std::make_shared<GenericDownset>(cast_downset<GenericDownset> (safe.value ()));
        } else game.safe = nullptr;
      },
      UNREACHABLE,
      vectors::nbools_to_nbitsets (nbitsetbools));
    },
    UNREACHABLE,
    actual_nonbools);
  }
  else {                                  // Vectors & Bitsets
    static_switch_t<STATIC_MAX_BITSETS> {} (
    [&] (auto vbitsets) {
      using SpecializedDownset = downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<
      vectors::X_and_bitset<
      vectors::VECTOR_IMPL<VECTOR_ELT_T>,
      vbitsets.value>>;
      auto skn = K_BOUNDED_SAFETY_AUT_IMPL<SpecializedDownset>
      (game.aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
      assert(game.safe);
      auto current_safe = cast_downset<SpecializedDownset> (*game.safe);
      auto safe = skn.solve (current_safe);
      if (safe.has_value ()) {
        game.safe = std::make_shared<GenericDownset>(cast_downset<GenericDownset> (safe.value ()));
      } else game.safe = nullptr;
    },
    UNREACHABLE,
    vectors::nbools_to_nbitsets (nbitsetbools));
  }

  game.solved = true;

  double solve_time = sw.stop ();
  verb_do(1, vout << "Safety game solved in " << solve_time << " seconds\n");
}

int composition_mt::epilogue (std::string synth_fname) {
  if (losing) {
    utils::vout << "(part of) safety game is not winning!\n";
    return 0;
  }

  // check stored_result
  if (!stored_result) {
    // shouldn't happen
    utils::vout << "No result?\n";
    return 0;
  }

  aut_ret& r = *stored_result;

  // solve it if it hasn't been solved
  if (!r.solved) {
    solve_game (r);
  }

  if (!r.safe) {
    utils::vout << "Safety game is not winning!!\n";
    return 0;
  }

  if (!synth_fname.empty()) {
    r.set_globals ();
    auto skn = K_BOUNDED_SAFETY_AUT_IMPL<GenericDownset>
    (r.aut, opt_Kmin, opt_K, opt_Kinc, all_inputs, all_outputs);
    skn.synthesis (*r.safe, synth_fname);
  }

  return 1;
}

void composition_mt::be_child (int id) {
  utils::vout.set_prefix ("[" + std::to_string (id+1) + "] ");

  pipe_t& to_main = workers[id].to_main;
  pipe_t& from_main = workers[id].from_main;

  while (true) {
    job_type job = from_main.read_obj<job_type> ();

    if (job == e_done) break;

    if (job == e_solve) {
      // solve job
      aut_ret r = from_main.read_result (dict);
      verb_do(1, vout << "Solve job received: read " << from_main.get_bytes_count () << " bytes from pipe\n");
      verb_do(1, vout << "Starting solve on automaton with " << r.aut->num_states() << " states\n");

      solve_game (r);

      shared_pipe.write_obj<char> (id);
      to_main.write_obj<int> (MESSAGE_START);
      to_main.write_obj<char> (0);
      to_main.write_result (r);
      to_main.write_obj<int> (MESSAGE_END);

      verb_do(1, vout << "Done: wrote " << to_main.get_bytes_count () << " bytes to pipe\n");
      continue;
    }

    if (job == e_formula) {
      // turn formula into automaton
      spot::formula f = from_main.read_formula ();
      verb_do(1, vout << "Formula job received: read " << from_main.get_bytes_count () << " bytes from pipe\n");
      verb_do(1, vout << "Formula: " << f << "\n");

      aut_ret r = prepare_formula (f);

      shared_pipe.write_obj<char> (id);
      to_main.write_obj<int> (MESSAGE_START);

      if (r.aut) {
        bdd condition;
        if (is_invariant (r.aut, condition)) {
          to_main.write_obj<char> (1);
          to_main.write_bdd (condition, dict);
        } else {
          to_main.write_obj<char> (0);
          to_main.write_result (r);
        }
      } else {
        // trivial formula: just send this as an invariant "true"
        to_main.write_obj<char> (1);
        to_main.write_bdd (bddtrue, dict);
      }

      to_main.write_obj<int> (MESSAGE_END);

      verb_do(1, vout << "Done: wrote " << to_main.get_bytes_count () << " bytes to pipe\n");
      continue;
    }

    assert (false);
  }

  verb_do(1, vout << "Worker is finished!\n");
  exit(0);
}

int composition_mt::run (int worker_count, std::string synth_fname) {
  utils::vout.set_prefix ("[0] ");

  if (worker_count <= 0) {
    worker_count = std::thread::hardware_concurrency ();
  }
  worker_count = std::min<int> (worker_count, 255);
  worker_count = std::min<int> (worker_count, pending_jobs.size ());
  utils::vout << "Workers: " << worker_count << "\n";

  assert (worker_count > 0);

  // create shared pipe
  assert (shared_pipe.create_pipe () == 0);

  workers.resize (worker_count);
  for(int i = 0; i < worker_count; i++) {
    assert (workers[i].to_main.create_pipe () == 0);
    assert (workers[i].from_main.create_pipe () == 0);
    //int new_size = fcntl(workers[i].pipe.w, F_SETPIPE_SZ, 1024*1024);
    //assert(new_size != -1);
    //utils::vout << "Pipe size: " << new_size << "\n";
  }

  // how many formula jobs aren't yet solved: once this is 0, add invariants
  int base_remaining = pending_jobs.size ();

  // spawn the workers with their initial job
  for(int i = 0; i < worker_count; i++) {
    pid_t pid = fork ();
    assert (pid >= 0);

    if (pid > 0) {
      workers[i].pid = pid;
      job_ptr job = dequeue ();
      assert(job != nullptr);

      job->to_pipe (workers[i].from_main);
    }
    else {
      // child process
      be_child (i);
    }
  }

  int active_workers = worker_count;


  // wait until a process writes to the shared pipe that it's writing its result
  while (active_workers > 0) {
    int wid = shared_pipe.read_obj<char> ();
    assert ((wid >= 0) && (wid < worker_count));

    pipe_t& to_main = workers[wid].to_main;
    pipe_t& from_main = workers[wid].from_main;

    to_main.assert_read<int> (MESSAGE_START);
    char has_game = !to_main.read_obj<char> ();

    if (has_game) {
      aut_ret game = to_main.read_result (dict);

      if (game.safe) {
        if (game.solved) {
          verb_do(1, vout << "Solved game -> add as result\n");
          add_result (game);
        } else {
          base_remaining--;
          verb_do(1, vout << "Unsolved game -> add solve job\n");
          enqueue (std::make_shared<job_solve> (game));
        }
      } else {
        losing = true;
        verb_do(1, vout << "Game not realizable -> abort!\n");
      }
    } else {
      // invariant
      base_remaining--;
      bdd inv = to_main.read_bdd (dict);
      verb_do(1, vout << "Read invariant: " << bdd_to_formula (inv) << "\n");
      add_invariant (inv);
    }

    if (base_remaining == 0) {
      base_remaining = -1;
      finish_invariant ();
    }

    // kill all workers and immediately abort if found to be losing
    if (losing) {
      for(int i = 0; i < worker_count; i++) {
        if (!workers[i].active) continue;
        kill (workers[i].pid, SIGKILL);
        waitpid (workers[i].pid, nullptr, 0);
      }
      break;
    }

    to_main.assert_read<int> (MESSAGE_END);
    verb_do(1, vout << "Done: read " << to_main.get_bytes_count () << " bytes from pipe\n");


    job_ptr new_job = dequeue ();
    if ((new_job == nullptr) || losing) {
      active_workers--;
      verb_do(1, vout << "Releasing worker " << wid << ": " << active_workers << " left.\n");
      workers[wid].active = false;

      from_main.write_obj<job_type> (e_done); // done!
      // wait for this process
      waitpid (workers[wid].pid, nullptr, 0);
    } else {
      // send new job
      new_job->to_pipe (from_main);
    }
  }


  utils::vout << "All workers are finished.\n";

  return epilogue (synth_fname);
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
  const auto build_aut = [&] (unsigned state, bdd saved_o,
                         const auto& recurse) {
    auto cached = cache.get (state, saved_o.id ());
    if (cached) return *cached;
    auto ret_state = ret->new_state ();
    cache (ret_state, state, saved_o.id ());
    for (auto& e : aut->out (state)) {

      for (auto&& one_input_bdd : minterms_of (e.cond, all_inputs)) {
        // Pick one satisfying assignment where outputs all have values
        ret->new_edge (ret_state,
        recurse (e.dst,
        bdd_exist (e.cond & one_input_bdd,
        all_inputs),
        recurse),
        saved_o & one_input_bdd,
        e.acc);
      }
    }
    return ret_state;
  };
  build_aut (aut->get_init_state_number (), bddtrue, build_aut);
  return ret;
}

aut_ret composition_mt::prepare_formula (spot::formula f, bool check_real, unreal_x_t opt_unreal_x) {
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

  if (want_time)
    sw.start ();

  ////////////////////////////////////////////////////////////////////////
  // Translate the formula to a UcB (Universal co-Büchi)
  // To do so, negate formula, and convert to a normal Büchi.
  if (check_real)
    f = spot::formula::Not (f);
  else if (opt_unreal_x == UNREAL_X_FORMULA) {
    // Add X at the outputs
    auto rec = [this] (auto&& self, spot::formula m) {
      if (m.is (spot::op::ap) and
      (std::ranges::find (output_aps_,
       m.ap_name ()) != output_aps_.end ()))
        return spot::formula::X (m);
      return m.map ([&] (spot::formula t) { return self (self, t); });
    };
    f = f.map ([&] (spot::formula t) { return rec (rec, t); });
    // Swap I and O.
    input_aps_.swap (output_aps_);
  }

  verb_do (1, vout << "Formula: " << f << std::endl);

  auto aut = trans_.run (&f);

  // If unreal but we haven't pushed outputs yet using X on formula
  if (not check_real and opt_unreal_x == UNREAL_X_AUTOMATON) {
    aut = push_outputs (aut, all_inputs, all_outputs);
    input_aps_.swap (output_aps_);
    std::swap (all_inputs, all_outputs); // can only happen once because no composition with unrealizability -> no problem
  }

  if (want_time) {
    double trans_time = sw.stop ();
    utils::vout << "Translating formula done in "
                << trans_time << " seconds\n";
    utils::vout << "Automaton has " << aut->num_states ()
                << " states and " << aut->num_sets () << " colors\n";
  }

  ////////////////////////////////////////////////////////////////////////
  // Preprocess automaton

  if (want_time) {
    sw.start();
    sw_nospot.start ();
  }

  auto aut_preprocessors_maker = AUT_PREPROCESSOR ();
  (aut_preprocessors_maker.make (aut, all_inputs, all_outputs, opt_K)) ();

  if (want_time) {
    double merge_time = sw.stop();
    utils::vout << "Preprocessing done in " << merge_time
                << " seconds\nDPA has " << aut->num_states()
                << " states\n";
  }
  verb_do (2, spot::print_hoa (utils::vout, aut, nullptr));

  ////////////////////////////////////////////////////////////////////////
  // Boolean states

  if (want_time)
    sw.start ();

  auto boolean_states_maker = BOOLEAN_STATES ();
  vectors::bool_threshold = (boolean_states_maker.make (aut, opt_K)) ();

  if (want_time) {
    double boolean_states_time = sw.stop ();
    utils::vout << "Computation of boolean states in " << boolean_states_time
                << "seconds , found " << vectors::bool_threshold << " nonboolean states.\n";
  }

  // Special case: only boolean states, so... no useful accepting state.
  if (vectors::bool_threshold == 0) {
    if (want_time)
      utils::vout << "Time disregarding Spot translation: " << sw_nospot.stop () << " seconds\n";
    aut_ret ret;
    ret.aut = nullptr;
    return ret;
  }


  ////////////////////////////////////////////////////////////////////////
  // Build S^K_N game, solve it.

  //if (want_time)
  //  sw.start ();

  aut_ret ret;
  ret.aut = aut;
  ret.bool_threshold = vectors::bool_threshold;
  ret.solved = false;
  ret.set_globals ();

  auto all_k = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), opt_Kmin - 1);
  for (size_t i = vectors::bool_threshold; i < aut->num_states (); ++i)
    all_k[i] = 0;
  ret.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (all_k));


  if (want_time) {
    double solve_time = sw.stop ();
    utils::vout << "Safety game created in " << solve_time << " seconds\n";
    utils::vout << "Time disregarding Spot translation: " << sw_nospot.stop () << " seconds\n";
  }

  timer.stop ();

  return ret;
}