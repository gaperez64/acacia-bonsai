//
// Created by nils on 04/05/23.
//

#pragma once
#include "types.hh"
#include "composition.hh"
#include <queue>
#include <fcntl.h>
#include <thread>
#include "pipes.hh"


class job_base;

using job_ptr = std::shared_ptr<job_base>;

struct worker_t {
  pipe_t to_main, from_main;
  pid_t pid = -1;
  //aut_ret game;
};

class composition_mt {
  public:
  std::queue<job_ptr> pending_jobs;
  std::shared_ptr<aut_ret> stored_result;
  bool losing = false;

  pipe_t shared_pipe;
  std::vector<worker_t> workers;

  bdd invariant = bddtrue;
  //aut_ret invariant_aut;

  bdd all_inputs, all_outputs;
  std::vector<bdd> all_inputs_v, all_outputs_v;
  spot::bdd_dict_ptr dict;
  unsigned _opt_K, _opt_Kmin, _opt_Kinc;


  public:
  composition_mt (unsigned opt_K, unsigned opt_Kmin, unsigned opt_Kinc,
      bdd all_inputs, std::vector<bdd> all_inputs_v, bdd all_outputs, std::vector<bdd> all_outputs_v, spot::bdd_dict_ptr dict):
        all_inputs(all_inputs), all_outputs(all_outputs), all_inputs_v(all_inputs_v), all_outputs_v(all_outputs_v), dict(dict) {
    _opt_K = opt_K;
    _opt_Kmin = opt_Kmin;
    _opt_Kinc = opt_Kinc;
  }

  spot::formula bdd_to_formula (bdd f) const;
  void add_game (aut_ret& t);
  void enqueue (job_ptr p);
  void add_invariant (bdd inv);
  void finish_invariant ();
  void be_child (job_ptr job, int id);

  job_ptr dequeue ();

  void add_result (aut_ret& r);

  int run (int workers, std::string synth_fname);
};


class job_base {
  public:
  virtual void run (pipe_t&, pipe_t&, int, composition_mt*) = 0;
  virtual std::string name () = 0;
  virtual aut_ret get_aut () = 0;
  virtual bool is_original () = 0;
  virtual ~job_base () = default;
};


// solve the safety game, changing the downset to the actual safe region instead of
// an overapproximation
class job_solve: public job_base {
  public:
  aut_ret starting_point;
  bool original;

  public:
  job_solve (aut_ret& game, bool original);

  virtual void run (pipe_t&, pipe_t&, int, composition_mt*) override;
  virtual std::string name () override;
  virtual aut_ret get_aut () override;
  virtual bool is_original () override;
  virtual ~job_solve () override = default;
};

//////////////////////////////////////////////////



job_solve::job_solve (aut_ret& game, bool orig) {
  starting_point = std::move (game);
  original = orig;
}



void shrink_safe (aut_ret& game, composition_mt* m) {
  spot::stopwatch sw;
  sw.start ();

  auto [nbitsetbools, actual_nonbools] = game.set_globals();

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
        (game.aut, m->_opt_Kmin, m->_opt_K, m->_opt_Kinc, m->all_inputs, m->all_outputs);
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
      (game.aut, m->_opt_Kmin, m->_opt_K, m->_opt_Kinc, m->all_inputs, m->all_outputs);
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
  utils::vout << "Safety game solved in " << solve_time << " seconds\n";
}

