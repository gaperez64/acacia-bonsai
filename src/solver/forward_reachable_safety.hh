#pragma once

/// A lazy reachable-state solver for one bounded forward safety game.
///
/// Environment states and controller choices are discovered only when the
/// current optimistic strategy reaches them.  A controller keeps the first
/// successor not yet proved losing and advances monotonically through its
/// remaining exact choices when that successor loses.

#include "actioners/direction.hh"
#include "solver/forward_game_nodes.hh"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>
#include <vector>

namespace acacia::solver_detail {

  enum class forward_result_status { win_k, lose_k, resource_limit };

  struct forward_limits {
      std::size_t max_env_nodes = 200000;
      std::size_t max_ctrl_nodes = 400000;
      /// Environment-input edges plus stored distinct successor choices.
      std::size_t max_edges = 2000000;
  };

  template <typename State>
  struct forward_solve_result {
      forward_result_status status;
      std::size_t env_nodes = 0;
      std::size_t ctrl_nodes = 0;
      std::size_t env_expanded = 0;
      std::size_t ctrl_expanded = 0;
      std::size_t choice_switches = 0;
      std::size_t intern_hits = 0;
      /// Successful controller-to-environment selections, including replacements.
      std::size_t edges_selected = 0;
      std::vector<State> strategy_ranks;
  };

  namespace forward_reachable_detail {

    template <typename State>
    using coordinate_type = typename State::value_type;

    template <typename State>
    using exact_state_key = std::vector<coordinate_type<State>>;

    /// Hash every coordinate.  Hash collisions are harmless because the
    /// unordered map still compares the complete coordinate vectors.
    template <typename State>
    struct exact_state_hash {
        std::size_t operator() (const exact_state_key<State>& key) const noexcept {
          std::size_t result = key.size ();
          for (const auto coordinate : key) {
            const std::size_t value =
                std::hash<coordinate_type<State>> {} (coordinate);
            result ^= value + static_cast<std::size_t> (0x9e3779b9U)
                      + (result << 6U) + (result >> 2U);
          }
          return result;
        }
    };

    template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
    class forward_search {
        using state = typename SetOfStates::value_type;
        using exact_key = exact_state_key<state>;

        struct queued_node {
            bool controller;
            std::size_t id;
        };

      public:
        forward_search (const state& initial, const state& safe,
                        const InputOutputFwdActions& input_output_fwd_actions,
                        Actioner& actioner, const forward_limits& limits)
          : initial {initial},
            safe {safe},
            input_output_fwd_actions {input_output_fwd_actions},
            actioner {actioner},
            limits {limits} {}

        forward_solve_result<state> solve () {
          const std::size_t initial_id = intern_env (initial.copy ());
          if (limit_exceeded)
            return make_result (forward_result_status::resource_limit);

          for (;;) {
            if (not drain_losing_queue ())
              return make_result (forward_result_status::resource_limit);
            if (env_nodes[initial_id].status == node_status::losing)
              return make_result (forward_result_status::lose_k);

            if (open_queue.empty ())
              return make_result (forward_result_status::win_k, initial_id);

            const queued_node next = open_queue.front ();
            open_queue.pop_front ();
            if (next.controller)
              expand_controller (next.id);
            else
              expand_environment (next.id);
            if (limit_exceeded)
              return make_result (forward_result_status::resource_limit);
          }
        }

      private:
        const state& initial;
        const state& safe;
        const InputOutputFwdActions& input_output_fwd_actions;
        Actioner& actioner;
        const forward_limits& limits;

        std::vector<forward_env_node<state>> env_nodes;
        std::vector<forward_ctrl_node<state>> ctrl_nodes;
        std::unordered_map<exact_key, std::size_t, exact_state_hash<state>> interned_envs;
        std::deque<queued_node> open_queue;
        std::deque<queued_node> losing_queue;

        std::size_t represented_edges = 0;
        std::size_t env_expanded = 0;
        std::size_t ctrl_expanded = 0;
        std::size_t choice_switches = 0;
        std::size_t intern_hits = 0;
        std::size_t edges_selected = 0;
        std::size_t next_proof_id = 1;
        bool limit_exceeded = false;

        [[nodiscard]] static exact_key key_for (const state& rank) {
          exact_key key;
          key.reserve (rank.size ());
          for (std::size_t i = 0; i < rank.size (); ++i)
            key.push_back (rank[i]);
          return key;
        }

