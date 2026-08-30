// The live local-certificate probe may terminate a worker early, so each
// positive answer needs an oracle that does not share the probe's search.
// These tiny games admit an exhaustive greatest-fixpoint computation over the
// complete bounded rank domain.

#include "actioners/standard.hh"
#include "research/rank_action_replay.hh"
#include "solver/local_certificate.hh"
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
  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using input_classes = std::list<std::pair<unsigned, std::list<action_vec>>>;

  struct tiny_game {
      unsigned states;
      VECTOR_ELT_T K;
      input_classes inputs;
  };

  int failures = 0;

  bool expect (const std::string& what, bool condition) {
    if (condition)
      return true;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
    return false;
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

  std::vector<rank_vector> entire_rank_domain (unsigned states, VECTOR_ELT_T K) {
    const size_t levels = static_cast<size_t> (K) + 2;
    size_t total = 1;
    for (unsigned i = 0; i < states; ++i)
      total *= levels;

    std::vector<rank_vector> domain;
    domain.reserve (total);
    for (size_t code = 0; code < total; ++code) {
      rank_vector value (states, 0);
      size_t rest = code;
      for (unsigned i = 0; i < states; ++i) {
        value[i] = static_cast<VECTOR_ELT_T> (static_cast<int> (rest % levels) - 1);
        rest /= levels;
      }
      domain.push_back (std::move (value));
    }
    return domain;
  }

  struct exact_region {
      VECTOR_ELT_T K;
      std::vector<bool> members;

      template <typename Vector>
      [[nodiscard]] bool contains (const Vector& value) const {
        const size_t levels = static_cast<size_t> (K) + 2;
        size_t index = 0, place = 1;
        for (size_t i = 0; i < value.size (); ++i) {
          index += static_cast<size_t> (static_cast<int> (value[i]) + 1) * place;
          place *= levels;
        }
        return members[index];
      }
  };

  bool is_safe (const rank_vector& value, VECTOR_ELT_T K, size_t bool_threshold) {
    for (size_t i = 0; i < value.size (); ++i) {
      const int cap = i < bool_threshold ? static_cast<int> (K) - 1 : 0;
      if (static_cast<int> (value[i]) > cap)
        return false;
    }
    return true;
  }

  /// This is the descending greatest fixpoint for the bounded safety game.
  /// Forward images use `research/rank_action_replay.hh`, whose implementation
  /// is the documented transcription of `actioners::standard::apply`.
  exact_region brute_force_winning_region (const tiny_game& game, size_t bool_threshold) {
    const auto domain = entire_rank_domain (game.states, game.K);
    exact_region region {game.K, std::vector<bool> (domain.size (), false)};
    for (size_t i = 0; i < domain.size (); ++i)
      region.members[i] = is_safe (domain[i], game.K, bool_threshold);

    for (;;) {
      std::vector<size_t> removed;
      for (size_t r = 0; r < domain.size (); ++r) {
        if (not region.members[r])
          continue;
        bool loses = false;
        for (const auto& input_and_actions : game.inputs) {
          bool has_winning_action = false;
          for (const auto& action : input_and_actions.second) {
            const auto image = apply_forward (domain[r], action, game.K);
            if (region.contains (image)) {
              has_winning_action = true;
              break;
            }
          }
          if (not has_winning_action) {
            loses = true;
            break;
          }
        }
        if (loses)
          removed.push_back (r);
      }
      if (removed.empty ())
        return region;
      for (const size_t r : removed)
        region.members[r] = false;
    }
  }

  action_vec random_action (std::mt19937& gen, unsigned states) {
    std::uniform_int_distribution<int> include_edge {0, 2};
    std::uniform_int_distribution<int> increment {0, 1};
    action_vec action (states);
    for (unsigned destination = 0; destination < states; ++destination)
      for (unsigned source = 0; source < states; ++source)
        if (include_edge (gen) != 0)
          action[destination].emplace_back (source, increment (gen) != 0);
    return action;
  }

  tiny_game random_game (std::mt19937& gen) {
    std::uniform_int_distribution<unsigned> state_count {1, 4};
    std::uniform_int_distribution<int> bound {1, 2};
    std::uniform_int_distribution<unsigned> class_count {1, 3};
    std::uniform_int_distribution<unsigned> action_count {1, 3};

    tiny_game game {state_count (gen), static_cast<VECTOR_ELT_T> (bound (gen)), {}};
    const unsigned inputs = class_count (gen);
    for (unsigned input = 0; input < inputs; ++input) {
      std::list<action_vec> actions;
      const unsigned count = action_count (gen);
      for (unsigned i = 0; i < count; ++i)
        actions.push_back (random_action (gen, game.states));
      game.inputs.emplace_back (input, std::move (actions));
    }
    return game;
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
  check_refutations_survive_envelope_shrinkage ();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "local-certificate: all checks passed\n";
  return 0;
}
