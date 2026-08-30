#pragma once

#include "configuration.hh"

#include <spot/twa/fwd.hh>

#include <string>

#if ACACIA_ENABLE_DIAGNOSTICS

# include <cstdlib>
# include <filesystem>
# include <fstream>
# include <posets/vectors.hh>
# include <spot/twa/twagraph.hh>
# include <spot/twaalgos/hoa.hh>
# include <iterator>
# include <string>
# include <vector>
# include <string_view>
# include <unistd.h>

namespace acacia::antichain_snapshot {

  /// Bumped whenever the on-disk layout changes in a way a reader must notice.
  /// 1: automaton.hoa + meta.tsv + antichain-<loop>.tsv.
  /// 2: meta.tsv gains schema_version; cpre-<loop>.tsv records one complete
  ///    input-conditioned update -- the region before, the ordered action list,
  ///    and the region after -- which is what an offline replay needs.
  /// 3: meta.tsv gains init_state, so the initial rank vector is recoverable
  ///    without parsing the HOA; antichain-<loop>.tsv is triggered by frontier
  ///    size rather than only by loop number, and records whether the bound was
  ///    raised on the way in; all-input-actions.tsv optionally records every
  ///    input class with its ordered actions.
  inline constexpr int SCHEMA_VERSION = 3;

  /// The CPre event has its own version, because its layout is independent of
  /// the directory's: bumping the directory schema for an unrelated addition
  /// must not make an unchanged event file unreadable.
  inline constexpr int CPRE_SCHEMA_VERSION = 2;

  namespace detail {

    struct snapshot_state {
        std::string root;
        std::string directory;
        size_t every = 1;
        size_t maximum = 20;
        size_t dumps = 0;
        size_t automata = 0;
        // CPre events are opt-in and separately capped: one event carries the
        // whole action list for its input class, which is the largest thing
        // this file can be asked to write.
        // Frontier-size checkpoints.  Sampling by loop number tells you when
        // a dump happened; sampling by frontier size tells you what the region
        // looked like, which is the quantity every later question is about.
        std::vector<size_t> size_marks {16, 64, 256, 1024, 4096, 16384};
        std::vector<bool> mark_taken;
        bool first_loop_taken = false;
        bool bound_raised = false;
        bool all_actions = false;
        bool all_actions_written = false;
        size_t max_inputs = 4096;
        size_t max_actions = 65536;
        size_t max_transitions = 4000000;
        bool cpre = false;
        size_t cpre_max_actions = 4096;
        size_t cpre_dumps = 0;
        size_t cpre_maximum = 8;
        std::ofstream cpre_out;
    };

    inline snapshot_state& state () {
      static snapshot_state value;
      return value;
    }

    inline size_t env_size (const char* name, size_t fallback, bool allow_zero) {
      const char* text = std::getenv (name);
      if (text == nullptr or *text == '\0')
        return fallback;
      char* end = nullptr;
      const unsigned long value = std::strtoul (text, &end, 10);
      if (*end != '\0' or (value == 0 and not allow_zero))
        return fallback;
      return static_cast<size_t> (value);
    }

  }  // namespace detail

  inline void configure (const spot::const_twa_graph_ptr& aut) {
    detail::snapshot_state& snapshot = detail::state ();
    snapshot.directory.clear ();
    snapshot.dumps = 0;

    const char* directory = std::getenv ("ACACIA_ANTICHAIN_SNAPSHOT_DIR");
    if (directory == nullptr or *directory == '\0')
      return;

    snapshot.root = directory;
    snapshot.every =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_EVERY", 1, false);
    snapshot.maximum =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_MAX", 20, true);
    const char* cpre = std::getenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE");
    snapshot.cpre = cpre != nullptr and *cpre != '\0' and std::string_view {cpre} != "0";
    snapshot.cpre_max_actions =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX_ACTIONS", 4096, false);
    snapshot.cpre_maximum =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX", 8, true);
    snapshot.cpre_dumps = 0;
    snapshot.cpre_out.close ();
    snapshot.cpre_out.clear ();
    snapshot.mark_taken.assign (snapshot.size_marks.size (), false);
    snapshot.first_loop_taken = false;
    snapshot.bound_raised = false;
    snapshot.all_actions_written = false;
    const char* all_actions = std::getenv ("ACACIA_ANTICHAIN_SNAPSHOT_ALL_ACTIONS");
    snapshot.all_actions =
        all_actions != nullptr and *all_actions != '\0' and std::string_view {all_actions} != "0";
    snapshot.max_inputs =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_ALL_ACTIONS_MAX_INPUTS", 4096, false);
    snapshot.max_actions =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_ALL_ACTIONS_MAX_ACTIONS", 65536, false);
    snapshot.max_transitions =
        detail::env_size ("ACACIA_ANTICHAIN_SNAPSHOT_ALL_ACTIONS_MAX_TRANSITIONS", 4000000, false);

    // One run can solve several automata, and they do not even share a process:
    // the parent forks one child per real/unreal strategy, and DECOMPOSE_SPEC
    // splits the spec further within a child.  Each carries its own state count
    // and bool_threshold, so a shared directory mixes vectors of different
    // lengths under one automaton.hoa -- and a per-process counter alone would
    // still collide, since every child starts it at zero.  Key on the pid too.
    snapshot.directory = snapshot.root + "/aut-" +
                         std::to_string (static_cast<long> (::getpid ())) + "-" +
                         std::to_string (snapshot.automata++);
    std::filesystem::create_directories (snapshot.directory);

    std::ofstream hoa {snapshot.directory + "/automaton.hoa"};
    spot::print_hoa (hoa, aut);

    // The schema version is what lets a replay tool refuse data it cannot
    // read, rather than silently misparsing an older dump.
    // init_state is all the initial rank vector needs: it is -1 everywhere
    // except 0 at the automaton's initial state.
    std::ofstream meta {snapshot.directory + "/meta.tsv"};
    meta << "schema_version\tstates\tbool_threshold\tinit_state\n"
         << SCHEMA_VERSION << '\t' << aut->num_states () << '\t'
         << posets::vectors::bool_threshold << '\t'
         << aut->get_init_state_number () << '\n';
  }

