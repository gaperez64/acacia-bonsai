#pragma once

/// A lazy reachable-state solver for one bounded forward safety game.
///
/// Environment states and controller choices are discovered only when the
/// current optimistic strategy reaches them.  A controller keeps the first
/// successor not yet proved losing and advances monotonically through its
/// remaining choices when that successor loses.
///
/// For a fixed rank r and input class i, suppose two actions have successors
/// s1 = tau_{i,a1}(r) and s2 = tau_{i,a2}(r), with s1 <= s2.  Then a2 is
/// unnecessary: every downward-closed winning region containing s2 also
/// contains s1, so keeping s2 can never help the controller.  A controller
/// node therefore needs only the Pareto-MINIMAL distinct successor vectors.
/// This reduction is state-dependent and additional to the global
/// semantic_mona action quotient, which cannot see it because it does not know
/// r.

#include "actioners/direction.hh"
#include "solver/forward_game_nodes.hh"
#include "solver/minimal_losing_antichain.hh"

#include <algorithm>
#include <cassert>
#include <chrono>
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

  enum class losing_reason {
    env_unsafe,
    env_subsumed,
    env_losing_input,
    ctrl_all_losing,
  };

  struct losing_proof {
      std::size_t id;
      losing_reason reason;
      std::size_t node;
      std::size_t witness = 0;
      std::vector<std::size_t> dependencies;
  };

  enum class forward_resource_limit {
    none,
    env_nodes,
    ctrl_nodes,
    edges,
  };

  /// Above this action count, use exact-equality deduplication rather than the
  /// quadratic Pareto pass.  This is solely a performance choice: both paths
  /// are exact, so changing the cutoff can never change a game verdict.
  inline constexpr std::size_t default_controller_minimisation_threshold = 64;

  struct forward_limits {
      std::size_t max_env_nodes = 200000;
      std::size_t max_ctrl_nodes = 400000;
      /// Environment-input edges plus stored distinct successor choices.
      std::size_t max_edges = 2000000;
  };

  template <typename State>
  struct forward_solve_result {
      forward_result_status status;
      forward_resource_limit resource_limit = forward_resource_limit::none;
      std::size_t env_nodes = 0;
      std::size_t ctrl_nodes = 0;
      std::size_t env_expanded = 0;
      std::size_t ctrl_expanded = 0;
      std::size_t choice_switches = 0;
      std::size_t intern_hits = 0;
      /// Successful controller-to-environment selections, including replacements.
      std::size_t edges_selected = 0;
      std::size_t losing_antichain_size = 0;
      std::size_t losing_antichain_peak = 0;
      std::size_t subsumption_queries = 0;
      std::size_t subsumption_hits = 0;
      std::size_t subsumption_prefilter_skips = 0;
      std::size_t losing_insertions = 0;
      std::size_t invalidation_scans = 0;
      std::size_t nodes_invalidated = 0;
      std::size_t raw_actions = 0;
      std::size_t distinct_successors = 0;
      std::size_t minimal_successors = 0;
      double minimisation_ms = 0.0;
      /// Retention ratios; an empty sample is reported as no reduction (1.0).
      double equality_reduction = 1.0;
      double dominance_reduction = 1.0;
      std::vector<losing_proof> losing_proofs;
      /// Rank vector of every environment node, indexed by node id, and the
      /// (parent env, input index) of every controller node.  A replayer needs
      /// these to recompute a losing proof from the game rather than trusting
      /// the solver's own status flags.
      std::vector<State> env_ranks;
      std::vector<std::pair<std::size_t, std::size_t>> ctrl_parents;
      std::size_t initial_env = 0;
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

    template <typename State>
    struct controller_choice_reduction {
        std::vector<successor_choice_for<State>> choices;
        std::size_t raw_actions = 0;
        std::size_t distinct_successors = 0;
        double minimisation_ms = 0.0;
    };

    /// Apply one input class in action order and retain either its exact
    /// distinct successors or, below the adaptive cutoff, their stable-order
    /// Pareto minima.  Representative indices always name the first action
    /// producing a retained exact successor.
    template <typename State, typename Actions, typename Actioner>
    controller_choice_reduction<State> reduce_controller_successors (
        const State& parent_rank, const Actions& actions, Actioner& actioner,
        std::size_t minimisation_threshold) {
      using clock = std::chrono::steady_clock;

      controller_choice_reduction<State> result;
      const std::size_t action_count =
          static_cast<std::size_t> (std::ranges::distance (actions));
      const bool pareto_minimise = action_count <= minimisation_threshold;
      result.choices.reserve (action_count);

      // Pareto minima can evict earlier successors.  Keep a bounded exact-seen
      // list as well so distinct_successors remains a true equality quotient.
      std::vector<State> distinct_seen;
      if (pareto_minimise)
        distinct_seen.reserve (action_count);

      std::size_t action_index = 0;
      for (const auto& action : actions) {
        State successor = actioner.apply (
            parent_rank, action, actioners::direction::forward);
        ++result.raw_actions;

        const auto minimisation_started = clock::now ();
        if (pareto_minimise) {
          const bool seen_exactly = std::ranges::any_of (
              distinct_seen, [&successor] (const State& seen) {
                return seen == successor;
              });
          if (not seen_exactly) {
            distinct_seen.push_back (successor.copy ());
            ++result.distinct_successors;
          }

          const bool duplicate = std::ranges::any_of (
              result.choices, [&successor] (const auto& choice) {
                return choice.successor == successor;
              });
          const bool dominated = not duplicate and std::ranges::any_of (
              result.choices, [&successor] (const auto& choice) {
                return choice.successor.partial_order (successor).leq ();
              });
          if (not duplicate and not dominated) {
            std::erase_if (result.choices, [&successor] (const auto& choice) {
              return successor.partial_order (choice.successor).leq ();
            });
            result.choices.push_back ({std::move (successor), action_index});
          }
        }
        else {
          const bool duplicate = std::ranges::any_of (
              result.choices, [&successor] (const auto& choice) {
                return choice.successor == successor;
              });
          if (not duplicate) {
            result.choices.push_back ({std::move (successor), action_index});
            ++result.distinct_successors;
          }
        }
        result.minimisation_ms +=
            std::chrono::duration<double, std::milli> (
                clock::now () - minimisation_started)
                .count ();
        ++action_index;
      }
      return result;
    }

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
                        Actioner& actioner, const forward_limits& limits,
                        bool use_losing_antichain,
                        std::size_t controller_minimisation_threshold)
          : initial {initial},
            safe {safe},
            input_output_fwd_actions {input_output_fwd_actions},
            actioner {actioner},
            limits {limits},
            use_losing_antichain {use_losing_antichain},
            controller_minimisation_threshold {
                controller_minimisation_threshold} {}

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
        const bool use_losing_antichain;
        const std::size_t controller_minimisation_threshold;

        std::vector<forward_env_node<state>> env_nodes;
        std::vector<forward_ctrl_node<state>> ctrl_nodes;
        std::unordered_map<exact_key, std::size_t, exact_state_hash<state>> interned_envs;
        std::deque<queued_node> open_queue;
        std::deque<queued_node> losing_queue;
        minimal_losing_antichain<state> losing_antichain;
        std::vector<std::size_t> losing_antichain_generators;
        std::vector<losing_proof> losing_proofs;

        std::size_t represented_edges = 0;
        std::size_t env_expanded = 0;
        std::size_t ctrl_expanded = 0;
        std::size_t choice_switches = 0;
        std::size_t intern_hits = 0;
        std::size_t edges_selected = 0;
        std::size_t losing_antichain_peak = 0;
        std::size_t invalidation_scans = 0;
        std::size_t nodes_invalidated = 0;
        std::size_t raw_actions = 0;
        std::size_t distinct_successors = 0;
        std::size_t minimal_successors = 0;
        double minimisation_ms = 0.0;
        std::size_t next_proof_id = 1;
        bool limit_exceeded = false;
        forward_resource_limit resource_limit = forward_resource_limit::none;

        void exceed_limit (forward_resource_limit reason) {
          limit_exceeded = true;
          if (resource_limit == forward_resource_limit::none)
            resource_limit = reason;
        }

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

        std::size_t append_losing_proof (
            losing_reason reason, std::size_t node, std::size_t witness = 0,
            std::vector<std::size_t> dependencies = {}) {
          const std::size_t id = next_proof_id++;
          for (const std::size_t dependency : dependencies)
            assert (dependency != 0 and dependency < id);
          losing_proofs.push_back (
              {id, reason, node, witness, std::move (dependencies)});
          return id;
        }

        [[nodiscard]] std::optional<std::size_t> subsuming_generator (
            const state& rank) const {
          for (const std::size_t generator : losing_antichain_generators)
            if (env_nodes[generator].rank.partial_order (rank).leq ())
              return generator;
          return std::nullopt;
        }

        void mark_environment_losing (
            std::size_t env_id, losing_reason reason, std::size_t witness = 0,
            std::vector<std::size_t> dependencies = {}) {
          auto& env = env_nodes[env_id];
          if (env.status == node_status::losing)
            return;
          env.status = node_status::losing;
          env.losing_proof_id = append_losing_proof (
              reason, env_id, witness, std::move (dependencies));
          losing_queue.push_back ({false, env_id});

          if (not use_losing_antichain or not is_safe (env.rank)
              or not losing_antichain.insert (env.rank))
            return;

          losing_antichain_peak =
              std::max (losing_antichain_peak, losing_antichain.size ());
          std::erase_if (
              losing_antichain_generators, [this, env_id] (std::size_t generator) {
                return env_nodes[env_id].rank
                    .partial_order (env_nodes[generator].rank)
                    .leq ();
              });
          losing_antichain_generators.push_back (env_id);
          ++invalidation_scans;

          // This is a full scan of every environment rank interned so far.
          // Marking a match queues the ordinary selected-controller
          // propagation; a recursively attempted insertion is already
          // subsumed by `env.rank` and therefore cannot start another scan.
          for (std::size_t id = 0; id < env_nodes.size (); ++id) {
            if (env_nodes[id].status == node_status::losing)
              continue;
            if (env.rank.partial_order (env_nodes[id].rank).leq ()) {
              ++nodes_invalidated;
              mark_environment_losing (
                  id, losing_reason::env_subsumed, env_id,
                  {env.losing_proof_id});
            }
          }
        }

        void mark_controller_losing (
            std::size_t ctrl_id, std::vector<std::size_t> dependencies) {
          auto& ctrl = ctrl_nodes[ctrl_id];
          if (ctrl.status == node_status::losing)
            return;
          ctrl.status = node_status::losing;
          ctrl.selected_env.reset ();
          ctrl.losing_proof_id = append_losing_proof (
              losing_reason::ctrl_all_losing, ctrl_id, 0,
              std::move (dependencies));
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
            mark_environment_losing (env_id, losing_reason::env_unsafe);
          else
            open_queue.push_back ({false, env_id});

          if (env_nodes.size () > limits.max_env_nodes)
            exceed_limit (forward_resource_limit::env_nodes);
          return env_id;
        }

        bool add_represented_edge () {
          ++represented_edges;
          if (represented_edges > limits.max_edges) {
            exceed_limit (forward_resource_limit::edges);
            return false;
          }
          return true;
        }

        void expand_environment (std::size_t env_id) {
          if (env_nodes[env_id].status != node_status::open)
            return;
          if (use_losing_antichain
              and losing_antichain.subsumes (env_nodes[env_id].rank)) {
            // The stored generator is a complete losing proof for this rank:
            // this is the "subsumed" environment-loss reason.
            const auto generator =
                subsuming_generator (env_nodes[env_id].rank);
            assert (generator.has_value ());
            mark_environment_losing (
                env_id, losing_reason::env_subsumed, *generator,
                {env_nodes[*generator].losing_proof_id});
            return;
          }

          std::size_t input_index = 0;
          for ([[maybe_unused]] const auto& input_and_actions :
               input_output_fwd_actions) {
            const std::size_t ctrl_id = ctrl_nodes.size ();
            ctrl_nodes.push_back ({env_id, input_index, node_status::open, {},
                                   0, std::nullopt, 0});
            if (ctrl_nodes.size () > limits.max_ctrl_nodes) {
              exceed_limit (forward_resource_limit::ctrl_nodes);
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
          auto reduction = reduce_controller_successors<state> (
              env_nodes[ctrl.parent_env].rank, (*input).second, actioner,
              controller_minimisation_threshold);
          raw_actions += reduction.raw_actions;
          distinct_successors += reduction.distinct_successors;
          minimal_successors += reduction.choices.size ();
          minimisation_ms += reduction.minimisation_ms;
          ctrl.choices = std::move (reduction.choices);
          for ([[maybe_unused]] const auto& choice : ctrl.choices)
            if (not add_represented_edge ())
              return;

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

          std::vector<std::size_t> dependencies;
          for (const auto& choice : ctrl.choices) {
            const auto env_id = find_env (choice.successor);
            if (env_id.has_value ()
                and env_nodes[*env_id].status == node_status::losing)
              dependencies.push_back (env_nodes[*env_id].losing_proof_id);
          }
          mark_controller_losing (ctrl_id, std::move (dependencies));
          return true;
        }

        bool drain_losing_queue () {
          while (not losing_queue.empty ()) {
            const queued_node losing = losing_queue.front ();
            losing_queue.pop_front ();
            if (losing.controller) {
              const std::size_t parent = ctrl_nodes[losing.id].parent_env;
              if (env_nodes[parent].status != node_status::losing)
                mark_environment_losing (
                    parent, losing_reason::env_losing_input, losing.id,
                    {ctrl_nodes[losing.id].losing_proof_id});
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
          forward_solve_result<state> result {status};
          result.resource_limit = resource_limit;
          result.env_nodes = env_nodes.size ();
          result.ctrl_nodes = ctrl_nodes.size ();
          result.env_expanded = env_expanded;
          result.ctrl_expanded = ctrl_expanded;
          result.choice_switches = choice_switches;
          result.intern_hits = intern_hits;
          result.edges_selected = edges_selected;
          result.losing_antichain_size = losing_antichain.size ();
          result.losing_antichain_peak = losing_antichain_peak;
          result.subsumption_queries = losing_antichain.queries;
          result.subsumption_hits = losing_antichain.hits;
          result.subsumption_prefilter_skips = losing_antichain.prefilter_skips;
          result.losing_insertions = losing_antichain.insertions;
          result.invalidation_scans = invalidation_scans;
          result.nodes_invalidated = nodes_invalidated;
          result.raw_actions = raw_actions;
          result.distinct_successors = distinct_successors;
          result.minimal_successors = minimal_successors;
          result.minimisation_ms = minimisation_ms;
          result.losing_proofs = losing_proofs;
          result.env_ranks.reserve (env_nodes.size ());
          for (const auto& node : env_nodes)
            result.env_ranks.push_back (node.rank.copy ());
          result.ctrl_parents.reserve (ctrl_nodes.size ());
          for (const auto& node : ctrl_nodes)
            result.ctrl_parents.emplace_back (node.parent_env, node.input_index);
          if (initial_id.has_value ())
            result.initial_env = *initial_id;
          if (raw_actions != 0)
            result.equality_reduction =
                static_cast<double> (distinct_successors) / raw_actions;
          if (distinct_successors != 0)
            result.dominance_reduction =
                static_cast<double> (minimal_successors)
                / distinct_successors;
          if (status == forward_result_status::win_k and initial_id.has_value ())
            result.strategy_ranks = build_strategy (*initial_id);
          return result;
        }
    };

  }  // namespace forward_reachable_detail

  /// Solve the reachable game while materializing only the current choices.
  ///
  /// Lazy choice is complete because every controller scans its finite,
  /// reduced action list monotonically: a choice is discarded only because an
  /// equal or smaller successor represents it, or after it is unsafe or its
  /// exact successor has a losing proof.  If the queues empty, every reachable
  /// environment has every input expanded and every such controller still
  /// selects a safe, non-losing successor.
  ///
  /// A resource limit only interrupts that proof process.  It is deliberately
  /// returned as `resource_limit`, never converted into either game verdict.
  template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
  forward_solve_result<typename SetOfStates::value_type>
  solve_forward_reachable_safety (
      const typename SetOfStates::value_type& initial,
      const typename SetOfStates::value_type& safe,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      const forward_limits& limits = {}, bool use_losing_antichain = false,
      std::size_t controller_minimisation_threshold =
          default_controller_minimisation_threshold) {
    forward_reachable_detail::forward_search<SetOfStates, InputOutputFwdActions,
                                             Actioner>
        search {initial, safe, input_output_fwd_actions, actioner, limits,
                use_losing_antichain, controller_minimisation_threshold};
    return search.solve ();
  }

  /// Spelling parallel to the F0 oracle for callers that name the game.
  template <typename SetOfStates, typename InputOutputFwdActions, typename Actioner>
  forward_solve_result<typename SetOfStates::value_type>
  solve_forward_reachable_safety_game (
      const typename SetOfStates::value_type& initial,
      const typename SetOfStates::value_type& safe,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      const forward_limits& limits = {}, bool use_losing_antichain = false,
      std::size_t controller_minimisation_threshold =
          default_controller_minimisation_threshold) {
    return solve_forward_reachable_safety<SetOfStates> (
        initial, safe, input_output_fwd_actions, actioner, limits,
        use_losing_antichain, controller_minimisation_threshold);
  }

}  // namespace acacia::solver_detail