        /// Safety is the posets order, not a representation-specific scan.
        [[nodiscard]] bool is_safe (const state& rank) const {
          return rank.partial_order (safe).leq ();
        }

        [[nodiscard]] std::optional<std::size_t> find_env (const state& rank) const {
          const auto found = interned_envs.find (key_for (rank));
          if (found == interned_envs.end ())
            return std::nullopt;
          return found->second;
        }

        std::size_t proof_id () { return next_proof_id++; }

        void mark_environment_losing (std::size_t env_id) {
          auto& env = env_nodes[env_id];
          if (env.status == node_status::losing)
            return;
          env.status = node_status::losing;
          env.losing_proof_id = proof_id ();
          losing_queue.push_back ({false, env_id});
        }

        void mark_controller_losing (std::size_t ctrl_id) {
          auto& ctrl = ctrl_nodes[ctrl_id];
          if (ctrl.status == node_status::losing)
            return;
          ctrl.status = node_status::losing;
          ctrl.selected_env.reset ();
          ctrl.losing_proof_id = proof_id ();
          losing_queue.push_back ({true, ctrl_id});
        }

        std::size_t intern_env (state rank) {
          exact_key key = key_for (rank);
          const auto found = interned_envs.find (key);
          if (found != interned_envs.end ()) {
            ++intern_hits;
            return found->second;
          }

          const std::size_t env_id = env_nodes.size ();
          env_nodes.push_back (
              {std::move (rank), node_status::open, {}, {}, 0});
          interned_envs.emplace (std::move (key), env_id);
          if (not is_safe (env_nodes[env_id].rank))
            mark_environment_losing (env_id);
          else
            open_queue.push_back ({false, env_id});

          if (env_nodes.size () > limits.max_env_nodes)
            limit_exceeded = true;
          return env_id;
        }

        bool add_represented_edge () {
          ++represented_edges;
          if (represented_edges > limits.max_edges) {
            limit_exceeded = true;
            return false;
          }
          return true;
        }

        void expand_environment (std::size_t env_id) {
          if (env_nodes[env_id].status != node_status::open)
            return;

          std::size_t input_index = 0;
          for ([[maybe_unused]] const auto& input_and_actions :
               input_output_fwd_actions) {
            const std::size_t ctrl_id = ctrl_nodes.size ();
            ctrl_nodes.push_back ({env_id, input_index, node_status::open, {},
                                   0, std::nullopt, 0});
            if (ctrl_nodes.size () > limits.max_ctrl_nodes) {
              limit_exceeded = true;
              return;
            }

            env_nodes[env_id].controller_ids.push_back (ctrl_id);
            if (not add_represented_edge ())
              return;
            open_queue.push_back ({true, ctrl_id});
            ++input_index;
          }

          env_nodes[env_id].status = node_status::expanded;
          ++env_expanded;
        }

        void expand_controller (std::size_t ctrl_id) {
          auto& ctrl = ctrl_nodes[ctrl_id];
          if (ctrl.status != node_status::open
              or env_nodes[ctrl.parent_env].status == node_status::losing)
            return;

          auto input = std::ranges::begin (input_output_fwd_actions);
          std::ranges::advance (input, ctrl.input_index);
          std::size_t action_index = 0;
          for (const auto& action : (*input).second) {
            state successor = actioner.apply (
                env_nodes[ctrl.parent_env].rank, action,
                actioners::direction::forward);
            const bool duplicate = std::ranges::any_of (
                ctrl.choices, [&successor] (const auto& choice) {
                  return choice.successor == successor;
                });
            if (not duplicate) {
              ctrl.choices.push_back ({std::move (successor), action_index});
              if (not add_represented_edge ())
                return;
            }
            ++action_index;
          }

          ctrl.current_choice = 0;
          ctrl.selected_env.reset ();
          if (not advance_controller (ctrl_id))
            return;
          if (ctrl.status != node_status::losing) {
            ctrl.status = node_status::expanded;
            ++ctrl_expanded;
          }
        }