  /// Tell the snapshot the bound was raised.  A bump re-inflates every maximum,
  /// so the region immediately after one is a deliberate over-approximation and
  /// is not a candidate for "is this already inductive"; the next checkpoint
  /// records that it followed one.
  inline void note_bound_raised () { detail::state ().bound_raised = true; }

  template <typename SetOfStates>
  inline void observe (const SetOfStates& f, int k, int loop) {
    detail::snapshot_state& snapshot = detail::state ();
    if (snapshot.directory.empty () or snapshot.dumps >= snapshot.maximum or loop < 0)
      return;

    // Take the first loop, then the first crossing of each frontier size, then
    // whatever `every` asks for on top.  Sampling by loop number says when a
    // dump happened; sampling by frontier size says what the region looked
    // like, which is what every later question is about.
    bool wanted = false;
    if (not snapshot.first_loop_taken) {
      snapshot.first_loop_taken = true;
      wanted = true;
    }
    for (size_t i = 0; i < snapshot.size_marks.size (); ++i)
      if (not snapshot.mark_taken[i] and f.size () >= snapshot.size_marks[i]) {
        snapshot.mark_taken[i] = true;
        wanted = true;
      }
    if (snapshot.every > 1 and static_cast<size_t> (loop) % snapshot.every == 0)
      wanted = true;
    if (not wanted)
      return;

    ++snapshot.dumps;
    std::ofstream output {snapshot.directory + "/antichain-" + std::to_string (loop) + ".tsv"};
    output << "# k=" << k << " loop=" << loop << " maxima=" << f.size ()
           << " after_bound_raise=" << (snapshot.bound_raised ? 1 : 0) << '\n';
    snapshot.bound_raised = false;
    for (const auto& maximum : f) {
      for (size_t i = 0; i < maximum.size (); ++i) {
        if (i != 0)
          output << '\t';
        output << static_cast<int> (maximum[i]);
      }
      output << '\n';
    }
  }

  /// Record the region the solver actually finished with.
  ///
  /// The size-crossing checkpoints say what the region looked like on the way
  /// up; this says where it landed. Without it an offline continuation has
  /// nothing exact to be checked against, which is the whole of Gate 0.
  template <typename SetOfStates>
  inline void record_final (const SetOfStates& f, int k, int loop) {
    detail::snapshot_state& snapshot = detail::state ();
    if (snapshot.directory.empty ())
      return;
    std::ofstream output {snapshot.directory + "/antichain-final.tsv"};
    output << "# k=" << k << " loop=" << loop << " maxima=" << f.size ()
           << " after_bound_raise=0 final=1\n";
    for (const auto& maximum : f) {
      for (size_t i = 0; i < maximum.size (); ++i) {
        if (i != 0)
          output << '\t';
        output << static_cast<int> (maximum[i]);
      }
      output << '\n';
    }
  }

