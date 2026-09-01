// The explicit forward game is a reachable-state implementation of the same
// bounded safety game as the exhaustive descending fixpoint.  These tests keep
// both references small enough to compare exactly.

#include "actioners/standard.hh"
#include "research/explicit_forward_game.hh"
#include "solver/certificate_verifier.hh"
#include "solver/forward_reachable_safety.hh"
#include "solver/minimal_losing_antichain.hh"
#include "tiny_game_oracle.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <list>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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
  unsigned certificates_checked = 0;

  using f1_result = acacia::solver_detail::forward_solve_result<state>;
  using choice_reduction =
      acacia::solver_detail::forward_reachable_detail::
          controller_choice_reduction<state>;

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

  /// F0 fixtures use vector_mm while F1 deliberately operates on the solver's
  /// configured state representation, so tests cross that boundary explicitly.
  state as_solver_state (const rank_vector& value) {
    return state (rank_vector (value));
  }

  state as_solver_state (const state& value) { return value.copy (); }

  struct test_automaton {
      unsigned states;
      [[nodiscard]] unsigned num_states () const { return states; }
      [[nodiscard]] bool state_is_accepting (unsigned) const { return false; }
  };

  template <typename Check>
  decltype(auto) with_actioner (const tiny_game& game, size_t bool_threshold,
                                Check&& check) {
    posets::vectors::bool_threshold = bool_threshold;
    test_automaton automaton {game.states};
    const test_automaton* aut = &automaton;
    std::list<std::pair<bdd, std::list<std::vector<std::pair<unsigned, unsigned>>>>>
        empty;
    auto actioner = actioners::standard<state>::make (aut, empty, game.K);
    return std::forward<Check> (check) (actioner);
  }

  f1_result solve_f1 (
      const tiny_game& game, size_t bool_threshold,
      const rank_vector& initial,
      const acacia::solver_detail::forward_limits& limits = {},
      bool use_losing_antichain = false,
      std::size_t controller_minimisation_threshold =
          acacia::solver_detail::default_controller_minimisation_threshold) {
    return with_actioner (game, bool_threshold, [&] (auto& actioner) {
      const state initial_state = as_solver_state (initial);
      const state safe_state = as_solver_state (
          safe_vector (game.states, game.K, bool_threshold));
      return acacia::solver_detail::solve_forward_reachable_safety<SetOfStates> (
          initial_state, safe_state, game.inputs, actioner, limits,
          use_losing_antichain, controller_minimisation_threshold);
    });
  }

  choice_reduction reduce_first_input (
      const tiny_game& game, size_t bool_threshold, const rank_vector& parent,
      std::size_t controller_minimisation_threshold =
          acacia::solver_detail::default_controller_minimisation_threshold) {
    return with_actioner (game, bool_threshold, [&] (auto& actioner) {
      return acacia::solver_detail::forward_reachable_detail::
          reduce_controller_successors<state> (
              as_solver_state (parent), game.inputs.front ().second, actioner,
              controller_minimisation_threshold);
    });
  }

  void check_antichain_unit_behaviour () {
    using antichain = acacia::solver_detail::minimal_losing_antichain<state>;

    antichain chain;
    const state larger = as_solver_state (vec ({1, 1}));
    const state still_larger = as_solver_state (vec ({2, 1}));
    const state smaller = as_solver_state (vec ({0, 0}));
    expect ("antichain: first generator is inserted", chain.insert (larger));
    expect ("antichain: inserted generator subsumes itself",
            chain.subsumes (larger));
    expect ("antichain: inserted generator subsumes a larger rank",
            chain.subsumes (still_larger));
    expect ("antichain: one stored generator has exact size", chain.size () == 1);

    expect ("antichain: smaller generator is inserted", chain.insert (smaller));
    expect ("antichain: smaller generator evicts the larger one",
            chain.size () == 1 and chain.removals == 1);
    expect ("antichain: already subsumed insertion is rejected",
            not chain.insert (still_larger) and chain.size () == 1);

    antichain incomparable;
    const state left = as_solver_state (vec ({0, 1}));
    const state right = as_solver_state (vec ({1, 0}));
    expect ("antichain: first incomparable generator is inserted",
            incomparable.insert (left));
    expect ("antichain: second incomparable generator is inserted",
            incomparable.insert (right));
    expect ("antichain: incomparable generators both remain",
            incomparable.size () == 2);
    expect ("antichain: insertion counter is exact",
            incomparable.insertions == 2);
  }

  void check_antichain_rank_prefilter () {
    using antichain = acacia::solver_detail::minimal_losing_antichain<state>;
    constexpr unsigned dimensions = 5;
    constexpr unsigned generators = 200;
    constexpr unsigned queries = 2000;
    std::mt19937 gen {20260901};
    std::uniform_int_distribution<int> coordinate {-1, 4};

    auto random_state = [&] {
      rank_vector rank (dimensions, 0);
      for (std::size_t i = 0; i < dimensions; ++i)
        rank[i] = static_cast<VECTOR_ELT_T> (coordinate (gen));
      return as_solver_state (rank);
    };

    antichain chain;
    std::vector<state> direct_generators;
    direct_generators.reserve (generators);
    for (unsigned i = 0; i < generators; ++i) {
      state candidate = random_state ();
      const bool already_subsumed = std::ranges::any_of (
          direct_generators, [&candidate] (const state& stored) {
            return stored.partial_order (candidate).leq ();
          });
      expect ("antichain prefilter: random insertion agrees with direct scan",
              chain.insert (candidate) == not already_subsumed);
      direct_generators.push_back (candidate.copy ());
    }

    for (unsigned i = 0; i < queries; ++i) {
      const state candidate = random_state ();
      const bool expected = std::ranges::any_of (
          direct_generators, [&candidate] (const state& stored) {
            return stored.partial_order (candidate).leq ();
          });
      expect ("antichain prefilter: subsumption agrees with direct scan",
              chain.subsumes (candidate) == expected);
    }
    expect ("antichain prefilter: random pairs exercise rank skips",
            chain.prefilter_skips != 0);
  }

  template <typename StrategyRanks>
  void check_certificate_ranks (const std::string& label,
                                const tiny_game& game,
                                size_t bool_threshold,
                                const rank_vector& initial, bool winning,
                                const StrategyRanks& strategy_ranks) {
    if (not winning)
      return;
    ++certificates_checked;

    posets::vectors::bool_threshold = bool_threshold;
    std::vector<state> raw_generators;
    raw_generators.reserve (strategy_ranks.size ());
    for (const auto& rank : strategy_ranks)
      raw_generators.push_back (as_solver_state (rank));
    SetOfStates certificate {std::move (raw_generators)};

    const bool verified = with_actioner (game, bool_threshold, [&] (auto& actioner) {
      const SetOfStates envelope {
          as_solver_state (safe_vector (game.states, game.K, bool_threshold))};
      const state initial_state = as_solver_state (initial);
      return acacia::solver_detail::verify_winning_certificate (
          envelope, certificate, initial_state, game.inputs, actioner);
    });
    expect (label + ": shared winning-certificate verification", verified);
  }

  void check_certificate (const std::string& label, const tiny_game& game,
                          size_t bool_threshold, const rank_vector& initial,
                          const forward_result& result) {
    check_certificate_ranks (
        label + ": F0", game, bool_threshold, initial,
        result.status == forward_status::win_k, result.strategy_ranks);
  }

  void check_certificate (const std::string& label, const tiny_game& game,
                          size_t bool_threshold, const rank_vector& initial,
                          const f1_result& result) {
    check_certificate_ranks (
        label + ": F1", game, bool_threshold, initial,
        result.status == acacia::solver_detail::forward_result_status::win_k,
        result.strategy_ranks);
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

    const f1_result lazy = solve_f1 (game, bool_threshold, initial);
    const bool expected_win = expected == forward_status::win_k;
    expect (label + ": F1 status",
            (lazy.status
             == acacia::solver_detail::forward_result_status::win_k)
                == expected_win
                and (lazy.status
                     == acacia::solver_detail::forward_result_status::lose_k)
                        == not expected_win);
    check_certificate (label, game, bool_threshold, initial, lazy);
    return result;
  }

  void check_state_dependent_minimal_successors () {
    const action_vec larger =
        make_action (2, {{0, 0, true}, {1, 1, false}});
    const action_vec smaller =
        make_action (2, {{0, 0, false}, {1, 1, false}});
    const tiny_game comparable = make_game (
        2, static_cast<VECTOR_ELT_T> (2), {{larger, smaller, smaller}});

    const choice_reduction minimal =
        reduce_first_input (comparable, 2, vec ({0, 0}));
    expect ("F3 comparable: all raw actions are counted",
            minimal.raw_actions == 3);
    expect ("F3 comparable: exact quotient has two successors",
            minimal.distinct_successors == 2);
    expect ("F3 comparable: dominated successor is dropped",
            minimal.choices.size () == 1
                and as_rank (minimal.choices.front ().successor)
                        == vec ({0, 0}));
    expect ("F3 comparable: first action for the survivor is retained",
            minimal.choices.size () == 1
                and minimal.choices.front ().representative_action_index == 1);

    const f1_result integrated = solve_f1 (comparable, 2, vec ({0, 0}));
    expect ("F3 comparable: controller expansion exports exact metrics",
            integrated.raw_actions == 3
                and integrated.distinct_successors == 2
                and integrated.minimal_successors == 1
                and integrated.equality_reduction == 2.0 / 3.0
                and integrated.dominance_reduction == 1.0 / 2.0);

    const choice_reduction dedup_only =
        reduce_first_input (comparable, 2, vec ({0, 0}), 0);
    expect ("F3 cutoff zero: exact duplicate is still dropped",
            dedup_only.raw_actions == 3
                and dedup_only.distinct_successors == 2
                and dedup_only.choices.size () == 2);
    expect ("F3 cutoff zero: distinct action order is preserved",
            dedup_only.choices.size () == 2
                and dedup_only.choices[0].representative_action_index == 0
                and dedup_only.choices[1].representative_action_index == 1);

    const action_vec left = make_action (2, {{0, 0, false}});
    const action_vec right = make_action (2, {{1, 1, false}});
    const tiny_game incomparable = make_game (
        2, static_cast<VECTOR_ELT_T> (2), {{left, right}});
    const choice_reduction both =
        reduce_first_input (incomparable, 2, vec ({0, 0}));
    expect ("F3 incomparable: both successors survive in action order",
            both.choices.size () == 2
                and as_rank (both.choices[0].successor) == vec ({0, -1})
                and as_rank (both.choices[1].successor) == vec ({-1, 0})
                and both.choices[0].representative_action_index == 0
                and both.choices[1].representative_action_index == 1);

    const action_vec self = make_action (1, {{0, 0, false}});
    const tiny_game duplicate = make_game (
        1, static_cast<VECTOR_ELT_T> (2), {{self, self}});
    const choice_reduction exact =
        reduce_first_input (duplicate, 1, vec ({0}));
    expect ("F3 exact duplicate: one successor survives",
            exact.raw_actions == 2 and exact.distinct_successors == 1
                and exact.choices.size () == 1);
    expect ("F3 exact duplicate: first action index is retained",
            exact.choices.size () == 1
                and exact.choices.front ().representative_action_index == 0);
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
    struct counter_totals {
        std::size_t final_sizes = 0;
        std::size_t peak = 0;
        std::size_t queries = 0;
        std::size_t hits = 0;
        std::size_t prefilter_skips = 0;
        std::size_t insertions = 0;
        std::size_t invalidation_scans = 0;
        std::size_t nodes_invalidated = 0;

        void add (const f1_result& result) {
          final_sizes += result.losing_antichain_size;
          peak = std::max (peak, result.losing_antichain_peak);
          queries += result.subsumption_queries;
          hits += result.subsumption_hits;
          prefilter_skips += result.subsumption_prefilter_skips;
          insertions += result.losing_insertions;
          invalidation_scans += result.invalidation_scans;
          nodes_invalidated += result.nodes_invalidated;
        }
    } antichain_totals;
    struct reduction_totals {
        std::size_t raw_actions = 0;
        std::size_t distinct_successors = 0;
        std::size_t minimal_successors = 0;
        double minimisation_ms = 0.0;

        void add (const f1_result& result) {
          raw_actions += result.raw_actions;
          distinct_successors += result.distinct_successors;
          minimal_successors += result.minimal_successors;
          minimisation_ms += result.minimisation_ms;
        }

        [[nodiscard]] double equality_reduction () const {
          return raw_actions == 0
                     ? 1.0
                     : static_cast<double> (distinct_successors) / raw_actions;
        }

        [[nodiscard]] double dominance_reduction () const {
          return distinct_successors == 0
                     ? 1.0
                     : static_cast<double> (minimal_successors)
                           / distinct_successors;
        }
    } f3_totals, dedup_totals;

    for (unsigned trial = 0; trial < random_games; ++trial) {
      const tiny_game game = random_game (gen);
      std::uniform_int_distribution<size_t> threshold_distribution {0, game.states};
      const size_t bool_threshold = threshold_distribution (gen);
      const rank_vector initial = initial_vector (game.states, 0);
      const exact_region truth = brute_force_winning_region (game, bool_threshold);
      const forward_result result = solve_explicit_forward_game (
          initial, game.inputs, game.K, bool_threshold);
      const f1_result f3 = solve_f1 (game, bool_threshold, initial);
      const f1_result dedup_only =
          solve_f1 (game, bool_threshold, initial, {}, false, 0);
      const f1_result pruned = solve_f1 (game, bool_threshold, initial, {}, true);
      const std::string label = "random game " + std::to_string (trial);

      expect (label + ": F3 status agrees with cutoff-zero F1",
              f3.status == dedup_only.status);
      expect (label + ": antichain-on F3 status agrees with F1 baseline",
              pruned.status == dedup_only.status);

      if (result.status != forward_status::resource_limit) {
        expect (label + ": F0 agrees with the complete-domain greatest fixpoint",
                (result.status == forward_status::win_k)
                    == truth.contains (initial));
        expect (label + ": cutoff-zero F1/F0 winning statuses agree",
                (dedup_only.status
                 == acacia::solver_detail::forward_result_status::win_k)
                    == (result.status == forward_status::win_k));
        expect (label + ": cutoff-zero F1/F0 losing statuses agree",
                (dedup_only.status
                 == acacia::solver_detail::forward_result_status::lose_k)
                    == (result.status == forward_status::lose_k));
        expect (label + ": F3/F0 winning statuses agree",
                (f3.status
                 == acacia::solver_detail::forward_result_status::win_k)
                    == (result.status == forward_status::win_k));
        expect (label + ": F3/F0 losing statuses agree",
                (f3.status
                 == acacia::solver_detail::forward_result_status::lose_k)
                    == (result.status == forward_status::lose_k));
        expect (label + ": antichain-on F3/F0 winning statuses agree",
                (pruned.status
                 == acacia::solver_detail::forward_result_status::win_k)
                    == (result.status == forward_status::win_k));
        expect (label + ": antichain-on F3/F0 losing statuses agree",
                (pruned.status
                 == acacia::solver_detail::forward_result_status::lose_k)
                    == (result.status == forward_status::lose_k));
        ++compared;
      }
      check_certificate (label, game, bool_threshold, initial, result);
      check_certificate (label + ": F3", game, bool_threshold, initial, f3);
      check_certificate (label + ": cutoff-zero F1", game, bool_threshold,
                         initial, dedup_only);
      check_certificate (label + ": antichain-on F3", game, bool_threshold,
                         initial, pruned);
      antichain_totals.add (pruned);
      f3_totals.add (f3);
      dedup_totals.add (dedup_only);
    }

    expect ("random campaign performed at least 5000 F3/F1/F0 comparisons",
            compared >= 5000);
    expect ("random campaign exercised strict successor dominance",
            f3_totals.minimal_successors < f3_totals.distinct_successors);
    expect ("cutoff-zero campaign used exact-equality dedup only",
            dedup_totals.minimal_successors
                == dedup_totals.distinct_successors);
    expect ("random campaign exercised antichain queries",
            antichain_totals.queries != 0);
    expect ("random campaign inserted losing generators",
            antichain_totals.insertions != 0);
    expect ("every new losing generator caused one invalidation scan",
            antichain_totals.invalidation_scans == antichain_totals.insertions);
    std::cout << "forward-safety-game: all " << compared
              << " fixed-seed F3/F1/F0 statuses matched\n";
    std::cout << std::fixed << std::setprecision (6)
              << "forward-safety-game: F3 reduction totals"
              << " raw_actions=" << f3_totals.raw_actions
              << " distinct_successors=" << f3_totals.distinct_successors
              << " minimal_successors=" << f3_totals.minimal_successors
              << " minimisation_ms=" << f3_totals.minimisation_ms
              << " equality_reduction=" << f3_totals.equality_reduction ()
              << " dominance_reduction=" << f3_totals.dominance_reduction ()
              << '\n';
    std::cout << "forward-safety-game: cutoff-zero reduction totals"
              << " raw_actions=" << dedup_totals.raw_actions
              << " distinct_successors=" << dedup_totals.distinct_successors
              << " minimal_successors=" << dedup_totals.minimal_successors
              << " minimisation_ms=" << dedup_totals.minimisation_ms
              << " equality_reduction=" << dedup_totals.equality_reduction ()
              << " dominance_reduction="
              << dedup_totals.dominance_reduction () << '\n';
    std::cout << "forward-safety-game: antichain totals"
              << " losing_antichain_size_sum=" << antichain_totals.final_sizes
              << " losing_antichain_peak_max=" << antichain_totals.peak
              << " subsumption_queries=" << antichain_totals.queries
              << " subsumption_hits=" << antichain_totals.hits
              << " subsumption_prefilter_skips="
              << antichain_totals.prefilter_skips
              << " losing_insertions=" << antichain_totals.insertions
              << " losing_removals="
              << (antichain_totals.insertions - antichain_totals.final_sizes)
              << " invalidation_scans=" << antichain_totals.invalidation_scans
              << " nodes_invalidated=" << antichain_totals.nodes_invalidated
              << '\n';
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

    acacia::solver_detail::forward_limits f1_limits;
    f1_limits.max_ctrl_nodes = 0;
    const f1_result lazy = solve_f1 (game, 1, vec ({0}), f1_limits);
    expect ("F1 resource limit: reports resource_limit",
            lazy.status
                == acacia::solver_detail::forward_result_status::resource_limit);
    expect ("F1 resource limit: reports the controller-node cap",
            lazy.resource_limit
                == acacia::solver_detail::forward_resource_limit::ctrl_nodes);
    expect ("F1 resource limit: is not a winning verdict",
            lazy.status != acacia::solver_detail::forward_result_status::win_k);
    expect ("F1 resource limit: is not a losing verdict",
            lazy.status != acacia::solver_detail::forward_result_status::lose_k);
    expect ("F1 resource limit: retains counts gathered before returning",
            lazy.env_nodes == 1 and lazy.ctrl_nodes == 1
                and lazy.edges_selected == 0);
    expect ("F1 resource limit: antichain remains disabled by default",
            lazy.losing_antichain_size == 0
                and lazy.losing_antichain_peak == 0
                and lazy.subsumption_queries == 0
                and lazy.subsumption_hits == 0
                and lazy.subsumption_prefilter_skips == 0
                and lazy.losing_insertions == 0
                and lazy.invalidation_scans == 0
                and lazy.nodes_invalidated == 0);

    acacia::solver_detail::forward_limits env_limits;
    env_limits.max_env_nodes = 0;
    const f1_result env_limited = solve_f1 (game, 1, vec ({0}), env_limits);
    expect ("F1 resource limit: reports the environment-node cap distinctly",
            env_limited.status
                    == acacia::solver_detail::forward_result_status::resource_limit
                and env_limited.resource_limit
                    == acacia::solver_detail::forward_resource_limit::env_nodes);

    acacia::solver_detail::forward_limits edge_limits;
    edge_limits.max_edges = 0;
    const f1_result edge_limited = solve_f1 (game, 1, vec ({0}), edge_limits);
    expect ("F1 resource limit: reports the edge cap distinctly",
            edge_limited.status
                    == acacia::solver_detail::forward_result_status::resource_limit
                and edge_limited.resource_limit
                    == acacia::solver_detail::forward_resource_limit::edges);
    expect ("F1 resource limit: carries no strategy ranks",
            lazy.strategy_ranks.empty ());
  }

}  // namespace

int main () {
  check_antichain_unit_behaviour ();
  check_antichain_rank_prefilter ();
  check_state_dependent_minimal_successors ();
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
