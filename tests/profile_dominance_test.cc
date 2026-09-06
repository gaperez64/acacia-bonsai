#include "actioners/profile_dominance.hh"
#include "research/rank_action_replay.hh"
#include "tiny_game_oracle.hh"

#include <chrono>
#include <iostream>
#include <list>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

  namespace dominance = actioners::profile_dominance;
  namespace research = acacia::research;

  using dominance::action_vec;
  using dominance::action_vecs;
  using research::rank_vector;

  bool expect (std::string_view label, bool got, bool wanted = true) {
    if (got == wanted)
      return true;
    std::cerr << "profile-dominance: " << label << ": expected " << std::boolalpha << wanted
              << ", got " << got << '\n';
    return false;
  }

  action_vec make_action (
      unsigned destinations,
      std::initializer_list<std::tuple<unsigned, unsigned, bool>> endpoints) {
    action_vec result (destinations);
    for (const auto& [destination, source, increment] : endpoints)
      result[destination].emplace_back (source, increment);
    return result;
  }

  bool unit_cases () {
    bool ok = true;

    const action_vec equal_a = make_action (2, {{0, 1, false}, {1, 0, true}});
    const action_vec equal_b = equal_a;
    ok &= expect ("equal a <= b",
                  dominance::worse_or_equal (dominance::normalize (equal_a),
                                             dominance::normalize (equal_b)));
    ok &= expect ("equal b <= a",
                  dominance::worse_or_equal (dominance::normalize (equal_b),
                                             dominance::normalize (equal_a)));

    const action_vec endpoint_superset =
        make_action (1, {{0, 0, false}, {0, 1, false}});
    const action_vec endpoint_subset = make_action (1, {{0, 0, false}});
    ok &= expect ("endpoint superset is worse",
                  dominance::worse_or_equal (dominance::normalize (endpoint_superset),
                                             dominance::normalize (endpoint_subset)));
    ok &= expect ("endpoint subset is not worse",
                  dominance::worse_or_equal (dominance::normalize (endpoint_subset),
                                             dominance::normalize (endpoint_superset)),
                  false);

    const action_vec increment_true = make_action (1, {{0, 0, true}});
    const action_vec increment_false = make_action (1, {{0, 0, false}});
    ok &= expect ("increment one is worse than increment zero",
                  dominance::worse_or_equal (dominance::normalize (increment_true),
                                             dominance::normalize (increment_false)));
    ok &= expect ("increment zero is not worse than increment one",
                  dominance::worse_or_equal (dominance::normalize (increment_false),
                                             dominance::normalize (increment_true)),
                  false);

    const action_vec left = make_action (1, {{0, 0, false}});
    const action_vec right = make_action (1, {{0, 1, false}});
    ok &= expect ("incomparable endpoints left/right",
                  dominance::worse_or_equal (dominance::normalize (left),
                                             dominance::normalize (right)),
                  false);
    ok &= expect ("incomparable endpoints right/left",
                  dominance::worse_or_equal (dominance::normalize (right),
                                             dominance::normalize (left)),
                  false);

    const action_vec parallel =
        make_action (2, {{0, 3, false}, {0, 1, false}, {0, 3, true}, {0, 3, false}});
    const auto parallel_normalized = dominance::normalize (parallel);
    ok &= expect ("parallel entries collapse",
                  parallel_normalized.size () == 2 and parallel_normalized[0].size () == 2);
    ok &= expect ("parallel entries stay sorted",
                  parallel_normalized[0][0].source == 1
                      and not parallel_normalized[0][0].increment
                      and parallel_normalized[0][1].source == 3
                      and parallel_normalized[0][1].increment);
    ok &= expect ("parallel maximum equals accepting endpoint",
                  dominance::worse_or_equal (
                      parallel_normalized,
                      dominance::normalize (make_action (1, {{0, 3, true}}))));

    const action_vec empty_zero_destinations;
    const action_vec empty_two_destinations (2);
    const auto empty_zero = dominance::normalize (empty_zero_destinations);
    const auto empty_two = dominance::normalize (empty_two_destinations);
    ok &= expect ("empty actions are equivalent (short/long)",
                  dominance::worse_or_equal (empty_zero, empty_two)
                      and dominance::worse_or_equal (empty_two, empty_zero));
    ok &= expect ("empty action is not dominated by a nonempty action",
                  dominance::worse_or_equal (empty_zero,
                                             dominance::normalize (increment_false)),
                  false);
    ok &= expect ("nonempty action is dominated by empty action",
                  dominance::worse_or_equal (dominance::normalize (increment_false),
                                             empty_zero));

    action_vecs stable {make_action (1, {{0, 0, false}, {0, 1, false}}),
                        make_action (1, {{0, 2, false}}),
                        make_action (1, {{0, 0, false}})};
    const action_vecs stable_wanted {make_action (1, {{0, 2, false}}),
                                     make_action (1, {{0, 0, false}})};
    const auto stable_stats = dominance::prune (stable);
    ok &= expect ("later dominator removes earlier survivor in stable order",
                  stable == stable_wanted);
    ok &= expect ("stable prune stats",
                  stable_stats.actions_before == 3 and stable_stats.actions_after == 2
                      and not stable_stats.declined);

    action_vecs equal_actions {equal_a, equal_b};
    dominance::prune (equal_actions);
    ok &= expect ("equal actions retain first occurrence",
                  equal_actions == action_vecs {equal_a});

    dominance::budget no_pairs;
    no_pairs.max_pair_tests = 0;
    no_pairs.max_duration = std::chrono::seconds (1);
    action_vecs declined_actions {increment_true, increment_false};
    const auto declined_stats = dominance::prune (declined_actions, no_pairs);
    ok &= expect ("pair budget decline retains unproved actions",
                  declined_stats.declined and declined_actions.size () == 2
                      and declined_stats.pair_tests == 0);

    dominance::budget no_endpoints;
    no_endpoints.max_endpoint_visits = 0;
    no_endpoints.max_duration = std::chrono::seconds (1);
    action_vecs endpoint_decline {increment_true, increment_false};
    const auto endpoint_stats = dominance::prune (endpoint_decline, no_endpoints);
    ok &= expect ("endpoint budget decline retains unproved actions",
                  endpoint_stats.declined and endpoint_decline.size () == 2
                      and endpoint_stats.pair_tests == 1
                      and endpoint_stats.endpoint_visits == 0);

    dominance::budget one_pair;
    one_pair.max_pair_tests = 1;
    one_pair.max_duration = std::chrono::seconds (1);
    action_vecs one_proof {empty_zero_destinations, empty_zero_destinations, increment_false};
    const auto one_proof_stats = dominance::prune (one_proof, one_pair);
    ok &= expect ("budget decline preserves completed dominance proof",
                  one_proof_stats.declined and one_proof.size () == 2
                      and one_proof.front ().empty () and one_proof.back () == increment_false);

    return ok;
  }

  action_vec random_action (std::mt19937& generator, unsigned states) {
    std::uniform_int_distribution<int> presence (0, 3);
    std::uniform_int_distribution<int> increment (0, 1);
    action_vec result (states);
    for (unsigned destination = 0; destination < states; ++destination) {
      for (unsigned source = 0; source < states; ++source) {
        const int choice = presence (generator);
        if (choice == 0)
          continue;
        const bool first_increment = increment (generator) != 0;
        result[destination].emplace_back (source, first_increment);
        if (choice == 3)
          result[destination].emplace_back (source,
                                            increment (generator) != 0);
      }
    }
    return result;
  }

  std::string rank_string (const rank_vector& rank) {
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < rank.size (); ++i) {
      if (i != 0)
        out << ',';
      out << static_cast<int> (rank[i]);
    }
    return out.str () + ']';
  }

  std::string action_string (const action_vec& value) {
    std::ostringstream out;
    out << '[';
    for (std::size_t destination = 0; destination < value.size (); ++destination) {
      if (destination != 0)
        out << ',';
      out << destination << ":{";
      for (std::size_t edge = 0; edge < value[destination].size (); ++edge) {
        if (edge != 0)
          out << ',';
        out << '(' << value[destination][edge].first << ','
            << static_cast<int> (value[destination][edge].second) << ')';
      }
      out << '}';
    }
    return out.str () + ']';
  }

  std::string actions_string (const action_vecs& actions) {
    std::ostringstream out;
    std::size_t index = 0;
    for (const auto& action : actions)
      out << "  action " << index++ << " = " << action_string (action) << '\n';
    return out.str ();
  }

  bool covered_by (const std::vector<rank_vector>& left,
                   const std::vector<rank_vector>& right) {
    for (const auto& image : left) {
      bool covered = false;
      for (const auto& possible_dominator : right)
        if (research::leq (image, possible_dominator)) {
          covered = true;
          break;
        }
      if (not covered)
        return false;
    }
    return true;
  }

  bool differential_cases () {
    constexpr unsigned table_count = 240;
    std::mt19937 generator (0x5eedD01AU);
    std::uniform_int_distribution<unsigned> state_count (2, 4);
    std::uniform_int_distribution<unsigned> action_count (2, 6);
    std::uniform_int_distribution<int> bound (2, 3);

    dominance::budget generous;
    generous.max_pair_tests = 1000000;
    generous.max_endpoint_visits = 10000000;
    generous.max_duration = std::chrono::seconds (5);

    for (unsigned trial = 0; trial < table_count; ++trial) {
      const unsigned states = state_count (generator);
      const VECTOR_ELT_T K = static_cast<VECTOR_ELT_T> (bound (generator));
      const std::size_t bool_threshold = trial % (states + 1);
      action_vecs before;
      const unsigned count = action_count (generator);
      for (unsigned i = 0; i < count; ++i)
        before.push_back (random_action (generator, states));

      action_vecs after = before;
      const auto prune_stats = dominance::prune (after, generous);
      if (prune_stats.declined or after.size () > before.size ()) {
        std::cerr << "profile-dominance: pruning invariant failed at trial " << trial
                  << ": before=" << before.size () << ", after=" << after.size ()
                  << ", declined=" << std::boolalpha << prune_stats.declined << '\n'
                  << actions_string (before);
        return false;
      }

      action_vecs twice = after;
      const auto second_stats = dominance::prune (twice, generous);
      if (second_stats.declined or twice != after) {
        std::cerr << "profile-dominance: idempotence failed at trial " << trial << '\n'
                  << "before:\n" << actions_string (before)
                  << "once:\n" << actions_string (after)
                  << "twice:\n" << actions_string (twice);
        return false;
      }

      const auto domain = acacia::testing::entire_rank_domain (states, K);
      for (const auto& rank : domain) {
        std::vector<rank_vector> before_images;
        std::vector<rank_vector> after_images;
        before_images.reserve (before.size ());
        after_images.reserve (after.size ());
        for (const auto& action : before)
          before_images.push_back (
              research::apply_backward (rank, action, K, bool_threshold));
        for (const auto& action : after)
          after_images.push_back (
              research::apply_backward (rank, action, K, bool_threshold));

        if (not covered_by (before_images, after_images)
            or not covered_by (after_images, before_images)) {
          std::cerr << "profile-dominance: differential mismatch at trial " << trial
                    << ", states=" << states << ", K=" << static_cast<int> (K)
                    << ", bool_threshold=" << bool_threshold
                    << ", rank=" << rank_string (rank) << '\n'
                    << "full action table:\n" << actions_string (before)
                    << "pruned action table:\n" << actions_string (after)
                    << "before images:\n";
          for (const auto& image : before_images)
            std::cerr << "  " << rank_string (image) << '\n';
          std::cerr << "after images:\n";
          for (const auto& image : after_images)
            std::cerr << "  " << rank_string (image) << '\n';
          return false;
        }
      }
    }
    return true;
  }

}  // namespace

int main () {
  if (not unit_cases () or not differential_cases ())
    return 1;
  std::cout << "profile-dominance: ok (240 exact random tables)\n";
  return 0;
}
