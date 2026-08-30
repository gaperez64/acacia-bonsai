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
# include <string_view>
# include <unistd.h>

namespace acacia::antichain_snapshot {

  /// Bumped whenever the on-disk layout changes in a way a reader must notice.
  /// 1: automaton.hoa + meta.tsv + antichain-<loop>.tsv.
  /// 2: meta.tsv gains schema_version; cpre-<loop>.tsv records one complete
  ///    input-conditioned update -- the region before, the ordered action list,
  ///    and the region after -- which is what an offline replay needs.
  inline constexpr int SCHEMA_VERSION = 2;

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
    std::ofstream meta {snapshot.directory + "/meta.tsv"};
    meta << "schema_version\tstates\tbool_threshold\n"
         << SCHEMA_VERSION << '\t' << aut->num_states () << '\t'
         << posets::vectors::bool_threshold << '\n';
  }

  template <typename SetOfStates>
  inline void observe (const SetOfStates& f, int k, int loop) {
    detail::snapshot_state& snapshot = detail::state ();
    if (snapshot.directory.empty () or snapshot.dumps >= snapshot.maximum or loop < 0 or
        static_cast<size_t> (loop) % snapshot.every != 0)
      return;

    ++snapshot.dumps;
    std::ofstream output {snapshot.directory + "/antichain-" + std::to_string (loop) + ".tsv"};
    output << "# k=" << k << " loop=" << loop << " maxima=" << f.size () << '\n';
    for (const auto& maximum : f) {
      for (size_t i = 0; i < maximum.size (); ++i) {
        if (i != 0)
          output << '\t';
        output << static_cast<int> (maximum[i]);
      }
      output << '\n';
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
    out << "# schema_version=" << SCHEMA_VERSION << " loop=" << loop << " k=" << k
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

  template <typename SetOfStates, typename Actions>
  inline bool record_cpre_before (const SetOfStates&, int, int, const std::string&,
                                  const Actions&) {
    return false;
  }

  template <typename SetOfStates>
  inline void record_cpre_after (const SetOfStates&) {}

}  // namespace acacia::antichain_snapshot

#endif
