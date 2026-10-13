#pragma once

#include "phase_records.hh"
#include <string>
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

  class legacy_query_progress {
      bool enabled_ = phase_records_enabled ();
      uint64_t stage_id_ = 0, completed_ = 0;

    public:
      template <typename Rank>
      void completed (const Rank& rank) {
        if (!enabled_)
          return;
        auto* worker = active_worker_record ();
        if (!worker || strcmp (worker->observed_stage, "search") != 0)
          return;
        if (stage_id_ != worker->stage_id) {
          stage_id_ = worker->stage_id;
          completed_ = 0;
        }
        // Global sampling rule: first four, then powers of two; at most 65
        // snapshots per stage. No rank scan, clock or counter with observation off.
        if (completed_ == UINT64_MAX)
          return;
        ++completed_;
        if (completed_ > 4 && (completed_ & (completed_ - 1)) != 0)
          return;
        const auto now = phase_clock (CLOCK_MONOTONIC);
        if (completed_ == 1)
          legacy_count ("first_useful_action_query_ns", now);
        legacy_count ("query_snapshot_ns", now);
        legacy_count ("completed_action_applications", completed_);
        size_t support = 0;
        long long maximum = -1;
        for (size_t q = 0; q < rank.size (); ++q)
          if (rank[q] != -1) {
            ++support;
            maximum = std::max (maximum, static_cast<long long> (rank[q]));
          }
        legacy_count ("query_rank_support", support);
        legacy_metric ("query_rank_maximum", std::to_string (maximum).c_str ());
      }
  };

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
        if constexpr (requires { action.payload_bytes (); })
          bytes += action.payload_bytes ();
        else {
          bytes +=
              sizeof (action) +
              action.capacity () * sizeof (typename std::decay_t<decltype (action)>::value_type);
          for (const auto& endpoints : action)
            bytes += endpoints.capacity () *
                     sizeof (typename std::decay_t<decltype (endpoints)>::value_type);
        }
      }
    }
    return bytes;
  }
}