bool is_invariant(spot::twa_graph_ptr aut, bdd& condition) {
  if (aut->num_states() != 2) return false;
  unsigned init = aut->get_init_state_number();
  unsigned other = 1-init;
  if (aut->state_is_accepting (init)) return false;
  if (!aut->state_is_accepting (other)) return false;

  bdd condition_neg;

  int edges = 0;

  for(auto& edge: aut->edges()) {
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

void job_solve::run (pipe_t& pipe, pipe_t& shared_pipe, int id, composition_mt* m) {
  // solve
  utils::vout << "Starting solve on automaton with " << starting_point.aut->num_states() << " states\n";

  //utils::vout << starting_point.bool_threshold << " (nonbool)\n";
  //custom_print (utils::vout, starting_point.aut);

  //utils::vout << "F: " << *(starting_point.safe);
  shrink_safe (starting_point, m);

  shared_pipe.write_obj<char> (id);

  /*
  if (starting_point.safe) {
    //utils::vout << "Solved into " << *(starting_point.safe);
    utils::vout << "Pipe.w: " << pipe.w << "\n";
    pipe.write_obj<char> (1); // solved it
    pipe.write_downset (*starting_point.safe);
  }
  else {
    utils::vout << "Solved but not winning\n";
    pipe.write_obj<char> (0); // no downset
  }
  */
  pipe.write_result (starting_point);

}

std::string job_solve::name () {
  return "SOLVE";
}

aut_ret job_solve::get_aut () {
  return starting_point;
}

bool job_solve::is_original () {
  return original;
}



//////////////////////////////////////////////////



void composition_mt::add_game (aut_ret& t) {
  enqueue (std::make_shared<job_solve> (t, true));
}

spot::formula composition_mt::bdd_to_formula (bdd f) const {
  return spot::bdd_to_formula (f, dict);
}

void composition_mt::enqueue (job_ptr p) {
  bdd condition;
  if (is_invariant(p->get_aut().aut, condition)) {
    utils::vout << "Found invariant: " << spot::bdd_to_formula (condition, p->get_aut ().aut->get_dict ()) << "\n";
    add_invariant (condition);
    return;
  }

  utils::vout << "New job added to queue: " << p->name () << "\n";
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
  utils::vout << "Adding result to T..\n";
  if (!stored_result) {
    stored_result = std::make_shared<aut_ret> (r);
  }
  else {
    //utils::vout << "Adding new merge job\n";
    //enqueue (std::make_shared<job_merge> (*stored_result, r));

    aut_ret inputs[2];
    inputs[0] = *stored_result;
    inputs[1] = r;

    if (!inputs[0].safe) {
      utils::vout << "Merging invalid 0? abort\n";
      return;
    }
    if (!inputs[1].safe) {
      utils::vout << "Merging invalid 1? abort\n";
      return;
    }

    utils::vout << "Merging " << *inputs[0].safe << " and " << *inputs[1].safe;

    auto composer = composition ();
    composer.merge_aut (inputs[0], inputs[1]);
    inputs[0].safe = std::make_shared<GenericDownset> (composer.merge_saferegions (*inputs[0].safe, *inputs[1].safe));
    inputs[0].solved = false;

    //m->add_result (inputs[0]);
    if (inputs[0].safe) utils::vout << "Merge res: " << *(inputs[0].safe);
    utils::vout << "Done with merge, adding solve job\n";
    inputs[0].solved = false;
    enqueue (std::make_shared<job_solve> (inputs[0], false));

    stored_result = nullptr;
  }
}

void composition_mt::add_invariant (bdd inv) {
  invariant &= inv;
  if (invariant == bddfalse) losing = true;
}

void composition_mt::finish_invariant() {
  if (invariant != bddtrue) {
    aut_ret invariant_aut;
    invariant_aut.bool_threshold = 1;

    spot::twa_graph_ptr aut = std::make_shared<spot::twa_graph> (dict);
    aut->set_generalized_buchi (1);
    aut->set_acceptance (spot::acc_cond::inf ({0}));
    aut->prop_state_acc (true);
    aut->new_states (2);
    aut->set_init_state (1);
    //utils::vout << "Adding invariant: " << spot::bdd_to_formula (invariant, aut->get_dict ()) << "\n";

    aut->new_edge (1, 1, invariant);
    aut->new_edge (1, 0, !invariant);
    aut->new_acc_edge (0, 0, bddtrue);
    unsigned init = aut->get_init_state_number();
    unsigned other = 1-init;

    invariant_aut.solved = true;

    auto safe = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), 0);
    safe[other] = -1;
    invariant_aut.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (safe));
    invariant_aut.aut = aut;

    //custom_print(utils::vout, aut);

    add_result (invariant_aut);
  }
}



