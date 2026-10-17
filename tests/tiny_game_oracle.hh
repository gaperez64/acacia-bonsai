#pragma once



/// Independent test reference for Acacia's rank semantics.
///
/// The forward and backward maps reproduce the solver's rank updates using
/// explicit vectors. Tests compare them with `actioners::standard::apply` and
/// use the complete forward game below as an independent correctness oracle.

#include "configuration.hh"

#include <algorithm>
#include <vector>

#include <posets/utils/vector_mm.hh>

namespace acacia::research {

  using rank_vector = posets::utils::vector_mm<VECTOR_ELT_T>;

  /// `avec[i]` holds the pairs `(j, increment)` relating coordinate `i` to
  /// coordinate `j`, which is how `actioners::standard` stores an action.
  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;

  /// The deterministic forward rank update, `tau`.
  ///
  /// `avec` is indexed by destination and holds sources, so this is: a state is
  /// present in the successor iff some predecessor was present, and its counter
  /// is the largest predecessor counter plus one on an accepting step,
  /// saturating at K.  -1 means absent and does not propagate.
  inline rank_vector apply_forward (const rank_vector& m, const action_vec& avec,
                                    VECTOR_ELT_T K) {
    rank_vector out (m.size (), -1);
    for (size_t p = 0; p < m.size (); ++p)
      for (const auto& [q, p_final] : avec[p]) {
        if (m[q] != -1)
          out[p] = std::max (out[p],
                             std::min (K, (VECTOR_ELT_T) (m[q] + (VECTOR_ELT_T) (p_final ? 1 : 0))));
        if (out[p] == K)
          break;  // max cannot grow further; the solver short-circuits here too
      }
    return out;
  }

  /// The backward upper-corner transformer, seeded from the reset the solver
  /// uses: K-1 on a counting coordinate, 0 on a Boolean one.
  inline rank_vector apply_backward (const rank_vector& m, const action_vec& avec,
                                     VECTOR_ELT_T K, size_t bool_threshold) {
    rank_vector out (m.size (), 0);
    const size_t counting = std::min (bool_threshold, m.size ());
    std::fill_n (out.begin (), counting, (VECTOR_ELT_T) (K - 1));
    std::fill (out.begin () + static_cast<std::ptrdiff_t> (counting), out.end (),
               (VECTOR_ELT_T) 0);

    for (size_t p = 0; p < m.size (); ++p)
      for (const auto& [q, p_final] : avec[p])
        if (out[q] != -1)
          out[q] = std::min (
              out[q], std::max ((VECTOR_ELT_T) -1,
                                (VECTOR_ELT_T) (m[p] - (VECTOR_ELT_T) (p_final ? 1 : 0))));
    return out;
  }

  /// The safe vector: K-1 on counting coordinates, 0 on Boolean ones.  A
  /// candidate generator introduced by a search must be below it; for a region
  /// the solver produced this holds by construction, since it starts there.
  inline rank_vector safe_vector (size_t states, VECTOR_ELT_T K, size_t bool_threshold) {
    rank_vector safe (states, (VECTOR_ELT_T) (K - 1));
    for (size_t i = std::min (bool_threshold, states); i < states; ++i)
      safe[i] = 0;
    return safe;
  }

  /// The initial rank vector: absent everywhere but the automaton's initial
  /// state, which starts at counter zero.
  inline rank_vector initial_vector (size_t states, size_t init_state) {
    rank_vector v (states, -1);
    v[init_state] = 0;
    return v;
  }

  inline bool leq (const rank_vector& a, const rank_vector& b) {
    for (size_t i = 0; i < a.size (); ++i)
      if (a[i] > b[i])
        return false;
    return true;
  }

}  // namespace acacia::research



/// The deliberately explicit F0 safety-game oracle.
///
/// This constructs the complete reachable bipartite AND/OR graph at one bound
/// and only then solves it retrogradely.  It is a correctness reference for
/// later lazy algorithms, so rank vectors are interned exactly and no input or
/// action is pruned by an order-based heuristic.



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


#include <cstddef>
#include <list>
#include <random>
#include <utility>
#include <vector>

#include <posets/downsets.hh>
#include <posets/vectors.hh>

namespace acacia::testing {

  using state = posets::vectors::simd_vector_backed<VECTOR_ELT_T>;
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
  /// Forward images use the independent rank-action arithmetic above, whose implementation
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
