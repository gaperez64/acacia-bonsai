#pragma once

#include "configuration.hh"

#include <spot/twa/fwd.hh>

#if ACACIA_ENABLE_DIAGNOSTICS

# include <cstdlib>
# include <filesystem>
# include <fstream>
# include <posets/vectors.hh>
# include <spot/twa/twagraph.hh>
# include <spot/twaalgos/hoa.hh>
# include <string>
# include <unistd.h>

namespace acacia::antichain_snapshot {

  namespace detail {

    struct snapshot_state {
        std::string root;
        std::string directory;
        size_t every = 1;
        size_t maximum = 20;
        size_t dumps = 0;
        size_t automata = 0;
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

    std::ofstream meta {snapshot.directory + "/meta.tsv"};
    meta << "states\tbool_threshold\n"
         << aut->num_states () << '\t' << posets::vectors::bool_threshold << '\n';
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

}  // namespace acacia::antichain_snapshot

#else

namespace acacia::antichain_snapshot {

  inline void configure (const spot::const_twa_graph_ptr&) {}

  template <typename SetOfStates>
  inline void observe (const SetOfStates&, int, int) {}

}  // namespace acacia::antichain_snapshot

#endif