void composition_mt::be_child (job_ptr job, int id) {
  utils::vout.set_prefix ("[" + std::to_string (id+1) + "] ");

  pipe_t& my_pipe = workers[id].to_main;

  my_pipe.write_obj<int> (MESSAGE_START);
  job->run (my_pipe, shared_pipe, id, this);
  my_pipe.write_obj<int> (MESSAGE_END);

  utils::vout << "Done: wrote " << my_pipe.get_bytes_count () << " bytes to pipe\n";
  exit(0);
}

int composition_mt::run (int worker_count, std::string synth_fname) {
  utils::vout.set_prefix ("[0] ");

  if (worker_count <= 0) {
    worker_count = std::thread::hardware_concurrency ();
  }
  utils::vout << "Workers: " << worker_count << "\n";

  worker_count = std::min<int> (worker_count, pending_jobs.size ());
  assert (worker_count >= 0);

  finish_invariant ();

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

  // spawn the workers with their initial job
  for(int i = 0; i < worker_count; i++) {
    job_ptr job = dequeue ();
    assert (job != nullptr);

    pid_t pid = fork ();
    assert (pid >= 0);

    if (pid > 0) {
      // parent process
      workers[i].pid = pid;
      //dict->dump(utils::vout);
      //workers[i].game = job->get_aut ();
      //workers[i].orig = job->is_original ();
      //utils::vout << "Orig: " << job->is_original() << "\n";
    }
    else {
      // child process
      be_child (job, i);
    }
  }

  int active_workers = worker_count;
  // wait until a process writes to the shared pipe that it's writing its result
  while (active_workers > 0) {
    int wid = shared_pipe.read_obj<char> ();
    assert((wid >= 0) && (wid < worker_count));

    pipe_t& my_pipe = workers[wid].to_main;
    utils::vout << "Wid " << wid << ": pipe " << my_pipe.r << "\n";

    /*
    while (true) {
      unsigned char c = my_pipe.read_obj<char>();
      printf("%02x ", c);
      fflush(stdout);
    }
    */

    my_pipe.assert_read<int> (MESSAGE_START);
    aut_ret game = my_pipe.read_result (dict);


    if (game.safe) {
      game.solved = true;
      add_result (game);
    } else {
      losing = true;
      utils::vout << "Game not realizable -> abort!\n";
    }

    my_pipe.assert_read<int> (MESSAGE_END);
    utils::vout << "Done: read " << my_pipe.get_bytes_count () << " bytes from pipe\n";

    // wait for this process
    waitpid(workers[wid].pid, nullptr, 0);

    job_ptr new_job = dequeue ();
    if ((new_job == nullptr) || losing) {
      active_workers--;
      continue;
    }

    pid_t pid = fork ();
    assert (pid >= 0);

    if (pid > 0) {
      // parent process
      workers[wid].pid = pid;
      //workers[wid].game = new_job->get_aut ();
      //workers[wid].orig = new_job->is_original ();
    }
    else {
      be_child (new_job, wid);
    }
  }


  utils::vout << "All workers are finished.\n";

  if (losing) {
    utils::vout << "Losing!\n";
    return 0;
  }

  // check stored_result
  if (!stored_result) {
    utils::vout << "No result?\n";
    return 0;
  }

  aut_ret& r = *stored_result;

  if (!r.solved) {
    shrink_safe (r, this);
  }

  if (!r.safe) {
    utils::vout << "Final result is not realizable!\n";
    return 0;
  }

  //utils::vout << "Realizable, safe region: " << *r.safe << "\n";
  if (!synth_fname.empty()) {
    r.set_globals ();
    auto skn = K_BOUNDED_SAFETY_AUT_IMPL<GenericDownset>
    (r.aut, _opt_Kmin, _opt_K, _opt_Kinc, all_inputs, all_outputs);
    skn.synthesis (*r.safe, synth_fname);
  }


  return 1;
}