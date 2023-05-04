//
// Created by nils on 04/05/23.
//

#pragma once
#include "types.hh"
#include "composition.hh"
#include <queue>
#include <thread>
#include <mutex>

unsigned _opt_K, _opt_Kmin, _opt_Kinc;
thread_local std::ostream* ost = &std::cout;

class job_base;

using job_ptr = std::shared_ptr<job_base>;

class composition_mt {
  private:
  std::queue<job_ptr> pending_jobs;
  std::mutex m;
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
  void enqueue (job_ptr p, bool already_locked = false);
  void lose ();
  void add_invariant (bdd inv, aut_ret& aut);

  job_ptr dequeue ();

  void add_result (aut_ret& r);

  int run (int threads, std::string synth_fname);
};


class job_base {
  public:
  virtual void run (composition_mt*) = 0;
  virtual std::string name () = 0;
  virtual ~job_base() = default;
};


// solve the safety game, changing the downset to the actual safe region instead of
// an overapproximation
class job_solve: public job_base {
  private:
  aut_ret starting_point;

  public:
  job_solve (aut_ret& game);

  virtual void run (composition_mt*) override;
  virtual std::string name () override;
  virtual ~job_solve () override = default;
};

// merge two automata and their safe regions (into an overapproximation)
class job_merge: public job_base {
  private:
  aut_ret inputs[2];

  public:
  job_merge (aut_ret& game1, aut_ret& game2);

  virtual void run (composition_mt*);
  virtual std::string name () override;
  virtual ~job_merge () override = default;
};

class worker {
  private:
  composition_mt* parent;
  int tid;
  std::ofstream f;

  public:
  worker (composition_mt* parent, int tid);

  void operator() ();
};

//////////////////////////////////////////////////

worker::worker(composition_mt* parent, int tid): parent(parent), tid(tid) {
  f.open("../T-"+std::to_string(tid)+".txt");
}

void worker::operator() () {
  //ost = &f;
  _tid = tid;
  // dequeue job from parent, run it
  job_ptr job;
  while (job = parent->dequeue ()) {
    *ost << "pop " << job->name() << "!\n";
    job->run(parent);
  }
  *ost << "Done!\n";
}



//////////////////////////////////////////////////



job_solve::job_solve (aut_ret& game) {
  starting_point = std::move (game);
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
  *ost << "Safety game solved in " << solve_time << " seconds\n";
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

void job_solve::run (composition_mt* m) {
  // solve
  *ost << "Starting solve on automaton with " << starting_point.aut->num_states() << " states\n";

  bdd condition;
  if (is_invariant(starting_point.aut, condition)) {
    *ost << "Found invariant: " << spot::bdd_to_formula (condition, starting_point.aut->get_dict ()) << "\n";
    m->add_invariant(condition, starting_point);
    return;
  }

  *ost << starting_point.bool_threshold << " (nonbool)\n";
  custom_print (*ost, starting_point.aut);

  *ost << "F: " << *(starting_point.safe);
  shrink_safe (starting_point);
  *ost << "P: " << &vectors::bool_threshold << " = " << vectors::bool_threshold << "\n";

  if (starting_point.safe) {
    *ost << "Solved into " << *(starting_point.safe);
    m->add_result (starting_point);
  }
  else {
    *ost << "Solved but not winning?\n";
    m->lose ();
  }

}

std::string job_solve::name () {
  return "SOLVE";
}





job_merge::job_merge (aut_ret& game1, aut_ret& game2) {
  inputs[0] = std::move (game1);
  inputs[1] = std::move (game2);
}

void job_merge::run (composition_mt* m) {
  // merge
  if (!inputs[0].safe) {
    *ost << "Merging invalid 0? abort\n";
    return;
  }
  if (!inputs[1].safe) {
    *ost << "Merging invalid 1? abort\n";
    return;
  }

  *ost << "Merging " << *inputs[0].safe << " and " << *inputs[1].safe;

  auto composer = composition ();
  composer.merge_aut (inputs[0], inputs[1]);
  inputs[0].safe = std::make_shared<GenericDownset> (composer.merge_saferegions (*inputs[0].safe, *inputs[1].safe));
  inputs[0].solved = false;

  //m->add_result (inputs[0]);
  if (inputs[0].safe) *ost << "Merge res: " << *(inputs[0].safe);
  *ost << "Done with merge, adding solve job\n";
  m->enqueue (std::make_shared<job_solve> (inputs[0]));
}

std::string job_merge::name () {
  return "MERGE";
}




//////////////////////////////////////////////////



void composition_mt::add_game (aut_ret& t) {
  enqueue (std::make_shared<job_solve> (t));
}
void composition_mt::enqueue (job_ptr p, bool already_locked) {
  *ost << "New job added to queue: " << p->name() << "\n";
  if (already_locked) {
    pending_jobs.push(p);
  }
  else {
    std::unique_lock<std::mutex> lock (m);
    pending_jobs.push(p);
  }
}

job_ptr composition_mt::dequeue () {
  std::unique_lock<std::mutex> lock (m);
  if (pending_jobs.empty ()) {
    // done
    return nullptr;
  }
  auto val = pending_jobs.front ();
  pending_jobs.pop ();
  return val;
}

void composition_mt::add_result (aut_ret& r) {
  std::unique_lock<std::mutex> lock (m);
  *ost << "Adding result to T..\n";
  if (!stored_result) {
    stored_result = std::make_shared<aut_ret>(r);
  }
  else {
    *ost << "Adding new merge job\n";
    enqueue (std::make_shared<job_merge> (*stored_result, r), true);
    stored_result = nullptr;
  }
}

void composition_mt::lose () {
  std::unique_lock<std::mutex> lock (m);
  losing = true;
}

void composition_mt::add_invariant (bdd inv, aut_ret& aut) {
  std::unique_lock<std::mutex> lock (m);
  invariant &= inv;
  invariant_aut = std::move(aut);
  if (invariant == bddfalse) losing = true;
}

int composition_mt::run (int threads, std::string synth_fname) {
  ost = &utils::vout;

  if (threads > 1) {
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < threads; i++) {
      worker_threads.push_back (std::thread (worker (this, i + 1)));
    }

    // wait for each thread to finish
    for (int i = 0; i < threads; i++) {
      worker_threads[i].join ();
    }
  }
  else {
    auto w = worker (this, 1);
    w ();
    _tid = 0;
  }

  utils::vout << "All workers are finished.\n";

  if (invariant != bddtrue) {
    spot::twa_graph_ptr aut = invariant_aut.aut;
    utils::vout << "Adding invariant: " << spot::bdd_to_formula (invariant, aut->get_dict ()) << "\n";

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

    custom_print (utils::vout, aut);

    invariant_aut.solved = true;

    auto safe = utils::vector_mm<VECTOR_ELT_T> (aut->num_states (), 0);
    safe[other] = -1;
    invariant_aut.safe = std::make_shared<GenericDownset> (GenericDownset::value_type (safe));

    add_result (invariant_aut);

    auto w = worker (this, 1);
    w ();
    _tid = 0;
  }

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

  utils::vout << "Realizable, safe region: " << *r.safe << "\n";
  if (!synth_fname.empty()) {
    auto skn = K_BOUNDED_SAFETY_AUT_IMPL<GenericDownset>
    (r.aut, _opt_Kmin, _opt_K, _opt_Kinc, r.all_inputs, r.all_outputs);
    skn.synthesis (*r.safe, synth_fname);
  }


  return 1;
}