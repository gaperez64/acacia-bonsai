#include "research/cpre_event.hh"
#include "solver/antichain_snapshot.hh"
#include "utils/verbose.hh"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <spot/twa/bdddict.hh>
#include <spot/twa/twagraph.hh>
#include <unistd.h>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 1;
}

int main (int argc, char** argv) {
  if (argc != 2)
    return 2;
  const auto root = std::filesystem::path (argv[1]) / std::to_string (::getpid ());
  std::filesystem::create_directories (root);
  int result = 0;
  try {
    ::setenv ("ACACIA_ANTICHAIN_SNAPSHOT_DIR", root.c_str (), 1);
    ::setenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE", "1", 1);
    ::setenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE_START", "2", 1);
    ::setenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE_EVERY", "3", 1);
    ::setenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX", "2", 1);
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->new_states (2);
    aut->set_init_state (0);
    acacia::antichain_snapshot::configure (aut);
    std::vector<acacia::research::rank_vector> region {acacia::research::rank_vector (2, -1)};
    std::vector<acacia::research::action_vec> actions {acacia::research::action_vec (2)};
    unsigned recorded = 0;
    for (int loop = 0; loop < 11; ++loop) {
      const bool capture =
          acacia::antichain_snapshot::record_cpre_before (region, 2, loop, "1", actions);
      if (capture != (loop == 2 or loop == 5))
        throw std::runtime_error ("capture sampling/cap mismatch");
      if (capture) {
        ++recorded;
        acacia::antichain_snapshot::record_cpre_after (region);
      }
    }
    if (recorded != 2)
      throw std::runtime_error ("empty capture test");
    for (const auto& directory : std::filesystem::directory_iterator (root))
      for (const auto& path : acacia::research::find_events (directory.path ())) {
        const auto event = acacia::research::load (path, 2);
        if (not event.complete or event.schema_version != 2 or event.before.size () != 1 or
            event.after.size () != 1 or event.actions.size () != 1)
          throw std::runtime_error ("sampled event is not a complete schema-2 event");
      }
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    result = 1;
  }
  std::filesystem::remove_all (root);
  return result;
}
