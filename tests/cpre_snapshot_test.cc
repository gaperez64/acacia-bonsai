// Exercise the shipping backward solver and its opt-in snapshot producer.
// The Python test independently checks each serialized update.
#include "actioners/standard.hh"
#include "input_pickers/critical.hh"
#include "ios_precomputers/mona.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "utils/verbose.hh"

#include <cstdlib>
#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include <posets/vectors/traits.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

int main (int argc, char** argv) {
  if (argc != 2)
    return 2;
  setenv ("ACACIA_DIAG", "1", 1);
  setenv ("ACACIA_ANTICHAIN_SNAPSHOT_DIR", argv[1], 1);
  setenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE", "1", 1);
  setenv ("ACACIA_ANTICHAIN_SNAPSHOT_CPRE_MAX", "4", 1);
  setenv ("ACACIA_DIAG_PROGRESS_EVERY", "0", 1);

  auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
  aut->set_buchi ();
  aut->prop_state_acc (true);
  aut->new_states (3);
  aut->set_init_state (0);
  const bdd request = bdd_ithvar (aut->register_ap ("request"));
  const bdd grant = bdd_ithvar (aut->register_ap ("grant"));
  aut->new_edge (0, 0, !request);
  aut->new_edge (0, 1, request & !grant);
  aut->new_edge (0, 2, request & grant);
  aut->new_edge (1, 1, bddtrue, {0});
  aut->new_edge (2, 2, bddtrue);

  using state = posets::vectors::simd_vector_backed<VECTOR_ELT_T>;
  using downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using solver = k_bounded_safety_aut_detail<downset, ios_precomputers::mona,
                                            actioners::standard<state>, input_pickers::critical>;
  // Both an all-counting layout and a nonaccepting Boolean tail must survive
  // serialization; a reset to K-1 for the Boolean tail would be incorrect.
  for (const size_t threshold : {3u, 2u}) {
    posets::vectors::bool_threshold = threshold;
    solver game (aut, 2, 2, 1, request, grant, ios_precomputers::mona (),
                 actioners::standard<state> (), input_pickers::critical ());
    if (not game.solve ())
      return 1;
  }
}
