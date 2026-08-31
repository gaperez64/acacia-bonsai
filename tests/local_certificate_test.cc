// The live local-certificate probe may terminate a worker early, so each
// positive answer needs an oracle that does not share the probe's search.
// These tiny games admit an exhaustive greatest-fixpoint computation over the
// complete bounded rank domain.

#include "actioners/standard.hh"
#include "research/rank_action_replay.hh"
#include "solver/local_certificate.hh"
#include "tiny_game_oracle.hh"
#include "utils/verbose.hh"

#include <cstddef>
#include <iostream>
#include <list>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <posets/downsets.hh>
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
  using namespace acacia::testing;

  int failures = 0;
  unsigned cache_equivalence_cases = 0;

  bool expect (const std::string& what, bool condition) {
    if (condition)
      return true;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
    return false;
  }

  void expect_cache_equivalent (
      const std::string& label,
      const acacia::solver_detail::local_certificate_result<SetOfStates>& cached,
      const acacia::solver_detail::local_certificate_result<SetOfStates>& uncached) {
    expect (label + ": status", cached.status == uncached.status);
    expect (label + ": nodes", cached.nodes == uncached.nodes);
    expect (label + ": forward applications",
            cached.forward_applications == uncached.forward_applications);
    expect (label + ": refuting input",
            cached.refuting_input == uncached.refuting_input);

    const bool same_presence = cached.win.has_value () == uncached.win.has_value ();
    expect (label + ": certificate presence", same_presence);
    if (not same_presence or not cached.win.has_value ())
      return;

    expect (label + ": certificate generator count",
            cached.win->size () == uncached.win->size ());
    bool cached_generators_are_contained = true;
    for (const auto& generator : *cached.win)
      cached_generators_are_contained =
          cached_generators_are_contained and uncached.win->contains (generator);
    expect (label + ": cached generators occur in the uncached certificate",
            cached_generators_are_contained);

    bool uncached_generators_are_contained = true;
    for (const auto& generator : *uncached.win)
      uncached_generators_are_contained =
          uncached_generators_are_contained and cached.win->contains (generator);
    expect (label + ": uncached generators occur in the cached certificate",
            uncached_generators_are_contained);
  }

  /// The actioner is intentionally built over a pointer-like duck-typed
  /// handle: production construction relies only on these two automaton calls.
  struct test_automaton {
      unsigned states;
      [[nodiscard]] unsigned num_states () const { return states; }
      [[nodiscard]] bool state_is_accepting (unsigned) const { return false; }
  };

  template <typename Check>
  void with_actioner (const tiny_game& game, size_t bool_threshold, Check&& check) {
    posets::vectors::bool_threshold = bool_threshold;
    test_automaton automaton {game.states};
    const test_automaton* aut = &automaton;
    std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>> empty;
    auto actioner = actioners::standard<state>::make (aut, empty, game.K);
    std::forward<Check> (check) (actioner);
  }

  state as_state (const rank_vector& value) { return state (rank_vector (value)); }

  SetOfStates principal_downset (const rank_vector& top) {
    return SetOfStates (as_state (top));
  }

  tiny_game two_action_refutable_game () {
    action_vec first (2), second (2);
    first[0].emplace_back (0, true);
    second[1].emplace_back (0, true);
    std::list<action_vec> actions;
    actions.push_back (std::move (first));
    actions.push_back (std::move (second));
    input_classes inputs;
    inputs.emplace_back (0, std::move (actions));
    return tiny_game {2, static_cast<VECTOR_ELT_T> (1), std::move (inputs)};
  }

  tiny_game backtracking_cache_game () {
    action_vec first_a (2), first_b (2);
    first_a[0].emplace_back (1, true);
    first_a[1].emplace_back (0, false);
    first_b[0].emplace_back (0, true);
    first_b[0].emplace_back (1, false);
    first_b[1].emplace_back (1, false);

    action_vec second_a (2), second_b (2);
    second_a[0].emplace_back (0, false);
    second_a[0].emplace_back (1, true);
    second_a[1].emplace_back (0, false);
    second_b[0].emplace_back (1, true);
    second_b[1].emplace_back (0, true);
    second_b[1].emplace_back (1, false);

    action_vec third (2);
    third[0].emplace_back (0, false);

    input_classes inputs;
    inputs.emplace_back (0, std::list<action_vec> {first_a, first_b});
    inputs.emplace_back (1, std::list<action_vec> {second_a, second_b});
    inputs.emplace_back (2, std::list<action_vec> {third});
    return tiny_game {2, static_cast<VECTOR_ELT_T> (2), std::move (inputs)};
  }

  void check_budget_exhaustion_is_inconclusive () {
    const tiny_game game = two_action_refutable_game ();
    constexpr size_t threshold = 2;
    const rank_vector initial = initial_vector (game.states, 0);
    const auto truth = brute_force_winning_region (game, threshold);
    expect ("budget fixture is genuinely refutable", not truth.contains (initial));

    with_actioner (game, threshold, [&] (auto& actioner) {
      auto envelope = principal_downset (safe_vector (game.states, game.K, threshold));
      const auto result = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 8, 100, 1);

      // The second action is deliberately untested: treating the first miss as
      // a complete input-class refutation would let a scan budget change truth.
      expect ("a partial input scan reports budget exhaustion",
              result.status == acacia::solver_detail::local_certificate_status::budget_exhausted);
      expect ("a partial input scan does not name a refuting input",
              result.refuting_input < 0);
    });
  }

  void check_candidate_envelope_separation () {
    action_vec increment (1);
    increment[0].emplace_back (0, true);
    input_classes inputs;
    inputs.emplace_back (0, std::list<action_vec> {increment});
    const tiny_game game {1, static_cast<VECTOR_ELT_T> (2), std::move (inputs)};
    constexpr size_t threshold = 1;
    const rank_vector initial = initial_vector (game.states, 0);

    with_actioner (game, threshold, [&] (auto& actioner) {
      auto envelope = principal_downset (safe_vector (game.states, game.K, threshold));
      auto candidate = principal_downset (initial);
      const auto image = actioner.apply (as_state (initial), increment,
                                         actioners::direction::forward);
      expect ("separation fixture creates an obligation inside the envelope",
              not candidate.contains (image) and envelope.contains (image));

      const auto result = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 2, 100, 100);
      // Leaving G asks the kernel search to add a generator.  Only leaving the
      // envelope can justify raising the worker's bound.
      expect ("an unresolved candidate obligation is not a root refutation",
              result.status != acacia::solver_detail::local_certificate_status::root_refuted
                  and result.refuting_input < 0);
    });
  }

  void check_partial_cache_equivalence () {
    action_vec increment (1);
    increment[0].emplace_back (0, true);
    input_classes inputs;
    inputs.emplace_back (0, std::list<action_vec> {increment});
    const tiny_game game {1, static_cast<VECTOR_ELT_T> (2), std::move (inputs)};
    constexpr size_t threshold = 1;
    const rank_vector initial = initial_vector (game.states, 0);

    with_actioner (game, threshold, [&] (auto& actioner) {
      auto envelope = principal_downset (safe_vector (game.states, game.K, threshold));
      const auto cached = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 2, 100, 100);
      const auto uncached = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 2, 100, 100, 0);
      const auto partially_cached = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 2, 100, 100, 1);

      expect ("the partial-cache fixture searches a pushed generator",
              partially_cached.nodes == 2);
      expect_cache_equivalent ("partial-cache fixture: default and uncached search",
                               cached, uncached);
      expect_cache_equivalent ("partial-cache fixture: default and one-entry cache",
                               cached, partially_cached);
      ++cache_equivalence_cases;
    });
  }

  void check_backtracking_cache_equivalence () {
    const tiny_game game = backtracking_cache_game ();
    constexpr size_t threshold = 2;
    const rank_vector initial = initial_vector (game.states, 0);

    with_actioner (game, threshold, [&] (auto& actioner) {
      auto envelope = principal_downset (safe_vector (game.states, game.K, threshold));
      const auto cached = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 8, 4096, 131072);
      const auto uncached = acacia::solver_detail::find_local_certificate (
          envelope, initial, game.inputs, actioner, game.K, 8, 4096, 131072, 0);

      // UNKNOWN after visiting child nodes means every pushed choice was
      // popped.  Reusing its generator id for the next sibling would make the
      // default cache return images computed for the popped generator.
      expect ("the cache-id fixture visits pushed generators", cached.nodes == 8);
      expect ("the cache-id fixture backtracks after its pushed generators",
              cached.status == acacia::solver_detail::local_certificate_status::unknown);
      expect_cache_equivalent ("backtracking cache fixture", cached, uncached);
      ++cache_equivalence_cases;
    });
  }

  void check_refutations_survive_envelope_shrinkage () {
    const tiny_game game = two_action_refutable_game ();
    constexpr size_t threshold = 2;
    const rank_vector initial = initial_vector (game.states, 0);
    const rank_vector safe = safe_vector (game.states, game.K, threshold);

    with_actioner (game, threshold, [&] (auto& actioner) {
      auto full = principal_downset (safe);
      auto smaller = principal_downset (initial);
      const rank_vector full_only = [] {
        rank_vector value (2, -1);
        value[1] = 0;
        return value;
      } ();
      expect ("the envelope fixture uses a strict contraction",
              full.contains (as_state (full_only))
                  and not smaller.contains (as_state (full_only)));

      const auto against_full = acacia::solver_detail::root_refutation (
          full, initial, game.inputs, actioner);
      const auto against_smaller = acacia::solver_detail::root_refutation (
          smaller, initial, game.inputs, actioner);
      expect ("the larger envelope fixture has a refutation",
              against_full.refuting_input >= 0);
      // Acacia contracts envelopes at a fixed bound, so a successor already
      // outside the old envelope cannot re-enter after contraction.
      expect ("a larger-envelope refutation survives a strict contraction",
              against_full.refuting_input < 0 or against_smaller.refuting_input >= 0);
    });
  }

  void check_random_games () {
    constexpr unsigned random_games = 256;
    static_assert (random_games >= 200);
    std::mt19937 gen {20260830};
    unsigned refutations = 0, win_certificates = 0, certificate_generators = 0;

    for (unsigned trial = 0; trial < random_games; ++trial) {
      const tiny_game game = random_game (gen);
      std::uniform_int_distribution<size_t> threshold_distribution {0, game.states};
      const size_t threshold = threshold_distribution (gen);
      const rank_vector initial = initial_vector (game.states, 0);
      const auto truth = brute_force_winning_region (game, threshold);
      const std::string label = "random game " + std::to_string (trial);

      with_actioner (game, threshold, [&] (auto& actioner) {
        auto full = principal_downset (safe_vector (game.states, game.K, threshold));
        const auto refutation = acacia::solver_detail::root_refutation (
            full, initial, game.inputs, actioner);
        if (refutation.refuting_input >= 0) {
          ++refutations;
          // A false answer here would make the solver raise the bound on a
          // worker whose initial state actually remains winning.
          expect (label + ": root refutation excludes the initial state from W_K",
                  not truth.contains (initial));
        }

        auto smaller = principal_downset (initial);
        const auto contracted = acacia::solver_detail::root_refutation (
            smaller, initial, game.inputs, actioner);
        if (refutation.refuting_input >= 0)
          expect (label + ": refutation persists in a contracted envelope",
                  contracted.refuting_input >= 0);

        const auto certificate = acacia::solver_detail::find_local_certificate (
            full, initial, game.inputs, actioner, game.K, 8, 4096, 131072);
        const auto uncached_certificate = acacia::solver_detail::find_local_certificate (
            full, initial, game.inputs, actioner, game.K, 8, 4096, 131072, 0);
        expect_cache_equivalent (label + ": cached and uncached search",
                                 certificate, uncached_certificate);
        ++cache_equivalence_cases;
        if (certificate.status
            == acacia::solver_detail::local_certificate_status::win_certificate) {
          ++win_certificates;
          expect (label + ": win certificate puts the initial state in W_K",
                  truth.contains (initial));
          const bool has_region = certificate.win.has_value ();
          expect (label + ": win status carries a region", has_region);
          if (has_region) {
            bool all_generators_win = true;
            for (const auto& generator : *certificate.win) {
              ++certificate_generators;
              all_generators_win = all_generators_win and truth.contains (generator);
            }
            // Inductiveness justifies the whole downset only if its maximal
            // generators belong to the true bounded winning region.
            expect (label + ": every certificate generator belongs to W_K",
                    all_generators_win);
          }
        }
      });
    }

    // These keep the conditional soundness assertions from passing only
    // because this particular deterministic campaign exercised no answer.
    expect ("the random campaign exercises root refutations", refutations > 0);
    expect ("the random campaign exercises win certificates", win_certificates > 0);
    expect ("the random campaign checks returned generators", certificate_generators > 0);
  }

}  // namespace

int main () {
  check_random_games ();
  check_budget_exhaustion_is_inconclusive ();
  check_candidate_envelope_separation ();
  check_partial_cache_equivalence ();
  check_backtracking_cache_equivalence ();
  check_refutations_survive_envelope_shrinkage ();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "local-certificate: cache equivalence assertions passed for "
            << cache_equivalence_cases << " cases\n";
  std::cout << "local-certificate: all checks passed\n";
  return 0;
}
