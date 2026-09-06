#pragma once

/// A lazy reachable-state solver for one bounded forward safety game.
///
/// Environment states and controller choices are discovered only when the
/// current optimistic strategy reaches them.  A controller applies actions
/// one at a time, keeps the first successor not yet proved losing, and does not
/// apply another action until that selected successor loses.
///
/// ACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS retains the former comparison path:
/// apply every action up front, exact-deduplicate its successors, and Pareto
/// minimise small successor sets before selecting one.  Pareto minimisation is
/// deliberately confined to that eager path.
///
/// ACACIA_FORWARD_CONDITIONAL_COVERING lets the lazy path conditionally use a
/// non-losing downward cover in place of interning an optimistic successor.

#include "actioners/direction.hh"
#include "configuration.hh"
#include "solver/forward_game_nodes.hh"
#include "solver/minimal_losing_antichain.hh"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#if ACACIA_FORWARD_CONDITIONAL_COVERING
#include <map>
#endif
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
    rank_bytes,
    total_bytes,
  };

  /// On the eager comparison path, use exact-equality deduplication rather than
  /// the quadratic Pareto pass above this action count.  This is solely a
  /// performance choice: both eager reductions are exact, so changing the
  /// cutoff can never change a game verdict.  The lazy path ignores it.
  inline constexpr std::size_t default_controller_minimisation_threshold = 64;

  struct forward_limits {
      std::size_t max_env_nodes = 200000;
      std::size_t max_ctrl_nodes = 400000;
      /// Environment-input edges plus stored distinct successor choices.
      std::size_t max_edges = 2000000;
      /// Defaults are deliberately unbounded until byte caps are tuned from
      /// measurements, so adding the accounting cannot change existing runs.
      std::size_t max_rank_bytes = std::numeric_limits<std::size_t>::max ();
      std::size_t max_total_bytes = std::numeric_limits<std::size_t>::max ();
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
      std::size_t nodes_checked = 0;
      std::size_t nodes_invalidated = 0;
      std::size_t raw_actions = 0;
      std::size_t forward_actions_skipped = 0;
      std::size_t forward_covers_created = 0;
      std::size_t forward_covers_resolved = 0;
      std::size_t forward_cover_search_visits = 0;
      std::size_t distinct_successors = 0;
      std::size_t minimal_successors = 0;
      /// Logical retained bytes.  The categories count objects and payload
      /// elements, rather than allocator capacity and implementation overhead.
      std::size_t rank_bytes = 0;
      std::size_t environment_node_bytes = 0;
      std::size_t controller_node_bytes = 0;
      std::size_t reverse_dependency_bytes = 0;
      std::size_t proof_bytes = 0;
      std::size_t hash_index_bytes = 0;
      std::size_t losing_antichain_bytes = 0;
      /// Aggregates used by diagnostics: node bytes are the two node records;
      /// index bytes are node links, proofs, the hash index, and antichain.
      std::size_t node_bytes = 0;
      std::size_t index_bytes = 0;
      std::size_t total_bytes = 0;
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
      std::vector<State> losing_antichain_ranks;
      std::size_t initial_env = 0;
      std::vector<State> strategy_ranks;
  };

  namespace forward_reachable_detail {

    template <typename State>
    using coordinate_type = typename State::value_type;

    template <typename State>
    [[nodiscard]] std::uint64_t coordinate_hash (const State& rank) noexcept {
      std::uint64_t result = static_cast<std::uint64_t> (rank.size ());
      for (std::size_t i = 0; i < rank.size (); ++i) {
        const auto value = static_cast<std::uint64_t> (
            std::hash<coordinate_type<State>> {} (rank[i]));
        result ^= value + UINT64_C (0x9e3779b97f4a7c15)
                  + (result << 6U) + (result >> 2U);
      }
      return result;
    }

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

    template <typename SetOfStates, typename InputOutputFwdActions,
              typename Actioner, bool EagerMinimalSuccessors>
    class forward_search {
        using state = typename SetOfStates::value_type;
        using env_id_bucket = std::vector<std::size_t>;
        using env_hash_index =
            std::unordered_map<std::uint64_t, env_id_bucket>;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
        using rank_sum_type = std::int64_t;
        using env_rank_index = std::map<rank_sum_type, env_id_bucket>;
#endif

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
            if (limit_exceeded)
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
        std::vector<std::vector<successor_choice_for<state>>>
            eager_controller_choices;
        env_hash_index interned_envs;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
        /// Coordinate-sum buckets containing only currently non-losing ranks.
        env_rank_index coverable_envs;
#endif
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
        std::size_t nodes_checked = 0;
        std::size_t nodes_invalidated = 0;
        std::size_t raw_actions = 0;
        std::size_t forward_actions_skipped = 0;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
        std::size_t forward_covers_created = 0;
        std::size_t forward_covers_resolved = 0;
        std::size_t forward_cover_search_visits = 0;
#endif
        std::size_t distinct_successors = 0;
        std::size_t minimal_successors = 0;
        std::size_t rank_bytes = 0;
        std::size_t environment_node_bytes = 0;
        std::size_t controller_node_bytes = 0;
        std::size_t reverse_dependency_bytes = 0;
        std::size_t proof_bytes = 0;
        std::size_t hash_index_bytes = 0;
        std::size_t losing_antichain_bytes = 0;
        std::size_t total_bytes = 0;
        double minimisation_ms = 0.0;
        std::size_t next_proof_id = 1;
        bool limit_exceeded = false;
        forward_resource_limit resource_limit = forward_resource_limit::none;

        void exceed_limit (forward_resource_limit reason) {
          limit_exceeded = true;
          if (resource_limit == forward_resource_limit::none)
            resource_limit = reason;
        }

        void accounting_overflow (std::size_t& category, bool is_rank) {
          category = std::numeric_limits<std::size_t>::max ();
          total_bytes = std::numeric_limits<std::size_t>::max ();
          exceed_limit (is_rank ? forward_resource_limit::rank_bytes
                                : forward_resource_limit::total_bytes);
        }

        /// Account a logical retained payload exactly once.  This intentionally
        /// does not guess allocator bucket/node headers or spare vector capacity:
        /// the estimate is approximate, while the arithmetic that defines it is
        /// exact and overflow checked.
        void account_bytes (std::size_t& category, std::size_t bytes,
                            bool is_rank = false) {
          const auto maximum = std::numeric_limits<std::size_t>::max ();
          if (bytes > maximum - category) {
            accounting_overflow (category, is_rank);
            return;
          }
          category += bytes;

          if (bytes > maximum - total_bytes) {
            total_bytes = maximum;
            exceed_limit (forward_resource_limit::total_bytes);
          }
          else {
            total_bytes += bytes;
          }

          if (is_rank and rank_bytes > limits.max_rank_bytes)
            exceed_limit (forward_resource_limit::rank_bytes);
          if (total_bytes > limits.max_total_bytes)
            exceed_limit (forward_resource_limit::total_bytes);
        }

        void account_items (std::size_t& category, std::size_t count,
                            std::size_t item_bytes, bool is_rank = false) {
          const auto maximum = std::numeric_limits<std::size_t>::max ();
          if (item_bytes != 0 and count > maximum / item_bytes) {
            accounting_overflow (category, is_rank);
            return;
          }
          account_bytes (category, count * item_bytes, is_rank);
        }

        void release_items (std::size_t& category, std::size_t count,
                            std::size_t item_bytes) {
          assert (item_bytes == 0 or count <= category / item_bytes);
          const std::size_t bytes = count * item_bytes;
          assert (bytes <= category and bytes <= total_bytes);
          category -= bytes;
          total_bytes -= bytes;
        }

        void account_rank (const state& rank) {
          account_items (rank_bytes, rank.size (),
                         sizeof (coordinate_type<state>), true);
        }

#if ACACIA_FORWARD_CONDITIONAL_COVERING
        void release_rank (const state& rank) {
          release_items (rank_bytes, rank.size (),
                         sizeof (coordinate_type<state>));
        }
#endif

        void account_hash_index_entry (bool new_bucket) {
          if (new_bucket)
            account_bytes (hash_index_bytes,
                           sizeof (typename env_hash_index::value_type));
          account_bytes (hash_index_bytes, sizeof (std::size_t));
        }

#if ACACIA_FORWARD_CONDITIONAL_COVERING
        void account_cover_index_entry (bool new_bucket) {
          if (new_bucket)
            account_bytes (hash_index_bytes,
                           sizeof (typename env_rank_index::value_type));
          account_bytes (hash_index_bytes, sizeof (std::size_t));
        }

        void remove_cover_index_entry (std::size_t env_id) {
          auto bucket = coverable_envs.find (
              rank_sum_of (env_nodes[env_id].rank));
          assert (bucket != coverable_envs.end ());
          const auto entry = std::ranges::find (bucket->second, env_id);
          assert (entry != bucket->second.end ());
          bucket->second.erase (entry);
          release_items (hash_index_bytes, 1, sizeof (std::size_t));
          if (bucket->second.empty ()) {
            release_items (
                hash_index_bytes, 1,
                sizeof (typename env_rank_index::value_type));
            coverable_envs.erase (bucket);
          }
        }
#endif

        [[nodiscard]] static std::size_t saturated_sum (
            std::initializer_list<std::size_t> terms) {
          const auto maximum = std::numeric_limits<std::size_t>::max ();
          std::size_t result = 0;
          for (const std::size_t term : terms) {
            if (term > maximum - result)
              return maximum;
            result += term;
          }
          return result;
        }

        void account_losing_antichain_change (std::size_t old_size,
                                               const state& inserted) {
          const std::size_t new_size = losing_antichain.size ();
          assert (new_size != 0 and new_size - 1 <= old_size);
          const std::size_t removed = old_size - (new_size - 1);
          const auto maximum = std::numeric_limits<std::size_t>::max ();
          if (inserted.size () > maximum / sizeof (coordinate_type<state>)) {
            accounting_overflow (losing_antichain_bytes, false);
            return;
          }
          const std::size_t coordinates =
              inserted.size () * sizeof (coordinate_type<state>);
          constexpr std::size_t record =
              sizeof (state) + sizeof (std::int64_t) + sizeof (std::size_t);
          if (coordinates > maximum - record) {
            accounting_overflow (losing_antichain_bytes, false);
            return;
          }
          const std::size_t generator_bytes = record + coordinates;
          assert (generator_bytes == 0
                  or removed <= losing_antichain_bytes / generator_bytes);
          release_items (losing_antichain_bytes, removed, generator_bytes);
          account_bytes (losing_antichain_bytes, generator_bytes);
        }

        /// Safety is the posets order, not a representation-specific scan.
        [[nodiscard]] bool is_safe (const state& rank) const {
          return rank.partial_order (safe).leq ();
        }

#if ACACIA_FORWARD_CONDITIONAL_COVERING
        [[nodiscard]] static rank_sum_type rank_sum_of (const state& rank) {
          rank_sum_type sum = 0;
          for (std::size_t i = 0; i < rank.size (); ++i)
            sum += static_cast<rank_sum_type> (rank[i]);
          return sum;
        }
#endif

        [[nodiscard]] std::optional<std::size_t> find_env (
            const state& rank, std::uint64_t hash) const {
          const auto found = interned_envs.find (hash);
          if (found == interned_envs.end ())
            return std::nullopt;
          // The hash selects only a small candidate bucket.  Identity is
          // decided by this exact coordinate comparison; it cannot be skipped,
          // because a 64-bit hash collision must never merge unequal states.
          for (const std::size_t env_id : found->second)
            if (env_nodes[env_id].rank == rank)
              return env_id;
          return std::nullopt;
        }

        [[nodiscard]] std::optional<std::size_t> find_env (
            const state& rank) const {
          return find_env (rank, coordinate_hash (rank));
        }

#if ACACIA_FORWARD_CONDITIONAL_COVERING
        /// Return the deterministic non-losing dominator of `successor`:
        /// equality first, then minimum coordinate-sum slack, then oldest id.
        /// The strict search starts above successor's rank-sum bucket because
        /// pointwise domination at equal sum can only be exact equality.
        [[nodiscard]] std::optional<std::size_t> find_cover (
            const state& successor) {
          const auto exact = find_env (successor);
          if (exact.has_value ()
              and env_nodes[*exact].status != node_status::losing) {
            ++forward_cover_search_visits;
            return exact;
          }

          for (auto bucket = coverable_envs.upper_bound (
                   rank_sum_of (successor));
               bucket != coverable_envs.end (); ++bucket) {
            // Ids are appended in creation order, so the first match in the
            // first matching rank bucket is the required oldest minimum-slack
            // cover.
            for (const std::size_t env_id : bucket->second) {
              ++forward_cover_search_visits;
              assert (env_nodes[env_id].status != node_status::losing);
              if (successor.partial_order (env_nodes[env_id].rank).leq ())
                return env_id;
            }
          }
          return std::nullopt;
        }
#endif

        std::size_t append_losing_proof (
            losing_reason reason, std::size_t node, std::size_t witness = 0,
            std::vector<std::size_t> dependencies = {}) {
          const std::size_t id = next_proof_id++;
          for (const std::size_t dependency : dependencies)
            assert (dependency != 0 and dependency < id);
          const std::size_t dependency_count = dependencies.size ();
          losing_proofs.push_back (
              {id, reason, node, witness, std::move (dependencies)});
          account_bytes (proof_bytes, sizeof (losing_proof));
          account_items (
              proof_bytes, dependency_count, sizeof (std::size_t));
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
#if ACACIA_FORWARD_CONDITIONAL_COVERING
          if (is_safe (env.rank))
            remove_cover_index_entry (env_id);
#endif
          env.status = node_status::losing;
          env.losing_proof_id = append_losing_proof (
              reason, env_id, witness, std::move (dependencies));
          losing_queue.push_back ({false, env_id});

          if (not use_losing_antichain or not is_safe (env.rank))
            return;

          const std::size_t old_antichain_size = losing_antichain.size ();
          if (not losing_antichain.insert (env.rank))
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
          assert (losing_antichain_generators.size ()
                  == losing_antichain.size ());
          account_losing_antichain_change (old_antichain_size, env.rank);
          ++invalidation_scans;

          // This is a full scan of every environment rank interned so far.
          // Marking a match queues the ordinary selected-controller
          // propagation; a recursively attempted insertion is already
          // subsumed by `env.rank` and therefore cannot start another scan.
          for (std::size_t id = 0; id < env_nodes.size (); ++id) {
            ++nodes_checked;
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
#if ACACIA_FORWARD_CONDITIONAL_COVERING
          if (ctrl.covered_actual_successor.has_value ()) {
            release_rank (*ctrl.covered_actual_successor);
            ctrl.covered_actual_successor.reset ();
          }
          ctrl.cover_env.reset ();
#endif
          ctrl.selected_env.reset ();
          ctrl.selected_action_index.reset ();
          ctrl.losing_proof_id = append_losing_proof (
              losing_reason::ctrl_all_losing, ctrl_id, 0,
              std::move (dependencies));
          losing_queue.push_back ({true, ctrl_id});
        }

        std::size_t intern_env (state rank) {
          const std::uint64_t hash = coordinate_hash (rank);
          const auto found = find_env (rank, hash);
          if (found.has_value ()) {
            ++intern_hits;
            return *found;
          }

          const std::size_t env_id = env_nodes.size ();
#if ACACIA_FORWARD_CONDITIONAL_COVERING
          env_nodes.push_back (
              {std::move (rank), node_status::open, {}, {}, {}, 0});
#else
          env_nodes.push_back (
              {std::move (rank), node_status::open, {}, {}, 0});
#endif
          account_bytes (
              environment_node_bytes, sizeof (forward_env_node<state>));
          account_rank (env_nodes[env_id].rank);
          auto [bucket, new_bucket] = interned_envs.try_emplace (hash);
          bucket->second.push_back (env_id);
          account_hash_index_entry (new_bucket);
          if (not is_safe (env_nodes[env_id].rank))
            mark_environment_losing (env_id, losing_reason::env_unsafe);
          else {
#if ACACIA_FORWARD_CONDITIONAL_COVERING
            auto [rank_bucket, new_rank_bucket] =
                coverable_envs.try_emplace (
                    rank_sum_of (env_nodes[env_id].rank));
            rank_bucket->second.push_back (env_id);
            account_cover_index_entry (new_rank_bucket);
#endif
            open_queue.push_back ({false, env_id});
          }

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
#if ACACIA_FORWARD_CONDITIONAL_COVERING
            ctrl_nodes.push_back ({env_id, input_index, node_status::open, 0,
                                   std::nullopt, std::nullopt,
                                   std::nullopt, std::nullopt, {}, 0});
#else
            ctrl_nodes.push_back ({env_id, input_index, node_status::open, 0,
                                   std::nullopt, std::nullopt, {}, 0});
#endif
            account_bytes (
                controller_node_bytes, sizeof (forward_ctrl_node<state>));
            if constexpr (EagerMinimalSuccessors)
              {
                eager_controller_choices.emplace_back ();
                account_bytes (
                    controller_node_bytes,
                    sizeof (std::vector<successor_choice_for<state>>));
              }
            if (ctrl_nodes.size () > limits.max_ctrl_nodes) {
              exceed_limit (forward_resource_limit::ctrl_nodes);
              return;
            }

            env_nodes[env_id].controller_ids.push_back (ctrl_id);
            account_bytes (
                reverse_dependency_bytes, sizeof (std::size_t));
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

          ctrl.next_action_index = 0;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
          ctrl.covered_actual_successor.reset ();
          ctrl.cover_env.reset ();
#endif
          ctrl.selected_env.reset ();
          ctrl.selected_action_index.reset ();
          ctrl.tried_env_ids.clear ();

          if constexpr (EagerMinimalSuccessors) {
            auto input = std::ranges::begin (input_output_fwd_actions);
            std::ranges::advance (input, ctrl.input_index);
            auto reduction = reduce_controller_successors<state> (
                env_nodes[ctrl.parent_env].rank, (*input).second, actioner,
                controller_minimisation_threshold);
            raw_actions += reduction.raw_actions;
            distinct_successors += reduction.distinct_successors;
            minimal_successors += reduction.choices.size ();
            minimisation_ms += reduction.minimisation_ms;
            eager_controller_choices[ctrl_id] = std::move (reduction.choices);
            for (const auto& choice : eager_controller_choices[ctrl_id]) {
              account_bytes (
                  controller_node_bytes,
                  sizeof (successor_choice_for<state>));
              account_rank (choice.successor);
              if (not add_represented_edge ())
                return;
              if (limit_exceeded)
                return;
            }
          }

          if (not advance_controller (ctrl_id))
            return;
          if (ctrl.status != node_status::losing) {
            ctrl.status = node_status::expanded;
            ++ctrl_expanded;
          }
        }

        bool advance_controller (std::size_t ctrl_id) {
          if constexpr (EagerMinimalSuccessors)
            return advance_eager_controller (ctrl_id);
          else
            return advance_lazy_controller (ctrl_id);
        }

        bool advance_eager_controller (std::size_t ctrl_id) {
          auto& ctrl = ctrl_nodes[ctrl_id];
          const auto& choices = eager_controller_choices[ctrl_id];

          // A selected choice that just lost is now behind the monotone scan.
          // Its selected_by entry is intentionally left stale in the old
          // environment node and is checked again when that node is processed.
          if (ctrl.selected_env.has_value ()) {
            ctrl.selected_env.reset ();
            ctrl.selected_action_index.reset ();
            ++ctrl.next_action_index;
            ++choice_switches;
          }

          while (ctrl.next_action_index < choices.size ()) {
            const auto& choice = choices[ctrl.next_action_index];
            const state& successor = choice.successor;
            const auto known_env = find_env (successor);
            if (not is_safe (successor)
                or (known_env.has_value ()
                    and env_nodes[*known_env].status == node_status::losing)) {
              ++ctrl.next_action_index;
              ++choice_switches;
              continue;
            }

            if (use_losing_antichain
                and losing_antichain.subsumes (successor)) {
              [[maybe_unused]] const auto generator =
                  subsuming_generator (successor);
              assert (generator.has_value ());
              ++ctrl.next_action_index;
              ++choice_switches;
              continue;
            }

            const std::size_t env_id = intern_env (successor.copy ());
            if (limit_exceeded)
              return false;
            ctrl.selected_env = env_id;
            ctrl.selected_action_index = choice.representative_action_index;
            ctrl.tried_env_ids.push_back (env_id);
            account_bytes (
                reverse_dependency_bytes, sizeof (std::size_t));
            env_nodes[env_id].selected_by.push_back (ctrl_id);
            account_bytes (
                reverse_dependency_bytes, sizeof (std::size_t));
            ++edges_selected;
            return not limit_exceeded;
          }

          std::vector<std::size_t> dependencies;
          for (const auto& choice : choices) {
            const auto env_id = find_env (choice.successor);
            if (env_id.has_value ()
                and env_nodes[*env_id].status == node_status::losing)
              dependencies.push_back (env_nodes[*env_id].losing_proof_id);
          }
          mark_controller_losing (ctrl_id, std::move (dependencies));
          return not limit_exceeded;
        }

        bool advance_lazy_controller (std::size_t ctrl_id) {
          auto& ctrl = ctrl_nodes[ctrl_id];

          auto input = std::ranges::begin (input_output_fwd_actions);
          std::ranges::advance (input, ctrl.input_index);
          const auto& actions = (*input).second;
          const std::size_t action_count =
              static_cast<std::size_t> (std::ranges::size (actions));

#if ACACIA_FORWARD_CONDITIONAL_COVERING
          if (ctrl.cover_env.has_value ()) {
            assert (ctrl.selected_env == ctrl.cover_env);
            assert (env_nodes[*ctrl.cover_env].status == node_status::losing);
            assert (ctrl.covered_actual_successor.has_value ());
            assert (ctrl.selected_action_index.has_value ());

            state actual = std::move (*ctrl.covered_actual_successor);
            const std::size_t action_index = *ctrl.selected_action_index;
            release_rank (actual);
            ctrl.covered_actual_successor.reset ();
            ctrl.cover_env.reset ();
            ctrl.selected_env.reset ();
            ++forward_covers_resolved;

            const auto known_actual = find_env (actual);
            bool actual_is_losing =
                known_actual.has_value ()
                and env_nodes[*known_actual].status == node_status::losing;
            if (not actual_is_losing and use_losing_antichain
                and losing_antichain.subsumes (actual)) {
              [[maybe_unused]] const auto generator =
                  subsuming_generator (actual);
              assert (generator.has_value ());
              actual_is_losing = true;
            }

            if (not actual_is_losing) {
              // A lost dominator proves nothing about the smaller actual
              // image.  Keep the same controller action, intern its saved
              // image, and let ordinary expansion decide it.  Only if that
              // actual image and every later action fail may this controller
              // be declared losing.
              const std::size_t env_id = intern_env (std::move (actual));
              ctrl.tried_env_ids.push_back (env_id);
              account_bytes (
                  reverse_dependency_bytes, sizeof (std::size_t));
              if (limit_exceeded)
                return false;
              assert (env_nodes[env_id].status != node_status::losing);
              ctrl.selected_env = env_id;
              ctrl.selected_action_index = action_index;
              env_nodes[env_id].selected_by.push_back (ctrl_id);
              account_bytes (
                  reverse_dependency_bytes, sizeof (std::size_t));
              ++edges_selected;
              return not limit_exceeded;
            }

            if (known_actual.has_value ()
                and not std::ranges::contains (
                    ctrl.tried_env_ids, *known_actual)) {
              ctrl.tried_env_ids.push_back (*known_actual);
              account_bytes (
                  reverse_dependency_bytes, sizeof (std::size_t));
            }
            assert (forward_actions_skipped
                    >= action_count - ctrl.next_action_index);
            forward_actions_skipped -=
                action_count - ctrl.next_action_index;
            ctrl.selected_action_index.reset ();
            ++choice_switches;
          }
#endif

          // The selected successor just acquired a losing proof.  Actions
          // after it are no longer skipped: the monotone cursor resumes at
          // exactly the next semantic action.
          if (ctrl.selected_env.has_value ()) {
            assert (env_nodes[*ctrl.selected_env].status == node_status::losing);
            assert (forward_actions_skipped
                    >= action_count - ctrl.next_action_index);
            forward_actions_skipped -= action_count - ctrl.next_action_index;
            ctrl.selected_env.reset ();
            ctrl.selected_action_index.reset ();
            ++choice_switches;
          }

          auto action = std::ranges::begin (actions);
          std::ranges::advance (action, ctrl.next_action_index);
          while (ctrl.next_action_index < action_count) {
            const std::size_t action_index = ctrl.next_action_index++;
            state successor = actioner.apply (
                env_nodes[ctrl.parent_env].rank, *action,
                actioners::direction::forward);
            ++action;
            ++raw_actions;

            if (not is_safe (successor)) {
              ++choice_switches;
              continue;
            }

            const auto known_env = find_env (successor);
#if ACACIA_FORWARD_CONDITIONAL_COVERING
            if (known_env.has_value ()
                and env_nodes[*known_env].status == node_status::losing) {
              if (not std::ranges::contains (
                      ctrl.tried_env_ids, *known_env)) {
                ctrl.tried_env_ids.push_back (*known_env);
                account_bytes (
                    reverse_dependency_bytes, sizeof (std::size_t));
              }
              ++choice_switches;
              continue;
            }
#endif
            if (known_env.has_value ()
                and std::ranges::contains (ctrl.tried_env_ids, *known_env)) {
              ++choice_switches;
              continue;
            }

            if (use_losing_antichain
                and losing_antichain.subsumes (successor)) {
              [[maybe_unused]] const auto generator =
                  subsuming_generator (successor);
              assert (generator.has_value ());
              ++choice_switches;
              continue;
            }

            ++distinct_successors;
            ++minimal_successors;
            if (not add_represented_edge ())
              return false;

#if ACACIA_FORWARD_CONDITIONAL_COVERING
            const auto cover = find_cover (successor);
            if (cover.has_value ()) {
              ctrl.covered_actual_successor.emplace (std::move (successor));
              account_rank (*ctrl.covered_actual_successor);
              ctrl.cover_env = *cover;
              ctrl.selected_env = *cover;
              ctrl.selected_action_index = action_index;
              forward_actions_skipped +=
                  action_count - ctrl.next_action_index;
              env_nodes[*cover].covered_by.push_back (ctrl_id);
              account_bytes (
                  reverse_dependency_bytes, sizeof (std::size_t));
              ++forward_covers_created;
              ++edges_selected;
              return not limit_exceeded;
            }
#endif

            const std::size_t env_id = intern_env (std::move (successor));
            ctrl.tried_env_ids.push_back (env_id);
            account_bytes (
                reverse_dependency_bytes, sizeof (std::size_t));
            if (limit_exceeded)
              return false;

            if (env_nodes[env_id].status == node_status::losing) {
              ++choice_switches;
              continue;
            }

            ctrl.selected_env = env_id;
            ctrl.selected_action_index = action_index;
            forward_actions_skipped += action_count - ctrl.next_action_index;
            env_nodes[env_id].selected_by.push_back (ctrl_id);
            account_bytes (
                reverse_dependency_bytes, sizeof (std::size_t));
            ++edges_selected;
            return not limit_exceeded;
          }

          std::vector<std::size_t> dependencies;
          for (const std::size_t env_id : ctrl.tried_env_ids)
            if (env_nodes[env_id].status == node_status::losing
                and not std::ranges::contains (
                    dependencies, env_nodes[env_id].losing_proof_id))
              dependencies.push_back (env_nodes[env_id].losing_proof_id);
          mark_controller_losing (ctrl_id, std::move (dependencies));
          return not limit_exceeded;
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
              if (limit_exceeded)
                return false;
              continue;
            }

            // Advancing a controller can intern a node and reallocate
            // env_nodes, so detach this list before following its entries.
            std::vector<std::size_t> selected_by =
                std::move (env_nodes[losing.id].selected_by);
            release_items (reverse_dependency_bytes, selected_by.size (),
                           sizeof (std::size_t));
#if ACACIA_FORWARD_CONDITIONAL_COVERING
            std::vector<std::size_t> covered_by =
                std::move (env_nodes[losing.id].covered_by);
            release_items (reverse_dependency_bytes, covered_by.size (),
                           sizeof (std::size_t));
#endif
            for (const std::size_t ctrl_id : selected_by) {
              auto& ctrl = ctrl_nodes[ctrl_id];
              if (ctrl.selected_env != losing.id
                  or env_nodes[ctrl.parent_env].status == node_status::losing)
                continue;
              if (not advance_controller (ctrl_id))
                return false;
            }
#if ACACIA_FORWARD_CONDITIONAL_COVERING
            for (const std::size_t ctrl_id : covered_by) {
              auto& ctrl = ctrl_nodes[ctrl_id];
              if (ctrl.cover_env != losing.id
                  or env_nodes[ctrl.parent_env].status == node_status::losing)
                continue;
              if (not advance_controller (ctrl_id))
                return false;
            }
#endif
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
          result.nodes_checked = nodes_checked;
          result.nodes_invalidated = nodes_invalidated;
          result.raw_actions = raw_actions;
          result.forward_actions_skipped = forward_actions_skipped;
#if ACACIA_FORWARD_CONDITIONAL_COVERING
          result.forward_covers_created = forward_covers_created;
          result.forward_covers_resolved = forward_covers_resolved;
          result.forward_cover_search_visits = forward_cover_search_visits;
#endif
          result.distinct_successors = distinct_successors;
          result.minimal_successors = minimal_successors;
          result.rank_bytes = rank_bytes;
          result.environment_node_bytes = environment_node_bytes;
          result.controller_node_bytes = controller_node_bytes;
          result.reverse_dependency_bytes = reverse_dependency_bytes;
          result.proof_bytes = proof_bytes;
          result.hash_index_bytes = hash_index_bytes;
          result.losing_antichain_bytes = losing_antichain_bytes;
          result.node_bytes = saturated_sum (
              {environment_node_bytes, controller_node_bytes});
          result.index_bytes = saturated_sum (
              {reverse_dependency_bytes, proof_bytes, hash_index_bytes,
               losing_antichain_bytes});
          result.total_bytes = total_bytes;
          assert (total_bytes == std::numeric_limits<std::size_t>::max ()
                  or saturated_sum (
                         {result.rank_bytes, result.node_bytes,
                          result.index_bytes})
                         == total_bytes);
          result.minimisation_ms = minimisation_ms;
          result.losing_proofs = losing_proofs;
          result.env_ranks.reserve (env_nodes.size ());
          for (const auto& node : env_nodes)
            result.env_ranks.push_back (node.rank.copy ());
          result.ctrl_parents.reserve (ctrl_nodes.size ());
          for (const auto& node : ctrl_nodes)
            result.ctrl_parents.emplace_back (node.parent_env, node.input_index);
          result.losing_antichain_ranks.reserve (
              losing_antichain_generators.size ());
          for (const std::size_t generator : losing_antichain_generators)
            result.losing_antichain_ranks.push_back (
                env_nodes[generator].rank.copy ());
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
  /// Lazy choice is complete because every controller scans its finite action
  /// list monotonically: a choice is discarded only after it is unsafe, its
  /// exact successor has already been tried, or its successor has a losing
  /// proof.  If the queues empty, every reachable environment has every input
  /// expanded and every such controller still selects a safe, non-losing
  /// successor.
  ///
  /// A resource limit only interrupts that proof process.  It is deliberately
  /// returned as `resource_limit`, never converted into either game verdict.
  template <typename SetOfStates,
            bool EagerMinimalSuccessors =
                (ACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS != 0),
            typename InputOutputFwdActions, typename Actioner>
  forward_solve_result<typename SetOfStates::value_type>
  solve_forward_reachable_safety (
      const typename SetOfStates::value_type& initial,
      const typename SetOfStates::value_type& safe,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      const forward_limits& limits = {}, bool use_losing_antichain = false,
      std::size_t controller_minimisation_threshold =
          default_controller_minimisation_threshold) {
    forward_reachable_detail::forward_search<
        SetOfStates, InputOutputFwdActions, Actioner, EagerMinimalSuccessors>
        search {initial, safe, input_output_fwd_actions, actioner, limits,
                use_losing_antichain, controller_minimisation_threshold};
    return search.solve ();
  }

  /// Spelling parallel to the F0 oracle for callers that name the game.
  template <typename SetOfStates,
            bool EagerMinimalSuccessors =
                (ACACIA_FORWARD_EAGER_MINIMAL_SUCCESSORS != 0),
            typename InputOutputFwdActions, typename Actioner>
  forward_solve_result<typename SetOfStates::value_type>
  solve_forward_reachable_safety_game (
      const typename SetOfStates::value_type& initial,
      const typename SetOfStates::value_type& safe,
      const InputOutputFwdActions& input_output_fwd_actions, Actioner& actioner,
      const forward_limits& limits = {}, bool use_losing_antichain = false,
      std::size_t controller_minimisation_threshold =
          default_controller_minimisation_threshold) {
    return solve_forward_reachable_safety<SetOfStates, EagerMinimalSuccessors> (
        initial, safe, input_output_fwd_actions, actioner, limits,
        use_losing_antichain, controller_minimisation_threshold);
  }

}  // namespace acacia::solver_detail