        bool advance_controller (std::size_t ctrl_id) {
          auto& ctrl = ctrl_nodes[ctrl_id];

          // A selected choice that just lost is now behind the monotone scan.
          // Its selected_by entry is intentionally left stale in the old
          // environment node and is checked again when that node is processed.
          if (ctrl.selected_env.has_value ()) {
            ctrl.selected_env.reset ();
            ++ctrl.current_choice;
            ++choice_switches;
          }

          while (ctrl.current_choice < ctrl.choices.size ()) {
            const state& successor = ctrl.choices[ctrl.current_choice].successor;
            const auto known_env = find_env (successor);
            if (not is_safe (successor)
                or (known_env.has_value ()
                    and env_nodes[*known_env].status == node_status::losing)) {
              ++ctrl.current_choice;
              ++choice_switches;
              continue;
            }

            const std::size_t env_id = intern_env (successor.copy ());
            if (limit_exceeded)
              return false;
            ctrl.selected_env = env_id;
            env_nodes[env_id].selected_by.push_back (ctrl_id);
            ++edges_selected;
            return true;
          }

          mark_controller_losing (ctrl_id);
          return true;
        }

        bool drain_losing_queue () {
          while (not losing_queue.empty ()) {
            const queued_node losing = losing_queue.front ();
            losing_queue.pop_front ();
            if (losing.controller) {
              const std::size_t parent = ctrl_nodes[losing.id].parent_env;
              if (env_nodes[parent].status != node_status::losing)
                mark_environment_losing (parent);
              continue;
            }

            // Advancing a controller can intern a node and reallocate
            // env_nodes, so detach this list before following its entries.
            std::vector<std::size_t> selected_by =
                std::move (env_nodes[losing.id].selected_by);
            for (const std::size_t ctrl_id : selected_by) {
              auto& ctrl = ctrl_nodes[ctrl_id];
              if (ctrl.selected_env != losing.id
                  or env_nodes[ctrl.parent_env].status == node_status::losing)
                continue;
              if (not advance_controller (ctrl_id))
                return false;
            }
          }
          return true;
        }

        std::vector<state> build_strategy (std::size_t initial_id) const {
          std::vector<bool> seen_env (env_nodes.size (), false);
          std::vector<bool> seen_ctrl (ctrl_nodes.size (), false);
          std::deque<std::size_t> todo;
          std::vector<state> strategy;
          seen_env[initial_id] = true;
          todo.push_back (initial_id);

          while (not todo.empty ()) {
            const std::size_t env_id = todo.front ();
            todo.pop_front ();
            strategy.push_back (env_nodes[env_id].rank.copy ());

            for (const std::size_t ctrl_id : env_nodes[env_id].controller_ids) {
              if (seen_ctrl[ctrl_id])
                continue;
              seen_ctrl[ctrl_id] = true;
              const auto successor = ctrl_nodes[ctrl_id].selected_env;
              if (successor.has_value () and not seen_env[*successor]) {
                seen_env[*successor] = true;
                todo.push_back (*successor);
              }
            }
          }
          return strategy;
        }

        forward_solve_result<state> make_result (
            forward_result_status status,
            std::optional<std::size_t> initial_id = std::nullopt) const {
          forward_solve_result<state> result {
              status, env_nodes.size (), ctrl_nodes.size (), env_expanded,
              ctrl_expanded, choice_switches, intern_hits, edges_selected, {}};
          if (status == forward_result_status::win_k and initial_id.has_value ())
            result.strategy_ranks = build_strategy (*initial_id);
          return result;
        }
    };

  }  // namespace forward_reachable_detail

  /// Solve the reachable game while materializing only the current choices.
  ///
  /// Lazy choice is complete because every controller scans its finite,
  /// deduplicated action list monotonically: a choice is discarded only after
  /// it is unsafe or its exact successor has a losing proof.  If the queues
  /// empty, every reachable environment has every input expanded and every
  /// such controller still selects a safe, non-losing successor.
  ///
  /// A resource limit only interrupts that proof process.  It is deliberately
  /// returned as `resource_limit`, never converted into either game verdict.
  template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
  forward_solve_result<typename SetOfStates::value_type>
  solve_forward_reachable_safety (
      const typename SetOfStates::value_type& initial,
      const typename SetOfStates::value_type& safe,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      const forward_limits& limits = {}) {
    forward_reachable_detail::forward_search<SetOfStates, InputOutputFwdActions,
                                             Actioner>
        search {initial, safe, input_output_fwd_actions, actioner, limits};
    return search.solve ();
  }

  /// Spelling parallel to the F0 oracle for callers that name the game.
  template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
  forward_solve_result<typename SetOfStates::value_type>
  solve_forward_reachable_safety_game (
      const typename SetOfStates::value_type& initial,
      const typename SetOfStates::value_type& safe,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      const forward_limits& limits = {}) {
    return solve_forward_reachable_safety<SetOfStates> (
        initial, safe, input_output_fwd_actions, actioner, limits);
  }

}  // namespace acacia::solver_detail
