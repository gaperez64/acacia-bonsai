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

unsigned _opt_K, _opt_Kmin, _opt_Kinc;

class job_base;

using job_ptr = std::shared_ptr<job_base>;

class composition_mt {
  private:
  std::queue<job_ptr> pending_jobs;
  std::shared_ptr<aut_ret> stored_result;
  bool losing = false;

  bdd invariant = bddtrue;
  aut_ret invariant_aut;


  public:
  composition_mt (unsigned opt_K, unsigned opt_Kmin, unsigned opt_Kinc) {
    _opt_K = opt_K;
    _opt_Kmin = opt_Kmin;
    _opt_Kinc = opt_Kinc;
  }

  void add_game (aut_ret& t);
  void enqueue (job_ptr p);
  void add_invariant (bdd inv, aut_ret aut);
  void finish_invariant ();

  job_ptr dequeue ();

  void add_result (aut_ret& r);

  int run (int threads, std::string synth_fname);
};


class job_base {
  public:
  virtual void run (pipe_t&, pipe_t&, int) = 0;
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

  virtual void run (pipe_t&, pipe_t&, int) override;
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



void shrink_safe (aut_ret& game) {
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
        (game.aut, _opt_Kmin, _opt_K, _opt_Kinc, game.all_inputs, game.all_outputs);
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
      (game.aut, _opt_Kmin, _opt_K, _opt_Kinc, game.all_inputs, game.all_outputs);
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

void job_solve::run (pipe_t& pipe, pipe_t& shared_pipe, int id) {
  // solve
  utils::vout << "Starting solve on automaton with " << starting_point.aut->num_states() << " states\n";

  //utils::vout << starting_point.bool_threshold << " (nonbool)\n";
  //custom_print (utils::vout, starting_point.aut);

  //utils::vout << "F: " << *(starting_point.safe);
  shrink_safe (starting_point);

  shared_pipe.write_obj<char> (id);

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
void composition_mt::enqueue (job_ptr p) {
  bdd condition;
  if (is_invariant(p->get_aut().aut, condition)) {
    utils::vout << "Found invariant: " << spot::bdd_to_formula (condition, p->get_aut ().aut->get_dict ()) << "\n";
    add_invariant (condition, p->get_aut ());
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

void composition_mt::add_invariant (bdd inv, aut_ret aut) {
  invariant &= inv;
  invariant_aut = aut;
  if (invariant == bddfalse) losing = true;
}

void composition_mt::finish_invariant() {
  if (invariant != bddtrue) {
    spot::twa_graph_ptr aut = invariant_aut.aut;
    //utils::vout << "Adding invariant: " << spot::bdd_to_formula (invariant, aut->get_dict ()) << "\n";

    unsigned init = aut->get_init_state_number();
    unsigned other = 1-init;

    for(auto& edge: aut->edges()) {
      if ((edge.src == init) && (edge.dst == init)) {
        edge.cond = invariant;
      }
      else if ((edge.src == init) && (edge.dst == other)) {
        edge.cond = !invariant;
      }
    }

    invariant_aut.solved = true;

    auto safe = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), 0);
    safe[other] = -1;
    invariant_aut.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (safe));

    add_result (invariant_aut);
  }
}

struct worker_t {
  pipe_t pipe;
  pid_t pid = -1;
  bool orig = false;
  aut_ret game;
};

void be_child(pipe_t& my_pipe, pipe_t& shared_pipe, job_ptr job, int id) {
  utils::vout.set_prefix ("[" + std::to_string (id+1) + "]");

  my_pipe.write_obj<int> (RESULT_START);
  job->run (my_pipe, shared_pipe, id);
  my_pipe.write_obj<int> (RESULT_END);

  utils::vout << "Done: wrote " << my_pipe.get_bytes_count () << " bytes to pipe\n";
  //shared_pipe.write_obj<char> (id);

  exit(0);
}

int composition_mt::run (int threads, std::string synth_fname) {
  utils::vout.set_prefix ("[0]");

  //threads = 1;
  if (threads <= 0) {
    threads = std::thread::hardware_concurrency ();
  }
  utils::vout << "Workers: " << threads << "\n";

  threads = std::min<int> (threads, pending_jobs.size ());
  assert (threads >= 0);


  finish_invariant ();

  // create shared pipe
  pipe_t shared_pipe;
  assert (pipe ((int*)&shared_pipe) == 0);

  // for each worker: a pipe for them to send the result,
  // a value to store their process ID, and what automaton they are currently working on
  std::vector<worker_t> workers (threads);
  for(int i = 0; i < threads; i++) {
    assert (pipe ((int*)&workers[i].pipe) == 0);
    //int new_size = fcntl(workers[i].pipe.w, F_SETPIPE_SZ, 1024*1024);
    //assert(new_size != -1);
    //utils::vout << "Pipe size: " << new_size << "\n";
  }

  // spawn the workers with their initial job
  for(int i = 0; i < threads; i++) {
    job_ptr job = dequeue ();
    assert (job != nullptr);

    pid_t pid = fork ();
    assert (pid >= 0);

    if (pid > 0) {
      // parent process
      workers[i].pid = pid;
      workers[i].game = job->get_aut ();
      workers[i].orig = job->is_original ();
      //utils::vout << "Orig: " << job->is_original() << "\n";
    }
    else {
      // child process
      be_child (workers[i].pipe, shared_pipe, job, i);
    }
  }

  int active_workers = threads;

  // wait until a process writes to the shared pipe that it's writing its result
  while (active_workers > 0) {
    int wid = shared_pipe.read_obj<char> ();
    assert((wid >= 0) && (wid < threads));

    pipe_t& my_pipe = workers[wid].pipe;
    utils::vout << "Wid " << wid << ": pipe " << my_pipe.r << "\n";

    /*
    while (true) {
      unsigned char c = my_pipe.read_obj<char>();
      printf("%02x ", c);
      fflush(stdout);
    }
    */

    my_pipe.assert_read<int> (RESULT_START);
    char result = my_pipe.read_obj<char> (); // 0 if no downset (unrealizable), otherwise 1 if solved

    if (result == 0) {
      // unrealizable!
      losing = true;
      utils::vout << "Game not realizable -> abort!\n";
    }
    else if (result == 1) {
      aut_ret game = workers[wid].game;
      game.safe = my_pipe.read_downset ();
      game.solved = true;
      //utils::vout << "Downset: " << *game.safe << "\n";
      add_result (game);
    }
    else assert(false);

    my_pipe.assert_read<int> (RESULT_END);
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
      workers[wid].game = new_job->get_aut ();
      workers[wid].orig = new_job->is_original ();
    }
    else {
      be_child (workers[wid].pipe, shared_pipe, new_job, wid);
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
    shrink_safe (r);
  }

  if (!r.safe) {
    utils::vout << "Final result is not realizable!\n";
    return 0;
  }

  //utils::vout << "Realizable, safe region: " << *r.safe << "\n";
  if (!synth_fname.empty()) {
    auto skn = K_BOUNDED_SAFETY_AUT_IMPL<GenericDownset>
    (r.aut, _opt_Kmin, _opt_K, _opt_Kinc, r.all_inputs, r.all_outputs);
    skn.synthesis (*r.safe, synth_fname);
  }


  return 1;
}