// Picker exhaustion must never publish a region that excludes the initial state.
#include "actioners/standard.hh"
#include "input_pickers/critical.hh"
#include "ios_precomputers/mona.hh"
#include "solver/k_bounded_safety_aut.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include <posets/vectors/traits.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 1;
}

int main () {
  using state = posets::vectors::simd_vector_backed<VECTOR_ELT_T>;
  using downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using solver = k_bounded_safety_aut_detail<downset, ios_precomputers::mona,
                                           actioners::standard<state>, input_pickers::critical>;
  const ios_precomputers::mona precomputer;
  const actioners::standard<state> actioner;
  const input_pickers::critical picker;

  for (const bool accepting : {false, true}) {
    auto aut = spot::make_twa_graph (spot::make_bdd_dict ());
    aut->set_buchi ();
    aut->prop_state_acc (true);
    aut->new_states (1);
    aut->set_init_state (0);
    aut->new_edge (0, 0, bddtrue, accepting ? spot::acc_cond::mark_t {0}
                                         : spot::acc_cond::mark_t {});

    // Zero is deliberately injected below the public argument validation. Its
    // initial safe region [-1] excludes [0], yet the picker is already exhausted.
    // This checks the solver's defensive guard independently of the bindings.
    solver zero (aut, 0, 0, 1, bddtrue, bddtrue, precomputer, actioner, picker);
    if (zero.solve ().has_value ()) {
      std::cerr << "picker exhaustion published a region excluding init\n";
      return 1;
    }

    for (const int bound : {1, 3, 127}) {
      solver valid (aut, bound, bound, 1, bddtrue, bddtrue, precomputer, actioner, picker);
      const auto result = valid.solve ();
      if (result.has_value () == accepting) {
        std::cerr << "valid one-state game changed verdict\n";
        return 1;
      }
      if (result) {
        const posets::utils::vector_mm<VECTOR_ELT_T> initial (1, 0);
        if (not result->second.contains (state (initial))) {
          std::cerr << "successful valid region excludes init\n";
          return 1;
        }
      }
    }
  }

  // Exactly one accepting visit requires K=2. The maximum valid increment
  // overshoots 127 mathematically; the schedule must clamp before narrowing.
  auto transient = spot::make_twa_graph (spot::make_bdd_dict ());
  transient->set_buchi ();
  transient->prop_state_acc (true);
  transient->new_states (3);
  transient->set_init_state (0);
  transient->new_edge (0, 1, bddtrue);
  transient->new_edge (1, 2, bddtrue, {0});
  transient->new_edge (2, 2, bddtrue);
  posets::vectors::bool_threshold = 3;
  for (const int increment : {1, 127}) {
    solver raised (transient, 1, 127, increment, bddtrue, bddtrue,
                   precomputer, actioner, picker);
    const auto result = raised.solve ();
    posets::utils::vector_mm<VECTOR_ELT_T> initial (3, -1);
    initial[0] = 0;
    if (not result || result->first != (increment == 1 ? 2 : 127)
        || not result->second.contains (state (initial))) {
      std::cerr << "bound raising violated the initial-state invariant\n";
      return 1;
    }
  }
}
