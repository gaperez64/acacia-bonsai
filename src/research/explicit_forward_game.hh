#pragma once

/// The deliberately explicit F0 safety-game oracle.
///
/// This constructs the complete reachable bipartite AND/OR graph at one bound
/// and only then solves it retrogradely.  It is a correctness reference for
/// later lazy algorithms, so rank vectors are interned exactly and no input or
/// action is pruned by an order-based heuristic.

#include "research/rank_action_replay.hh"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace acacia::research {

  enum class forward_status { win_k, lose_k, resource_limit };

  struct forward_limits {
      std::size_t max_env_nodes = 200000;
      std::size_t max_ctrl_nodes = 400000;
      std::size_t max_edges = 2000000;
  };

  struct forward_result {
      forward_status status;
      std::size_t env_nodes;
      std::size_t ctrl_nodes;
      std::size_t edges;  ///< Environment-input plus deduplicated action edges.
      std::vector<rank_vector> strategy_ranks;
  };

  struct env_node {
      rank_vector rank;
      bool unsafe;
      std::vector<std::size_t> controller_children;
      std::vector<std::size_t> predecessor_controllers;
      bool losing;
  };

  struct ctrl_node {
      std::size_t parent_env;
      std::size_t input_index;
      std::vector<std::size_t> successor_envs;
      bool losing;
      std::size_t remaining_nonlosing;
  };

  namespace forward_detail {

    template <typename InputClass>
    [[nodiscard]] decltype(auto) actions_of (const InputClass& input) {
      if constexpr (requires { input.second; })
        return (input.second);
      else
        return (input);
    }

  }  // namespace forward_detail

  /// Build and solve the full reachable game from `initial`.
  ///
  /// `inputs` may be either the solver-shaped range of `(input, actions)`
  /// pairs or a range whose elements are action ranges, as used by
  /// `input_action_table::actions`.  Input labels do not affect the game; their
  /// order becomes `ctrl_node::input_index`.
  template <typename InputClasses>
  forward_result solve_explicit_forward_game (
      const rank_vector& initial, const InputClasses& inputs, VECTOR_ELT_T K,
      std::size_t bool_threshold, const forward_limits& limits = {}) {
    std::vector<env_node> env_nodes;
    std::vector<ctrl_node> ctrl_nodes;
    std::map<rank_vector, std::size_t> interned_envs;
    std::deque<std::size_t> unexpanded;
    std::size_t edges = 0;

    const rank_vector safe = safe_vector (initial.size (), K, bool_threshold);
    auto resource_limit = [&] {
      return forward_result {forward_status::resource_limit, env_nodes.size (),
                             ctrl_nodes.size (), edges, {}};
    };
    auto add_env = [&] (rank_vector rank) {
      const auto found = interned_envs.find (rank);
      if (found != interned_envs.end ())
        return std::pair {found->second, false};

      const std::size_t id = env_nodes.size ();
      const bool unsafe = not leq (rank, safe);
      env_nodes.push_back ({std::move (rank), unsafe, {}, {}, false});
      interned_envs.emplace (env_nodes.back ().rank, id);
      if (not unsafe)
        unexpanded.push_back (id);
      return std::pair {id, true};
    };

    const auto [initial_id, initial_created] = add_env (rank_vector (initial));
    (void) initial_created;
    if (env_nodes.size () > limits.max_env_nodes)
      return resource_limit ();

    // Breadth-first construction.  Every safe environment node gets every
    // input class, and every controller node gets every distinct exact action
    // successor in first-seen order.
    while (not unexpanded.empty ()) {
      const std::size_t env_id = unexpanded.front ();
      unexpanded.pop_front ();

      std::size_t input_index = 0;
      for (const auto& input : inputs) {
        const std::size_t ctrl_id = ctrl_nodes.size ();
        ctrl_nodes.push_back ({env_id, input_index, {}, false, 0});
        if (ctrl_nodes.size () > limits.max_ctrl_nodes)
          return resource_limit ();

        env_nodes[env_id].controller_children.push_back (ctrl_id);
        ++edges;
        if (edges > limits.max_edges)
          return resource_limit ();

        for (const auto& action : forward_detail::actions_of (input)) {
          rank_vector successor = apply_forward (env_nodes[env_id].rank, action, K);
          const auto [successor_id, created] = add_env (std::move (successor));
          if (created and env_nodes.size () > limits.max_env_nodes)
            return resource_limit ();

          auto& successors = ctrl_nodes[ctrl_id].successor_envs;
          if (std::ranges::find (successors, successor_id) != successors.end ())
            continue;
          successors.push_back (successor_id);
          env_nodes[successor_id].predecessor_controllers.push_back (ctrl_id);
          ++edges;
          if (edges > limits.max_edges)
            return resource_limit ();
        }
        ++input_index;
      }
    }

    struct losing_event {
        bool controller;
        std::size_t id;
    };
    std::deque<losing_event> losing_queue;

    // Unsafe environment nodes are the retrograde seeds.
    for (std::size_t id = 0; id < env_nodes.size (); ++id)
      if (env_nodes[id].unsafe) {
        env_nodes[id].losing = true;
        losing_queue.push_back ({false, id});
      }

    // A controller node loses exactly when all its action successors lose.
    // With no actions this is immediately true.
    for (std::size_t id = 0; id < ctrl_nodes.size (); ++id) {
      ctrl_nodes[id].remaining_nonlosing = ctrl_nodes[id].successor_envs.size ();
      if (ctrl_nodes[id].remaining_nonlosing == 0) {
        ctrl_nodes[id].losing = true;
        losing_queue.push_back ({true, id});
      }
    }

    while (not losing_queue.empty ()) {
      const losing_event event = losing_queue.front ();
      losing_queue.pop_front ();
      if (not event.controller) {
        for (const std::size_t predecessor :
             env_nodes[event.id].predecessor_controllers) {
          auto& ctrl = ctrl_nodes[predecessor];
          if (ctrl.losing)
            continue;
          --ctrl.remaining_nonlosing;
          if (ctrl.remaining_nonlosing == 0) {
            ctrl.losing = true;
            losing_queue.push_back ({true, predecessor});
          }
        }
      }
      else {
        const std::size_t parent = ctrl_nodes[event.id].parent_env;
        if (not env_nodes[parent].losing) {
          env_nodes[parent].losing = true;
          losing_queue.push_back ({false, parent});
        }
      }
    }

    if (env_nodes[initial_id].losing)
      return {forward_status::lose_k, env_nodes.size (), ctrl_nodes.size (), edges, {}};

    // Select the first surviving action successor at every winning controller
    // node, then retain exactly the environment nodes reachable under those
    // choices while allowing every environment input.
    constexpr std::size_t no_successor = std::numeric_limits<std::size_t>::max ();
    std::vector<std::size_t> selected (ctrl_nodes.size (), no_successor);
    for (std::size_t id = 0; id < ctrl_nodes.size (); ++id)
      if (not ctrl_nodes[id].losing)
        for (const std::size_t successor : ctrl_nodes[id].successor_envs)
          if (not env_nodes[successor].losing) {
            selected[id] = successor;
            break;
          }

    std::vector<bool> seen_env (env_nodes.size (), false);
    std::vector<bool> seen_ctrl (ctrl_nodes.size (), false);
    std::deque<std::size_t> strategy_queue;
    std::vector<rank_vector> strategy_ranks;
    seen_env[initial_id] = true;
    strategy_queue.push_back (initial_id);
    while (not strategy_queue.empty ()) {
      const std::size_t env_id = strategy_queue.front ();
      strategy_queue.pop_front ();
      strategy_ranks.push_back (env_nodes[env_id].rank);

      for (const std::size_t ctrl_id : env_nodes[env_id].controller_children) {
        if (seen_ctrl[ctrl_id])
          continue;
        seen_ctrl[ctrl_id] = true;
        const std::size_t successor = selected[ctrl_id];
        if (successor != no_successor and not seen_env[successor]) {
          seen_env[successor] = true;
          strategy_queue.push_back (successor);
        }
      }
    }

    return {forward_status::win_k, env_nodes.size (), ctrl_nodes.size (), edges,
            std::move (strategy_ranks)};
  }

}  // namespace acacia::research
