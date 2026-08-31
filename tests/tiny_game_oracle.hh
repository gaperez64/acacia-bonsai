#pragma once

#include "research/rank_action_replay.hh"

#include <cstddef>
#include <list>
#include <random>
#include <utility>
#include <vector>

#include <posets/downsets.hh>
#include <posets/vectors.hh>

namespace acacia::testing {

  using state = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using SetOfStates = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<state>;
  using input_classes =
      std::list<std::pair<unsigned, std::list<research::action_vec>>>;

  struct tiny_game {
      unsigned states;
      VECTOR_ELT_T K;
      input_classes inputs;
  };

  inline std::vector<research::rank_vector> entire_rank_domain (unsigned states,
                                                                 VECTOR_ELT_T K) {
    const size_t levels = static_cast<size_t> (K) + 2;
    size_t total = 1;
    for (unsigned i = 0; i < states; ++i)
      total *= levels;

    std::vector<research::rank_vector> domain;
    domain.reserve (total);
    for (size_t code = 0; code < total; ++code) {
      research::rank_vector value (states, 0);
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

  inline bool is_safe (const research::rank_vector& value, VECTOR_ELT_T K,
                       size_t bool_threshold) {
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
  inline exact_region brute_force_winning_region (const tiny_game& game,
                                                   size_t bool_threshold) {
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
            const auto image = research::apply_forward (domain[r], action, game.K);
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

  inline research::action_vec random_action (std::mt19937& gen, unsigned states) {
    std::uniform_int_distribution<int> include_edge {0, 2};
    std::uniform_int_distribution<int> increment {0, 1};
    research::action_vec action (states);
    for (unsigned destination = 0; destination < states; ++destination)
      for (unsigned source = 0; source < states; ++source)
        if (include_edge (gen) != 0)
          action[destination].emplace_back (source, increment (gen) != 0);
    return action;
  }

  inline tiny_game random_game (std::mt19937& gen) {
    std::uniform_int_distribution<unsigned> state_count {1, 4};
    std::uniform_int_distribution<int> bound {1, 2};
    std::uniform_int_distribution<unsigned> class_count {1, 3};
    std::uniform_int_distribution<unsigned> action_count {1, 3};

    tiny_game game {state_count (gen), static_cast<VECTOR_ELT_T> (bound (gen)), {}};
    const unsigned inputs = class_count (gen);
    for (unsigned input = 0; input < inputs; ++input) {
      std::list<research::action_vec> actions;
      const unsigned count = action_count (gen);
      for (unsigned i = 0; i < count; ++i)
        actions.push_back (random_action (gen, game.states));
      game.inputs.emplace_back (input, std::move (actions));
    }
    return game;
  }

}  // namespace acacia::testing
