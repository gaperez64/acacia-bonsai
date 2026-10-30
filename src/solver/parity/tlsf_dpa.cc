#include "solver/parity/tlsf_dpa.hh"

#include "solver/bdd_config.hh"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <limits>
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
      if (priority >= spot::acc_cond::mark_t::max_accsets ())
        throw stopped {{TLSF_DPA_DECLINED, TLSF_DPA_PRIORITY_LIMIT}};
      space.charge (sizeof (spot::twa_graph) + (priority + 1) * sizeof (spot::acc_cond::acc_code));
      if constexpr (ACACIA_BDD_MAX_INCREASE > 0)
        configure_bdd ();
      auto graph = spot::make_twa_graph (dictionary);
      graph->set_acceptance (priority + 1,
                             spot::acc_cond::acc_code::parity (true, true, priority + 1));
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
          graph->new_edge (src, dst, guard, {edges[e].priority});
          ++answer.edges;
          space.poll ();
        }
        if (covered != bddtrue)
          throw stopped {{TLSF_DPA_INVALID, TLSF_DPA_NONE}};
      }
      space.poll ();
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
