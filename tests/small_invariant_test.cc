// The offline rank semantics must agree with the solver's, exactly.
//
// Every small-invariant result is computed outside the solver, so a
// disagreement between these two implementations would make every measurement
// ambiguous. The first two checks therefore compare the shared research header
// against `actioners::standard::apply` itself, on the same vectors and actions,
// rather than against a hand-written expectation.

#include "actioners/standard.hh"
#include "tiny_game_oracle.hh"
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
  using state = posets::vectors::simd_vector_backed<VECTOR_ELT_T>;

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

  void check_forward_saturation () {
    constexpr VECTOR_ELT_T K = 3;
    action_vec avec (1);
    avec[0].emplace_back (0, true);

    // This pins the saturation boundary used by both searches: arithmetic
    // cannot pass K, while K itself lies outside the safe candidate space.
    const rank_vector image = apply_forward (vec ({3}), avec, K);
    const rank_vector safe = safe_vector (1, K, 1);
    expect ("a forward image that would exceed K saturates at K", image[0] == K);
    expect ("a coordinate at K is outside the safe set", not leq (image, safe));
  }

  void check_safe_vector () {
    constexpr VECTOR_ELT_T K = 3;
    posets::vectors::bool_threshold = 2;
    const rank_vector safe = safe_vector (4, K, 2);

    // This pins the mixed counting/Boolean domain: Boolean presence may be
    // introduced at zero, but any positive Boolean rank must remain unsafe.
    expect ("counting coordinates are capped at K-1", safe[0] == 2 and safe[1] == 2);
    expect ("Boolean coordinates are capped at 0", safe[2] == 0 and safe[3] == 0);
    expect ("a vector at the cap is safe", leq (vec ({2, 2, 0, 0}), safe));
    expect ("a vector above the Boolean cap is not", not leq (vec ({2, 2, 1, 0}), safe));
    expect ("a vector above the counting cap is not", not leq (vec ({3, 0, 0, 0}), safe));

    action_vec introduce_boolean (4);
    introduce_boolean[2].emplace_back (0, false);
    const rank_vector image =
        apply_forward (initial_vector (4, 0), introduce_boolean, K);
    expect ("a present predecessor raises a Boolean coordinate from absent to zero",
            image[2] == 0);
  }

  void check_absence_does_not_propagate () {
    constexpr VECTOR_ELT_T K = 3;
    action_vec absent_only (2);
    absent_only[1].emplace_back (1, false);

    // This pins forward presence semantics: an absent source cannot make its
    // destination present merely because the transition exists.
    const rank_vector image =
        apply_forward (initial_vector (2, 0), absent_only, K);
    expect ("a destination with only an absent predecessor stays absent", image[1] == -1);
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
  check_forward_saturation ();
  check_safe_vector ();
  check_absence_does_not_propagate ();
  check_initial_vector ();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "small-invariant: all checks passed\n";
  return 0;
}
