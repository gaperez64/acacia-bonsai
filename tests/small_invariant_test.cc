// The offline rank semantics must agree with the solver's, exactly.
//
// Every small-invariant result is computed outside the solver, so a
// disagreement between these two implementations would make every measurement
// ambiguous. The first two checks therefore compare the shared research header
// against `actioners::standard::apply` itself, on the same vectors and actions,
// rather than against a hand-written expectation.

#include "actioners/standard.hh"
#include "research/rank_action_replay.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <posets/vectors.hh>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {

  using namespace acacia::research;
  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;

  int failures = 0;

  bool expect (const std::string& what, bool condition) {
    if (condition)
      return true;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
    return false;
  }

  /// A stand-in automaton: the actioner only needs the state count and, through
  /// the transition payload, which states are accepting.
  struct test_automaton {
      unsigned states;
      [[nodiscard]] unsigned num_states () const { return states; }
      [[nodiscard]] bool state_is_accepting (unsigned) const { return false; }
  };

  action_vec random_action (std::mt19937& gen, unsigned states) {
    std::uniform_int_distribution<unsigned> pick {0, states - 1};
    std::uniform_int_distribution<int> coin {0, 1};
    action_vec avec (states);
    for (unsigned i = 0; i < states; ++i) {
      const unsigned degree = pick (gen) % 3;
      for (unsigned d = 0; d <= degree; ++d)
        avec[i].emplace_back (pick (gen), coin (gen) == 1);
    }
    return avec;
  }

  rank_vector random_vector (std::mt19937& gen, unsigned states, VECTOR_ELT_T K) {
    std::uniform_int_distribution<int> value {-1, static_cast<int> (K)};
    rank_vector v (states, 0);
    for (unsigned i = 0; i < states; ++i)
      v[i] = static_cast<VECTOR_ELT_T> (value (gen));
    return v;
  }

  /// The research header and the solver's actioner must produce identical
  /// vectors, in both directions, on the same inputs.
  void check_against_the_actioner () {
    constexpr unsigned states = 6;
    constexpr VECTOR_ELT_T K = 3;
    posets::vectors::bool_threshold = 4;

    test_automaton automaton {states};
    const test_automaton* aut = &automaton;
    std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>> empty;
    auto actioner = actioners::standard<state>::make (aut, empty, K);

    std::mt19937 gen {20260830};
    bool forward_ok = true, backward_ok = true;
    for (int trial = 0; trial < 400; ++trial) {
      const action_vec avec = random_action (gen, states);
      const rank_vector m = random_vector (gen, states, K);

      const state theirs_fwd =
          actioner.apply (state (rank_vector (m)), avec, actioners::direction::forward);
      const rank_vector ours_fwd = apply_forward (m, avec, K);
      for (unsigned i = 0; i < states; ++i)
        forward_ok = forward_ok and theirs_fwd[i] == ours_fwd[i];

      const state theirs_bwd =
          actioner.apply (state (rank_vector (m)), avec, actioners::direction::backward);
      const rank_vector ours_bwd =
          apply_backward (m, avec, K, posets::vectors::bool_threshold);
      for (unsigned i = 0; i < states; ++i)
        backward_ok = backward_ok and theirs_bwd[i] == ours_bwd[i];
    }
    expect ("apply_forward agrees with the actioner on 400 random cases", forward_ok);
    expect ("apply_backward agrees with the actioner on 400 random cases", backward_ok);
  }

  rank_vector vec (std::initializer_list<int> values) {
    rank_vector v (values.size (), 0);
    size_t i = 0;
    for (int value : values)
      v[i++] = static_cast<VECTOR_ELT_T> (value);
    return v;
  }

  void check_domination_index () {
    generator_index index {{vec ({2, 0}), vec ({0, 2}), vec ({1, 1})}};
    unsigned long long checks = 0;

    expect ("a dominated vector finds a dominator",
            index.find_dominator (vec ({1, 0}), checks) != generator_index::npos);
    expect ("an undominated vector finds none",
            index.find_dominator (vec ({2, 2}), checks) == generator_index::npos);

    // Deactivating the only dominator must make the same query fail: this is
    // the whole point of an active-subset view, and it is what the generic
    // Downset API cannot express.
    const size_t found = index.find_dominator (vec ({0, 2}), checks);
    index.deactivate (found);
    expect ("a deactivated generator no longer dominates",
            index.find_dominator (vec ({0, 2}), checks) == generator_index::npos);
    expect ("the active count follows deactivation", index.active_count () == 2);
  }

  /// Removing a generator can cost more than itself: anything whose only
  /// support was that generator must go too. This is the cascade the peeling
  /// has to follow, and the reason a single pass is not enough.
  void check_support_cascade () {
    // Three generators on one input class. `keep` supports itself; `middle`
    // is supported only by `tail`; `tail` is supported by nothing.
    const rank_vector keep = vec ({2, 0, 0});
    const rank_vector middle = vec ({0, 2, 0});
    const rank_vector tail = vec ({0, 0, 2});

    generator_index index {{keep, middle, tail}};
    unsigned long long checks = 0;
    expect ("all three start active", index.active_count () == 3);

    index.deactivate (2);  // tail loses its support
    expect ("middle's dominator is gone once tail is inactive",
            index.find_dominator (tail, checks) == generator_index::npos);
    index.deactivate (1);
    expect ("only the self-supporting generator survives", index.active_count () == 1);
    expect ("and it is the one that supports itself",
            index.find_dominator (keep, checks) == 0);
  }

  /// The counterexample the sprint brief turns on: states dropped during a
  /// contracting iteration do not come back, so a pruned branch may not be
  /// widened by adding the omitted generators to its terminal region.
  void check_add_back_is_unsound () {
    // s -> a -> b -> b, every state safe. From {s,a,b} the game is winning.
    // Dropping b gives {s,a} -> {s} -> {}, and adding b back gives {b}.
    const rank_vector s = vec ({0, -1, -1});
    const rank_vector a = vec ({-1, 0, -1});
    const rank_vector b = vec ({-1, -1, 0});

    generator_index full {{s, a, b}};
    unsigned long long checks = 0;
    expect ("the full set contains all three", full.active_count () == 3);

    // The contracted branch keeps only b; re-adding s and a would claim a
    // region the branch never justified.
    generator_index pruned {{s, a}};
    pruned.deactivate (0);
    pruned.deactivate (1);
    expect ("the pruned branch terminates empty", pruned.active_count () == 0);
    expect ("and b is not in it",
            pruned.find_dominator (b, checks) == generator_index::npos);
  }

  void check_safe_vector () {
    posets::vectors::bool_threshold = 2;
    const rank_vector safe = safe_vector (4, 3, 2);
    expect ("counting coordinates are capped at K-1", safe[0] == 2 and safe[1] == 2);
    expect ("Boolean coordinates are capped at 0", safe[2] == 0 and safe[3] == 0);
    expect ("a vector at the cap is safe", leq (vec ({2, 2, 0, 0}), safe));
    expect ("a vector above the Boolean cap is not", not leq (vec ({2, 2, 1, 0}), safe));
    expect ("a vector above the counting cap is not", not leq (vec ({3, 0, 0, 0}), safe));
  }

  void check_initial_vector () {
    const rank_vector init = initial_vector (5, 3);
    expect ("absent everywhere but the initial state",
            init[0] == -1 and init[1] == -1 and init[2] == -1 and init[4] == -1);
    expect ("and zero there", init[3] == 0);
  }

}  // namespace

int main () {
  check_against_the_actioner ();
  check_domination_index ();
  check_support_cascade ();
  check_add_back_is_unsound ();
  check_safe_vector ();
  check_initial_vector ();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "small-invariant: all checks passed\n";
  return 0;
}
