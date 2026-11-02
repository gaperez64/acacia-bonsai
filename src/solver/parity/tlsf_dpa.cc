#include "solver/parity/tlsf_dpa.hh"

#include "solver/bdd_config.hh"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <limits>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/synthesis.hh>
#include <stdexcept>

namespace acacia::tlsf_dpa {
  namespace {
    struct stopped {
        TlsfDpaStatus status;
    };
    class allowance {
      public:
        allowance (const limits& budget, size_t& bytes) : budget (budget), bytes (bytes) {}
        void poll () const {
          const auto& b = budget.construction;
          if ((b.cancel && *b.cancel) || (b.cancelled && b.cancelled (b.cancel_ctx)))
            throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_CANCELLED}};
          if (b.deadline_mono_ns) {
            timespec now;
            if (clock_gettime (CLOCK_MONOTONIC, &now) != 0 ||
                uint64_t (now.tv_sec) * 1000000000ULL + now.tv_nsec >= b.deadline_mono_ns)
              throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_DEADLINE}};
          }
        }
        void charge (size_t amount) {
          poll ();
          if (amount > std::numeric_limits<size_t>::max () - bytes ||
              (budget.import_bytes &&
               (bytes > budget.import_bytes || amount > budget.import_bytes - bytes)))
            throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT}};
          bytes += amount;
        }
        template <typename T>
        void push (std::vector<T>& items, T value) {
          if (items.size () == items.capacity ()) {
            if (items.capacity () > items.max_size () / 2)
              throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT}};
            const size_t capacity = std::max<size_t> (1, items.capacity () * 2);
            charge (capacity * sizeof (T));
            items.reserve (capacity);
          }
          items.push_back (value);
        }
        // Tree node links, allocator overhead, and value storage. Charges are
        // cumulative: scratch/capacity releases never replenish the allowance.
        template <typename Map>
        void entry () {
          charge (sizeof (typename Map::value_type) + 6 * sizeof (void*));
        }

      private:
        const limits& budget;
        size_t& bytes;
    };

    class labels {
      public:
        labels (TlsfDpa& source, const spot::twa_graph_ptr& graph, allowance& space, size_t& count)
          : source (source),
            space (space),
            count (count) {
          for (uint32_t ap = 0; ap < tlsf_dpa_ap_count (&source); ++ap) {
            space.poll ();
            const char* name = tlsf_dpa_ap_name (&source, ap);
            if (!name)
              throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
            // Dictionary/formula/AP-vector storage, including growth headroom.
            space.charge (8 * (sizeof (spot::formula) + sizeof (void*) + strlen (name) + 1));
            space.push (variables, graph->register_ap (spot::formula::ap (name)));
          }
        }
        bdd rebuild (TlsfDpaLabel root) {
          if (auto found = memo.find (root); found != memo.end ())
            return found->second;
          pending.clear ();
          space.push (pending, root);
          while (!pending.empty ()) {
            space.poll ();
            const auto id = pending.back ();
            if (memo.contains (id)) {
              pending.pop_back ();
              continue;
            }
            const auto* node = tlsf_dpa_label_node (&source, id);
            if (!node)
              throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
            bdd value;
            if (node->kind == TLSF_DPA_FALSE)
              value = bddfalse;
            else if (node->kind == TLSF_DPA_TRUE)
              value = bddtrue;
            else {
              if (node->kind != TLSF_DPA_DECISION || node->ap >= variables.size ())
                throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
              if (!memo.contains (node->low)) {
                space.push (pending, node->low);
                continue;
              }
              if (!memo.contains (node->high)) {
                space.push (pending, node->high);
                continue;
              }
              value = bdd_ite (bdd_ithvar (variables[node->ap]), memo.at (node->high),
                               memo.at (node->low));
              space.poll ();
            }
            space.entry<decltype (memo)> ();
            memo.emplace (id, value);
            ++count;
            pending.pop_back ();
          }
          return memo.at (root);
        }

      private:
        TlsfDpa& source;
        allowance& space;
        size_t& count;
        std::vector<int> variables;
        std::map<TlsfDpaLabel, bdd> memo;
        std::vector<TlsfDpaLabel> pending;
    };
  }

  context create (const TlsfAstNode* root, const std::vector<std::string>& inputs,
                  const std::vector<std::string>& outputs, const limits& budget,
                  TlsfDpaStatus& status) {
    std::vector<const char*> in, out;
    size_t bytes = 0;
    allowance space (budget, bytes);
    try {
      for (const auto& name : inputs)
        space.push (in, name.c_str ());
      for (const auto& name : outputs)
        space.push (out, name.c_str ());
      space.poll ();
    } catch (const stopped& failure) {
      status = failure.status;
      return {nullptr, tlsf_dpa_free};
    }
    if (in.size () > UINT32_MAX || out.size () > UINT32_MAX) {
      status = {TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT};
      return {nullptr, tlsf_dpa_free};
    }
    TlsfDpa* raw = nullptr;
    status = tlsf_dpa_create (root, in.data (), uint32_t (in.size ()), out.data (),
                              uint32_t (out.size ()), &budget.construction, &raw);
    return {raw, tlsf_dpa_free};
  }

  result import (TlsfDpa& source, const limits& budget, spot::bdd_dict_ptr dictionary) {
    result answer;
    allowance space (budget, answer.import_bytes);
    try {
      answer.stage = "import";
      space.poll ();
      if (auto status = tlsf_dpa_status (&source); status.code != TLSF_DPA_OK)
        throw stopped {status};
      const auto priority = tlsf_dpa_max_priority (&source);
      space.charge (sizeof (spot::twa_graph));
      if constexpr (ACACIA_BDD_MAX_INCREASE > 0)
        configure_bdd ();
      auto graph = spot::make_twa_graph (dictionary);
      std::vector<uint32_t> edge_priorities;
      std::map<uint32_t, unsigned> priorities;
      labels bridge (source, graph, space, answer.labels);
      std::vector<TlsfDpaState> queue;
      const auto intern = [&] (TlsfDpaState id) {
        if (auto found = answer.states.find (id); found != answer.states.end ())
          return found->second;
        if ((budget.construction.max_states &&
             answer.states.size () >= budget.construction.max_states) ||
            answer.states.size () >= std::numeric_limits<unsigned>::max ())
          throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_STATE_LIMIT}};
        space.entry<decltype (answer.states)> ();
        // Spot vectors can double and temporarily retain the old allocation.
        space.charge (4 * sizeof (spot::twa_graph::graph_t::state_storage_t));
        space.push (queue, id);
        const unsigned state = graph->new_state ();
        answer.states.emplace (id, state);
        return state;
      };
      graph->set_init_state (intern (tlsf_dpa_initial (&source)));
      for (size_t next = 0; next < queue.size (); ++next) {
        space.poll ();
        answer.stage = "expand";
        const TlsfDpaEdge* edges = nullptr;
        size_t count = 0;
        const auto status = tlsf_dpa_successors (&source, queue[next], &edges, &count);
        if (status.code != TLSF_DPA_OK)
          throw stopped {status};
        if (!edges || !count)
          throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
        answer.stage = "import";
        const auto src = answer.states.at (queue[next]);
        bdd covered = bddfalse;
        for (size_t e = 0; e < count; ++e) {
          space.poll ();
          if (edges[e].priority > priority)
            throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_PRIORITY_LIMIT}};
          const auto guard = bridge.rebuild (edges[e].label);
          if (guard == bddfalse || (covered & guard) != bddfalse)
            throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
          covered |= guard;
          const auto dst = intern (edges[e].successor);
          space.charge (4 * sizeof (graph->edge_storage (0u)));
          graph->new_edge (src, dst, guard);
          space.push (edge_priorities, edges[e].priority);
          if (!priorities.contains (edges[e].priority)) {
            space.entry<decltype (priorities)> ();
            priorities.emplace (edges[e].priority, 0);
          }
          ++answer.edges;
          space.poll ();
        }
        if (covered != bddtrue)
          throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
      }
      space.poll ();
      // The API bound is component count + padding, even before the first row.
      // Collapse adjacent used priorities of equal parity. This monotone map
      // preserves parity, so max(map(Inf(run))) = map(max(Inf(run))) is odd
      // exactly when the original maximum is odd.
      unsigned compressed = priorities.begin ()->first & 1u;
      for (auto& [raw, mapped] : priorities) {
        space.poll ();
        compressed += (raw & 1u) != (compressed & 1u);
        mapped = compressed;
      }
      if (compressed >= spot::acc_cond::mark_t::max_accsets ())
        throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_PRIORITY_LIMIT}};
      space.charge ((compressed + 1) * sizeof (spot::acc_cond::acc_code));
      graph->set_acceptance (compressed + 1,
                             spot::acc_cond::acc_code::parity (true, true, compressed + 1));
      size_t edge = 0;
      for (auto& transition : graph->edges ()) {
        space.poll ();
        transition.acc = {priorities.at (edge_priorities[edge++])};
      }
      graph->prop_universal (true);
      graph->prop_complete (true);
      answer.automaton = std::move (graph);
    } catch (const stopped& failure) {
      answer.status = failure.status;
    } catch (const std::bad_alloc&) {
      answer.status = {TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT};
    } catch (const std::length_error&) {
      answer.status = {TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT};
    }
    return answer;
  }

  lazy_result solve_lazy (TlsfDpa& source, const limits& budget,
                          const std::vector<std::string>& outputs, bool target_real,
                          std::function<void (const lazy_result&)> on_snapshot,
                          bool strategy_guided) {
    lazy_result answer;
    allowance space (budget, answer.import_bytes);
    try {
      const auto check = [&] {
        space.poll ();
        if (auto status = tlsf_dpa_status (&source); status.code != TLSF_DPA_OK)
          throw stopped {status};
      };
      check ();
      if constexpr (ACACIA_BDD_MAX_INCREASE > 0)
        configure_bdd ();
      space.charge (sizeof (spot::twa_graph));
      auto graph = spot::make_twa_graph (spot::make_bdd_dict ());
      labels bridge (source, graph, space, answer.labels);
      bdd output_cube = bddtrue;
      for (const auto& name : outputs) {
        space.poll ();
        output_cube &= bdd_ithvar (graph->register_ap (spot::formula::ap (name)));
      }
      const auto bound = tlsf_dpa_max_priority (&source);
      std::map<TlsfDpaState, unsigned> states;
      std::vector<TlsfDpaState> queue;
      std::vector<uint8_t> expanded;
      std::vector<unsigned> preferred;
      size_t fallback = 0;
      // Raw priorities never change. Every snapshot is rebuilt and compressed
      // independently, including all old edges and the fixed frontier priority.
      std::vector<uint32_t> raw_priorities;
      const auto intern = [&] (TlsfDpaState id) {
        if (auto found = states.find (id); found != states.end ())
          return found->second;
        if ((budget.construction.max_states && states.size () >= budget.construction.max_states) ||
            states.size () >= std::numeric_limits<unsigned>::max ())
          throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_STATE_LIMIT}};
        space.entry<decltype (states)> ();
        space.charge (4 * sizeof (spot::twa_graph::graph_t::state_storage_t));
        space.push (queue, id);
        space.push (expanded, uint8_t (0));
        const auto state = graph->new_state ();
        states.emplace (id, state);
        answer.states = states.size ();
        return state;
      };
      graph->set_init_state (intern (tlsf_dpa_initial (&source)));
      size_t next_solve = 1;
      for (;;) {
        while (answer.expanded < queue.size () && answer.expanded < next_solve) {
          check ();
          while (!preferred.empty () && expanded[preferred.back ()])
            preferred.pop_back ();
          while (fallback < expanded.size () && expanded[fallback])
            ++fallback;
          const auto src = preferred.empty () ? unsigned (fallback) : preferred.back ();
          if (!preferred.empty ())
            preferred.pop_back ();
          answer.stage = "expand";
          const TlsfDpaEdge* edges = nullptr;
          size_t count = 0;
          const auto status = tlsf_dpa_successors (&source, queue[src], &edges, &count);
          if (status.code != TLSF_DPA_OK)
            throw stopped {status};
          if (!edges || !count)
            throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
          answer.stage = "import";
          bdd covered = bddfalse;
          for (size_t e = 0; e < count; ++e) {
            space.poll ();
            if (edges[e].priority > bound)
              throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_PRIORITY_LIMIT}};
            const auto guard = bridge.rebuild (edges[e].label);
            if (guard == bddfalse || (covered & guard) != bddfalse)
              throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
            covered |= guard;
            const auto dst = intern (edges[e].successor);
            space.charge (4 * sizeof (graph->edge_storage (0u)));
            graph->new_edge (src, dst, guard);
            space.push (raw_priorities, edges[e].priority);
            ++answer.edges;
          }
          if (covered != bddtrue)
            throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
          check ();
          // Publication boundary: all outgoing labels, targets and priorities
          // exist and the complete row has passed validation.
          expanded[src] = 1;
          ++answer.expanded;
        }
        check ();
        answer.stage = "snapshot";
        const uint32_t frontier = target_real ? 0 : 1;
        std::map<uint32_t, unsigned> priorities;
        const auto remember = [&] (uint32_t raw) {
          if (!priorities.contains (raw)) {
            space.entry<decltype (priorities)> ();
            priorities.emplace (raw, 0);
          }
        };
        for (const auto raw : raw_priorities) {
          space.poll ();
          remember (raw);
        }
        if (answer.expanded < queue.size ())
          remember (frontier);
        unsigned compressed = priorities.begin ()->first & 1u;
        for (auto& [raw, mapped] : priorities) {
          space.poll ();
          compressed += (raw & 1u) != (compressed & 1u);
          mapped = compressed;
        }
        if (compressed >= spot::acc_cond::mark_t::max_accsets ())
          throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_PRIORITY_LIMIT}};
        space.charge (sizeof (spot::twa_graph) +
                      4 * size_t (graph->num_states ()) *
                          sizeof (spot::twa_graph::graph_t::state_storage_t));
        auto snapshot = spot::make_twa_graph (graph->get_dict ());
        snapshot->copy_ap_of (graph);
        snapshot->new_states (graph->num_states ());
        snapshot->set_init_state (graph->get_init_state_number ());
        space.charge ((compressed + 1) * sizeof (spot::acc_cond::acc_code));
        snapshot->set_acceptance (compressed + 1,
                                  spot::acc_cond::acc_code::parity (true, true, compressed + 1));
        size_t edge = 0;
        for (const auto& e : graph->edges ()) {
          space.charge (4 * sizeof (graph->edge_storage (0u)));
          snapshot->new_edge (e.src, e.dst, e.cond, {priorities.at (raw_priorities[edge++])});
        }
        for (unsigned state = 0; state < queue.size (); ++state)
          if (!expanded[state]) {
            space.charge (4 * sizeof (graph->edge_storage (0u)));
            snapshot->new_edge (state, state, bddtrue, {priorities.at (frontier)});
          }
        snapshot->prop_universal (true);
        snapshot->prop_complete (true);
        check ();
        answer.stage = "split";
        auto arena = spot::split_2step (snapshot, output_cube, true);
        // Splitter/solver and BDD internals are opaque; worker AS/RSS limits
        // cover their allocations. Charge the resulting arena and solver
        // storage estimates to the cumulative import allowance before solving.
        space.charge (sizeof (spot::twa_graph) +
                      4 * size_t (arena->num_states ()) *
                          sizeof (spot::twa_graph::graph_t::state_storage_t) +
                      4 * size_t (arena->num_edges ()) * sizeof (arena->edge_storage (0u)) +
                      64 * size_t (arena->num_states ()) + 16 * size_t (arena->num_edges ()));
        check ();
        answer.stage = "solve";
        const bool real = spot::solve_parity_game (arena, true);
        ++answer.solves;
        check ();
        if (on_snapshot)
          on_snapshot (answer);
        check ();
        if (real == target_real) {
          answer.decided = true;
          return answer;
        }
        if (answer.expanded == queue.size ())
          return answer;
        if (strategy_guided) {
          answer.stage = "snapshot";
          preferred.clear ();
          const auto& owners = spot::get_state_players (arena);
          const auto& winners = spot::get_state_winners (arena);
          const auto& strategy = spot::get_strategy (arena);
          const auto count = arena->num_states ();
          if (owners.size () == count && winners.size () == count && strategy.size () == count &&
              arena->get_init_state_number () < count) {
            space.charge (count);
            std::vector<uint8_t> seen (count, 0);
            std::vector<unsigned> todo;
            space.push (todo, arena->get_init_state_number ());
            seen[todo.front ()] = 1;
            bool valid = true;
            for (size_t next = 0; next < todo.size () && valid; ++next) {
              check ();
              const auto v = todo[next];
              if (winners[v] != real) {
                valid = false;
                break;
              }
              if (v < expanded.size () && !expanded[v])
                continue;
              bool selected = owners[v] != real;
              for (const auto& edge : arena->out (v)) {
                check ();
                // Only the current initial WINNER has a usable strategy.
                // Opponent-owned choices remain universal; never read their slot.
                if (owners[v] == real && arena->edge_number (edge) != strategy[v])
                  continue;
                selected = true;
                if (edge.dst >= count || winners[edge.dst] != real) {
                  valid = false;
                  break;
                }
                if (!seen[edge.dst]) {
                  seen[edge.dst] = 1;
                  space.push (todo, edge.dst);
                }
              }
              valid &= selected;
            }
            // Split preserves original state IDs. Use newest reachable frontier
            // first, retaining only graph IDs and no snapshot/strategy state.
            if (valid)
              for (unsigned v = 0; v < expanded.size (); ++v) {
                check ();
                if (!expanded[v] && v < seen.size () && seen[v])
                  space.push (preferred, v);
              }
          }
          check ();
        }
        if (next_solve > std::numeric_limits<size_t>::max () / 2)
          throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT}};
        next_solve *= 2;
      }
    } catch (const stopped& failure) {
      answer.status = failure.status;
    } catch (const std::bad_alloc&) {
      answer.status = {TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT};
    } catch (const std::length_error&) {
      answer.status = {TLSF_DPA_DECLINED, TLSF_DPA_HOST_LIMIT};
    }
    return answer;
  }

  const char* reason (TlsfDpaStatus status) {
    switch (status.reason) {
      case TLSF_DPA_UNSUPPORTED: return "dpa_unsupported";
      case TLSF_DPA_STATE_LIMIT: return "dpa_state_limit";
      case TLSF_DPA_HOST_LIMIT: return "dpa_host_limit";
      case TLSF_DPA_BDD_LIMIT: return "dpa_bdd_limit";
      case TLSF_DPA_DEADLINE: return "dpa_deadline";
      case TLSF_DPA_CANCELLED: return "dpa_cancelled";
      case TLSF_DPA_PRIORITY_LIMIT: return "dpa_priority_limit";
      default: return status.code == TLSF_DPA_OK ? "none" : "dpa_invalid";
    }
  }
}