  /// Record every input class with its ordered action list, once per automaton.
  ///
  /// The CPre event records the one input the picker selected, which is what a
  /// replay of that update needs.  A search for an inductive subregion instead
  /// has to check every input class against every candidate generator, so it
  /// needs the whole table.  It is independent of the region and of k: the same
  /// table serves every checkpoint.
  template <typename Actions>
  inline void record_all_input_actions (const Actions& inputs_to_actions) {
    detail::snapshot_state& snapshot = detail::state ();
    if (snapshot.directory.empty () or not snapshot.all_actions or snapshot.all_actions_written)
      return;
    snapshot.all_actions_written = true;

    const std::string path = snapshot.directory + "/all-input-actions.tsv";
    auto decline = [&] (const std::string& reason) {
      std::ofstream skipped {path + ".skipped"};
      skipped << reason << '\n';
    };

    size_t inputs = 0, actions = 0, transitions = 0;
    for (const auto& [input, action_vecs] : inputs_to_actions) {
      (void) input;
      ++inputs;
      for (const auto& avec : action_vecs) {
        ++actions;
        for (const auto& row : avec)
          transitions += row.size ();
      }
    }
    if (inputs > snapshot.max_inputs)
      return decline ("inputs=" + std::to_string (inputs) + " cap=" +
                      std::to_string (snapshot.max_inputs));
    if (actions > snapshot.max_actions)
      return decline ("actions=" + std::to_string (actions) + " cap=" +
                      std::to_string (snapshot.max_actions));
    if (transitions > snapshot.max_transitions)
      return decline ("transitions=" + std::to_string (transitions) + " cap=" +
                      std::to_string (snapshot.max_transitions));

    std::ofstream out {path};
    out << "# schema_version=1 inputs=" << inputs << " actions=" << actions
        << " transitions=" << transitions << '\n';
    size_t input_index = 0;
    for (const auto& [input, action_vecs] : inputs_to_actions) {
      (void) input;
      out << "[input\t" << input_index++ << "]\n";
      size_t action_index = 0;
      for (const auto& avec : action_vecs) {
        out << "action\t" << action_index++ << '\n';
        for (size_t i = 0; i < avec.size (); ++i)
          for (const auto& [j, increment] : avec[i])
            out << i << '\t' << j << '\t' << (increment ? 1 : 0) << '\n';
      }
    }
  }

  /// Open a CPre event for the update about to be applied, writing the region
  /// before it and the complete ordered action list.  `input` is provenance
  /// only: the action vectors already determine the update exactly, so a replay
  /// needs no BDD and no automaton.
  ///
  /// Returns false when nothing was opened, so the caller can skip the matching
  /// `record_cpre_after`.
  template <typename SetOfStates, typename Actions>
  inline bool record_cpre_before (const SetOfStates& f, int k, int loop,
                                  const std::string& input, const Actions& actions) {
    detail::snapshot_state& snapshot = detail::state ();
    if (snapshot.directory.empty () or not snapshot.cpre or
        snapshot.cpre_dumps >= snapshot.cpre_maximum)
      return false;
    // An input class with a very large action list would dominate the dump; the
    // cap is recorded rather than silently applied.
    const size_t action_count = std::distance (actions.begin (), actions.end ());
    if (action_count > snapshot.cpre_max_actions) {
      std::ofstream skipped {snapshot.directory + "/cpre-" + std::to_string (loop) +
                             ".skipped"};
      skipped << "actions=" << action_count << " cap=" << snapshot.cpre_max_actions << '\n';
      return false;
    }

    ++snapshot.cpre_dumps;
    snapshot.cpre_out.open (snapshot.directory + "/cpre-" + std::to_string (loop) + ".tsv");
    std::ofstream& out = snapshot.cpre_out;
    out << "# schema_version=" << CPRE_SCHEMA_VERSION << " loop=" << loop << " k=" << k
        << " actions=" << action_count << " before=" << f.size () << " input=" << input
        << '\n';

    out << "[before]\n";
    for (const auto& maximum : f) {
      for (size_t i = 0; i < maximum.size (); ++i) {
        if (i != 0)
          out << '\t';
        out << static_cast<int> (maximum[i]);
      }
      out << '\n';
    }

    // One action is a backward map: for each destination q, the sources p and
    // whether the edge increments.  That is the whole of what apply() reads.
    out << "[actions]\n";
    size_t index = 0;
    for (const auto& action_vec : actions) {
      out << "action\t" << index++ << '\n';
      for (size_t q = 0; q < action_vec.size (); ++q)
        for (const auto& [p, increment] : action_vec[q])
          out << q << '\t' << p << '\t' << (increment ? 1 : 0) << '\n';
    }
    return true;
  }

  /// Close the event opened by `record_cpre_before` with the region the solver
  /// actually produced.  A replay is correct only if it reproduces this.
  template <typename SetOfStates>
  inline void record_cpre_after (const SetOfStates& f) {
    detail::snapshot_state& snapshot = detail::state ();
    if (not snapshot.cpre_out.is_open ())
      return;
    std::ofstream& out = snapshot.cpre_out;
    out << "[after]\t" << f.size () << '\n';
    for (const auto& maximum : f) {
      for (size_t i = 0; i < maximum.size (); ++i) {
        if (i != 0)
          out << '\t';
        out << static_cast<int> (maximum[i]);
      }
      out << '\n';
    }
    out.close ();
  }

}  // namespace acacia::antichain_snapshot

#else

namespace acacia::antichain_snapshot {

  inline void configure (const spot::const_twa_graph_ptr&) {}

  template <typename SetOfStates>
  inline void observe (const SetOfStates&, int, int) {}

  inline void note_bound_raised () {}

  template <typename SetOfStates>
  inline void record_final (const SetOfStates&, int, int) {}

  template <typename Actions>
  inline void record_all_input_actions (const Actions&) {}

  template <typename SetOfStates, typename Actions>
  inline bool record_cpre_before (const SetOfStates&, int, int, const std::string&,
                                  const Actions&) {
    return false;
  }

  template <typename SetOfStates>
  inline void record_cpre_after (const SetOfStates&) {}

}  // namespace acacia::antichain_snapshot

#endif
