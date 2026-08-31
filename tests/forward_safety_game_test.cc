// The explicit forward game is a reachable-state implementation of the same
// bounded safety game as the exhaustive descending fixpoint.  These tests keep
// both references small enough to compare exactly.

#include "research/explicit_forward_game.hh"
#include "tiny_game_oracle.hh"

#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <list>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {

  using namespace acacia::research;
  using namespace acacia::testing;

  int failures = 0;
  unsigned certificates_checked = 0;

  bool expect (const std::string& what, bool condition) {
    if (condition)
      return true;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
    return false;
  }

  rank_vector vec (std::initializer_list<int> values) {
    rank_vector result (values.size (), 0);
    size_t i = 0;
    for (const int value : values)
      result[i++] = static_cast<VECTOR_ELT_T> (value);
    return result;
  }

  action_vec make_action (
      unsigned states,
      std::initializer_list<std::tuple<unsigned, unsigned, bool>> transitions) {
    action_vec result (states);
    for (const auto& [destination, source, increment] : transitions)
      result[destination].emplace_back (source, increment);
    return result;
  }

  tiny_game make_game (
      unsigned states, VECTOR_ELT_T K,
      std::initializer_list<std::vector<action_vec>> per_input_actions) {
    input_classes inputs;
    unsigned input = 0;
    for (const auto& actions : per_input_actions)
      inputs.emplace_back (input++, std::list<action_vec> (actions.begin (), actions.end ()));
    return {states, K, std::move (inputs)};
  }

  rank_vector as_rank (const state& value) {
    rank_vector result (value.size (), 0);
    for (size_t i = 0; i < value.size (); ++i)
      result[i] = value[i];
    return result;
  }

  void check_certificate (const std::string& label, const tiny_game& game,
                          size_t bool_threshold, const rank_vector& initial,
                          const forward_result& result) {
    if (result.status != forward_status::win_k)
      return;
    ++certificates_checked;

    posets::vectors::bool_threshold = bool_threshold;
    std::vector<state> raw_generators;
    raw_generators.reserve (result.strategy_ranks.size ());
    for (const auto& rank : result.strategy_ranks)
      raw_generators.emplace_back (rank_vector (rank));
    SetOfStates certificate {std::move (raw_generators)};

    bool all_safe = true;
    bool all_inputs_supported = true;
    for (const auto& generator : certificate) {
      const rank_vector rank = as_rank (generator);
      all_safe = all_safe and is_safe (rank, game.K, bool_threshold);
      for (const auto& input_and_actions : game.inputs) {
        bool supported = false;
        for (const auto& action : input_and_actions.second) {
          const rank_vector successor = apply_forward (rank, action, game.K);
          if (certificate.contains (state (rank_vector (successor)))) {
            supported = true;
            break;
          }
        }
        all_inputs_supported = all_inputs_supported and supported;
      }
    }

    expect (label + ": certificate has only safe generators", all_safe);
    expect (label + ": certificate contains the initial vector",
            certificate.contains (state (rank_vector (initial))));
    expect (label + ": every generator/input has a successor in the certificate",
            all_inputs_supported);
  }

  forward_result check_game (const std::string& label, const tiny_game& game,
                             size_t bool_threshold, const rank_vector& initial,
                             forward_status expected) {
    const forward_result result = solve_explicit_forward_game (
        initial, game.inputs, game.K, bool_threshold);
    expect (label + ": explicit status", result.status == expected);

    const exact_region truth = brute_force_winning_region (game, bool_threshold);
    const bool oracle_wins = truth.contains (initial);
    expect (label + ": hand-written oracle verdict",
            oracle_wins == (expected == forward_status::win_k));
    check_certificate (label, game, bool_threshold, initial, result);
    return result;
  }

  void check_hand_written_games () {
    const action_vec self = make_action (1, {{0, 0, false}});
    const action_vec increment = make_action (1, {{0, 0, true}});
    const action_vec disappear = make_action (1, {});

    const tiny_game unsafe_initial =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{self}});
    const auto unsafe_result = check_game (
        "1 unsafe initial state", unsafe_initial, 1, vec ({1}), forward_status::lose_k);
    expect ("1 unsafe initial state: unsafe nodes are not expanded",
            unsafe_result.env_nodes == 1 and unsafe_result.ctrl_nodes == 0
                and unsafe_result.edges == 0);

    const tiny_game one_safe_action =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{disappear}});
    const auto one_safe_result = check_game (
        "2 one input one safe action", one_safe_action, 1, vec ({0}),
        forward_status::win_k);
    expect ("2 one input one safe action: both reachable ranks are explicit",
            one_safe_result.env_nodes == 2 and one_safe_result.ctrl_nodes == 2
                and one_safe_result.edges == 4);

    const tiny_game all_unsafe =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{increment, increment}});
    check_game ("3 one input all unsafe actions", all_unsafe, 1, vec ({0}),
                forward_status::lose_k);

    const tiny_game losing_input =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{self}, {increment}});
    check_game ("4 two inputs one losing", losing_input, 1, vec ({0}),
                forward_status::lose_k);

    const tiny_game mixed_actions =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{increment, self}});
    check_game ("5 one losing and one winning controller action", mixed_actions, 1,
                vec ({0}), forward_status::win_k);

    const tiny_game safe_self_loop =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{self}});
    const auto self_loop_result = check_game (
        "6 safe self-loop", safe_self_loop, 1, vec ({0}), forward_status::win_k);
    expect ("6 safe self-loop: graph shape",
            self_loop_result.env_nodes == 1 and self_loop_result.ctrl_nodes == 1
                and self_loop_result.edges == 2);

    const action_vec swap = make_action (2, {{0, 1, false}, {1, 0, false}});
    const tiny_game safe_cycle =
        make_game (2, static_cast<VECTOR_ELT_T> (1), {{swap}});
    const auto cycle_result = check_game (
        "7 two-node safe cycle", safe_cycle, 2, vec ({0, -1}), forward_status::win_k);
    expect ("7 two-node safe cycle: graph shape",
            cycle_result.env_nodes == 2 and cycle_result.ctrl_nodes == 2
                and cycle_result.edges == 4);

    const action_vec cycle_escape = make_action (2, {{0, 0, true}});
    const tiny_game escaping_cycle =
        make_game (2, static_cast<VECTOR_ELT_T> (1), {{swap}, {cycle_escape}});
    check_game ("8 cycle plus environment escape to unsafe", escaping_cycle, 2,
                vec ({0, -1}), forward_status::lose_k);

    const tiny_game duplicate_successors =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{self, self, self}});
    const auto duplicate_result = check_game (
        "9 duplicate action successors", duplicate_successors, 1, vec ({0}),
        forward_status::win_k);
    expect ("9 duplicate action successors: one exact successor edge",
            duplicate_result.env_nodes == 1 and duplicate_result.ctrl_nodes == 1
                and duplicate_result.edges == 2);

    const action_vec unsafe_boolean =
        make_action (3, {{1, 0, true}, {1, 1, true}});
    const action_vec safe_boolean =
        make_action (3, {{1, 0, false}, {1, 1, false}});
    const tiny_game boolean_tail = make_game (
        3, static_cast<VECTOR_ELT_T> (2), {{unsafe_boolean, safe_boolean}});
    check_game ("10 Boolean-tail coordinates", boolean_tail, 1, vec ({0, -1, -1}),
                forward_status::win_k);

    const tiny_game reaches_k =
        make_game (1, static_cast<VECTOR_ELT_T> (2), {{increment}});
    check_game ("11 numeric increment reaching K", reaches_k, 1, vec ({0}),
                forward_status::lose_k);
  }

  void check_random_agreement () {
    constexpr unsigned random_games = 5000;
    static_assert (random_games >= 5000);
    std::mt19937 gen {20260831};
    unsigned compared = 0;

    for (unsigned trial = 0; trial < random_games; ++trial) {
      const tiny_game game = random_game (gen);
      std::uniform_int_distribution<size_t> threshold_distribution {0, game.states};
      const size_t bool_threshold = threshold_distribution (gen);
      const rank_vector initial = initial_vector (game.states, 0);
      const exact_region truth = brute_force_winning_region (game, bool_threshold);
      const forward_result result = solve_explicit_forward_game (
          initial, game.inputs, game.K, bool_threshold);
      const std::string label = "random game " + std::to_string (trial);

      expect (label + ": default limits are not reached",
              result.status != forward_status::resource_limit);
      expect (label + ": F0 agrees with the complete-domain greatest fixpoint",
              (result.status == forward_status::win_k) == truth.contains (initial));
      check_certificate (label, game, bool_threshold, initial, result);
      ++compared;
    }

    expect ("random campaign compared all requested games", compared == random_games);
    std::cout << "forward-safety-game: compared " << compared << " random games\n";
  }

  void check_resource_limit_is_inconclusive () {
    const action_vec self = make_action (1, {{0, 0, false}});
    const tiny_game game =
        make_game (1, static_cast<VECTOR_ELT_T> (1), {{self}});
    forward_limits limits;
    limits.max_ctrl_nodes = 0;
    const forward_result result = solve_explicit_forward_game (
        vec ({0}), game.inputs, game.K, 1, limits);

    expect ("resource limit: reports resource_limit",
            result.status == forward_status::resource_limit);
    expect ("resource limit: is not a winning verdict",
            result.status != forward_status::win_k);
    expect ("resource limit: is not a losing verdict",
            result.status != forward_status::lose_k);
    expect ("resource limit: retains counts gathered before returning",
            result.env_nodes == 1 and result.ctrl_nodes == 1 and result.edges == 0);
    expect ("resource limit: carries no strategy ranks", result.strategy_ranks.empty ());
  }

}  // namespace

int main () {
  check_hand_written_games ();
  check_random_agreement ();
  check_resource_limit_is_inconclusive ();

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "forward-safety-game: verified " << certificates_checked
            << " winning certificates\n";
  std::cout << "forward-safety-game: all checks passed\n";
  return 0;
}
