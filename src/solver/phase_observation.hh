#pragma once

#include "phase_records.hh"
#include <type_traits>

#include <bddx.h>
#include <spot/twaalgos/sccinfo.hh>

namespace acacia {
  inline void legacy_graph (const spot::const_twa_graph_ptr& aut, const char* prefix = "") {
    if (!phase_records_enabled () || !aut)
      return;
    const auto count = [prefix] (const char* key, size_t value) {
      char name[96];
      snprintf (name, sizeof name, "%s%s", prefix, key);
      legacy_count (name, value);
    };
    count ("states", aut->num_states ());
    count ("edges", aut->num_edges ());
    count ("acceptance_sets", aut->num_sets ());
    if (aut->num_states ()) {
      const spot::scc_info scc (aut, spot::scc_info_options::NONE);
      count ("sccs", scc.scc_count ());
    }
    else
      count ("sccs", 0);
  }

  class legacy_bdd_gc {
      inline static legacy_bdd_gc* active_ = nullptr;
      bddgbchandler previous_ = nullptr;
      legacy_bdd_gc* outer_ = nullptr;
      uint64_t started_ = 0, elapsed_ = 0;
      size_t count_ = 0;
      bool enabled_ = false;
      static void observe (int before, bddGbcStat* stats) {
        auto* self = active_;
        if (!self)
          return;
        if (before)
          self->started_ = phase_clock (CLOCK_MONOTONIC);
        else {
          ++self->count_;
          if (self->started_)
            self->elapsed_ += phase_clock (CLOCK_MONOTONIC) - self->started_;
        }
        if (self->previous_)
          self->previous_ (before, stats);
      }

    public:
      legacy_bdd_gc () : enabled_ (phase_records_enabled ()) {
        if (enabled_) {
          outer_ = active_;
          active_ = this;
          previous_ = bdd_gbc_hook (observe);
        }
      }
      ~legacy_bdd_gc () {
        if (enabled_) {
          bdd_gbc_hook (previous_);
          active_ = outer_;
        }
      }
      void publish () const {
        if (enabled_) {
          legacy_count ("bdd_gc_count", count_);
          legacy_count ("bdd_gc_wall_ns", elapsed_);
        }
      }
      void reset () {
        count_ = 0;
        elapsed_ = 0;
      }
  };

  template <typename Table>
  inline size_t legacy_action_bytes (const Table& table) {
    size_t bytes = sizeof (Table);
    for (const auto& [input, actions] : table) {
      (void) input;
      bytes += sizeof (typename Table::value_type);
      for (const auto& action : actions) {
        bytes +=
            sizeof (action) +
            action.capacity () * sizeof (typename std::decay_t<decltype (action)>::value_type);
        for (const auto& endpoints : action)
          bytes += endpoints.capacity () *
                   sizeof (typename std::decay_t<decltype (endpoints)>::value_type);
      }
    }
    return bytes;
  }
}
