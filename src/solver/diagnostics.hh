#pragma once

#include "configuration.hh"
#include "solver/symmetry.hh"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>

#ifndef ACACIA_ENABLE_DIAGNOSTICS
# define ACACIA_ENABLE_DIAGNOSTICS 0
#endif

namespace acacia::diagnostics {

#if ACACIA_ENABLE_DIAGNOSTICS

  using clock = std::chrono::steady_clock;

  inline bool enabled () {
    static const bool value = [] {
      const char* env = std::getenv ("ACACIA_DIAG");
      if (env == nullptr or *env == '\0')
        return false;
      std::string_view v {env};
      return v != "0" and v != "false" and v != "FALSE" and v != "off" and v != "OFF";
    } ();
    return value;
  }

  struct child_metrics {
      std::string instance = "-";
      std::string path = "unknown";
      std::string result = "unknown";
      std::string final_reason = "unknown";
      std::string translation_pref = "unknown";
      std::string syntactic_bypass = "not-run";
      std::string fast_class = "not-run";
      std::string fast_verdict = "fallback";
      std::string preprocessor = "unknown";
      std::string equivariant = "not-run";
      // sym_* covers every diagnostic recognition pass, including instances
      // the solver declines; eq_* is populated only when solving is attempted.
      std::string symmetry_families = "-";
      std::string symmetry_indices = "-";
      std::string symmetry_matrix = "-";
      std::string symmetry_subsets = "-";
      std::string symmetry_selected = "-";
      std::string symmetry_orbit_sizes = "-";
      std::string symmetry_blocks = "-";
      std::string symmetry_shared = "-";

      long long total_ms = 0;
      long long rsimp_ms = 0;
      long long syntactic_bypass_ms = 0;
      long long translation_ms = 0;
      long long fast_class_ms = 0;
      long long fast_solve_ms = 0;
      long long preproc_ms = 0;
      long long solve_ms = 0;
      double cpre_ms = 0.0;
      double picker_ms = 0.0;
      double apply_ms = 0.0;
      double downset_ms = 0.0;

      bool rsimp_changed = false;
      size_t aut_states = 0;
      size_t aut_edges = 0;
      size_t preproc_states_before = 0;
      size_t preproc_states_after = 0;
      size_t preproc_edges_before = 0;
      size_t preproc_edges_after = 0;
      size_t bool_threshold = 0;
      size_t bitset_threshold = 0;
      size_t max_f = 0;
      size_t max_f_size = 0;
      unsigned long long actions_seen = 0;
      unsigned long long meets_computed = 0;
      unsigned long long meet_batches = 0;
      int loops = 0;
      int k_attempts = 0;
      int last_k = -1;
      size_t equivariant_clients = 0;
      size_t equivariant_blocks = 0;
      size_t equivariant_orbits = 0;
      clock::time_point started = clock::now ();

      void observe_loop (size_t f_size, int k) {
        ++loops;
        max_f = std::max (max_f, f_size);
        max_f_size = std::max (max_f_size, f_size);
        if (last_k != k) {
          last_k = k;
          ++k_attempts;
        }
      }

      void refresh_total () {
        total_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                       clock::now () - started)
                       .count ();
      }
  };

  inline thread_local child_metrics* current_child = nullptr;
  inline thread_local child_metrics* active_cpre_metrics = nullptr;
  inline thread_local clock::time_point active_cpre_started {};

  inline child_metrics* current () {
    return enabled () ? current_child : nullptr;
  }

  inline std::string env_or_dash (const char* name) {
    const char* value = std::getenv (name);
    return value == nullptr or *value == '\0' ? "-" : value;
  }

  inline unsigned progress_loop_period () {
    static const unsigned value = [] {
      const char* env = std::getenv ("ACACIA_DIAG_PROGRESS_EVERY");
      if (env == nullptr or *env == '\0')
        return 128u;
      char* end = nullptr;
      unsigned long parsed = std::strtoul (env, &end, 10);
      if (end == env or *end != '\0')
        return 128u;
      if (parsed > std::numeric_limits<unsigned>::max ())
        return std::numeric_limits<unsigned>::max ();
      return static_cast<unsigned> (parsed);
    } ();
    return value;
  }

  inline void flush_active_cpre () {
    if (active_cpre_metrics == nullptr)
      return;
    const auto now = clock::now ();
    active_cpre_metrics->cpre_ms +=
        std::chrono::duration<double, std::milli> (now - active_cpre_started).count ();
    active_cpre_started = now;
  }

  inline void print (const child_metrics& m, std::string_view kind = "final",
                     std::string_view checkpoint = "final") {
    // Progress can be emitted from inside a CPre that never returns before
    // the wrapper terminates the child.  Charge the live interval before
    // serializing so those snapshots still account for fixed-point time.
    flush_active_cpre ();
    std::ostringstream line;
    line << "ACACIA_DIAG"
         << " pid=" << getpid ()
         << " diag_kind=" << kind
         << " checkpoint=" << checkpoint
         << " instance=" << m.instance
         << " path=" << m.path
         << " translation_pref=" << m.translation_pref
         << " rsimp_ms=" << m.rsimp_ms
         << " rsimp_changed=" << (m.rsimp_changed ? 1 : 0)
         << " syntactic_bypass=" << m.syntactic_bypass
         << " syntactic_bypass_ms=" << m.syntactic_bypass_ms
         << " translation_ms=" << m.translation_ms
         << " aut_states=" << m.aut_states
         << " aut_edges=" << m.aut_edges
         << " fast_class=" << m.fast_class
         << " fast_class_ms=" << m.fast_class_ms
         << " fast_solve_ms=" << m.fast_solve_ms
         << " fast_verdict=" << m.fast_verdict
         << " preproc=" << m.preprocessor
         << " preproc_ms=" << m.preproc_ms
         << " preproc_states_before=" << m.preproc_states_before
         << " preproc_states_after=" << m.preproc_states_after
         << " preproc_edges_before=" << m.preproc_edges_before
         << " preproc_edges_after=" << m.preproc_edges_after
         << " bool_threshold=" << m.bool_threshold
         << " bitset_threshold=" << m.bitset_threshold
         << " max_f=" << m.max_f
         << " max_f_size=" << m.max_f_size
         << " loops=" << m.loops
         << " k_attempts=" << m.k_attempts
         << " cpre_ms=" << m.cpre_ms
         << " picker_ms=" << m.picker_ms
         << " apply_ms=" << m.apply_ms
         << " downset_ms=" << m.downset_ms
         << " actions_seen=" << m.actions_seen
         << " meets_computed=" << m.meets_computed
         << " meet_batches=" << m.meet_batches
         << " equivariant=" << m.equivariant
         << " eq_clients=" << m.equivariant_clients
         << " eq_blocks=" << m.equivariant_blocks
         << " eq_orbits=" << m.equivariant_orbits
         << " sym_families=" << m.symmetry_families
         << " sym_indices=" << m.symmetry_indices
         << " sym_matrix=" << m.symmetry_matrix
         << " sym_subsets=" << m.symmetry_subsets
         << " sym_selected=" << m.symmetry_selected
         << " sym_orbit_sizes=" << m.symmetry_orbit_sizes
         << " sym_blocks=" << m.symmetry_blocks
         << " sym_shared=" << m.symmetry_shared
         << " solve_ms=" << m.solve_ms
         << " total_ms=" << m.total_ms
         << " result=" << m.result
         << " final_reason=" << m.final_reason
         << '\n';
    const std::string text = line.str ();
    [[maybe_unused]] const auto written = ::write (STDERR_FILENO, text.data (), text.size ());
  }

  class scoped_child {
    public:
      explicit scoped_child (std::string path)
        : previous {current_child},
          started {clock::now ()} {
        if (enabled ()) {
          metrics.instance = env_or_dash ("ACACIA_DIAG_INSTANCE");
          metrics.path = std::move (path);
          metrics.started = started;
          current_child = &metrics;
        }
      }

      ~scoped_child () {
        if (enabled () and current_child == &metrics) {
          metrics.refresh_total ();
          print (metrics);
          current_child = previous;
        }
      }

      child_metrics* operator-> () { return current (); }
      child_metrics& get () { return metrics; }

    private:
      child_metrics metrics;
      child_metrics* previous = nullptr;
      clock::time_point started;
  };

  class scoped_timer {
    public:
      explicit scoped_timer (long long* target)
        : target {enabled () ? target : nullptr},
          started {clock::now ()} {}

      ~scoped_timer () {
        if (target != nullptr)
          *target += std::chrono::duration_cast<std::chrono::milliseconds> (
                         clock::now () - started)
                         .count ();
      }

    private:
      long long* target = nullptr;
      clock::time_point started;
  };

  enum class fine_metric { cpre, picker, apply };

  class scoped_fine_timer {
    public:
      explicit scoped_fine_timer (fine_metric metric)
        : metrics {current ()}, metric {metric}, started {clock::now ()} {
        if (metrics != nullptr and metric == fine_metric::cpre) {
          active_cpre_metrics = metrics;
          active_cpre_started = started;
        }
      }

      ~scoped_fine_timer () {
        if (metrics == nullptr)
          return;
        if (metric == fine_metric::cpre) {
          flush_active_cpre ();
          active_cpre_metrics = nullptr;
          return;
        }
        const double elapsed =
            std::chrono::duration<double, std::milli> (clock::now () - started).count ();
        switch (metric) {
          case fine_metric::cpre:
            break;
          case fine_metric::picker:
            metrics->picker_ms += elapsed;
            break;
          case fine_metric::apply:
            metrics->apply_ms += elapsed;
            break;
        }
      }

    private:
      child_metrics* metrics;
      fine_metric metric;
      clock::time_point started;
  };

  // Time a downset operation while excluding any nested actioner.apply()
  // calls.  f.apply() interleaves both, so a plain nested timer would count
  // the letter work twice and bias the second-level phase split.
  class scoped_downset_timer {
    public:
      scoped_downset_timer ()
        : metrics {current ()},
          apply_before {metrics == nullptr ? 0.0 : metrics->apply_ms},
          started {clock::now ()} {}

      ~scoped_downset_timer () {
        if (metrics == nullptr)
          return;
        const double elapsed =
            std::chrono::duration<double, std::milli> (clock::now () - started).count ();
        const double nested_apply = metrics->apply_ms - apply_before;
        metrics->downset_ms += std::max (0.0, elapsed - nested_apply);
      }

    private:
      child_metrics* metrics;
      double apply_before;
      clock::time_point started;
  };

  inline void observe_action () {
    if (auto* m = current ())
      ++m->actions_seen;
  }

  inline void snapshot_action_progress () {
    if (auto* m = current ()) {
      const auto count = m->actions_seen;
      if (count <= 4 or (count & (count - 1)) == 0) {
        m->refresh_total ();
        print (*m, "progress", "cpre-after-action");
      }
    }
  }

  inline void observe_meets (size_t lhs, size_t rhs) {
    if (auto* m = current ()) {
      m->meets_computed += static_cast<unsigned long long> (lhs) * rhs;
      ++m->meet_batches;
    }
  }

  inline void snapshot_intersection_progress () {
    if (auto* m = current ()) {
      const auto count = m->meet_batches;
      if (count <= 4 or (count & (count - 1)) == 0) {
        m->refresh_total ();
        print (*m, "progress", "cpre-before-intersection");
      }
    }
  }

  inline void snapshot_loop_progress (std::string_view checkpoint) {
    if (auto* m = current ()) {
      const auto count = static_cast<unsigned> (m->loops);
      if (count <= 4 or (count & (count - 1)) == 0) {
        m->refresh_total ();
        print (*m, "progress", checkpoint);
      }
    }
  }

  inline void observe_loop (size_t f_size, int k) {
    if (auto* m = current ()) {
      m->observe_loop (f_size, k);
      const unsigned period = progress_loop_period ();
      if (period != 0 and static_cast<unsigned> (m->loops) % period == 0) {
        m->refresh_total ();
        print (*m, "progress", "solve-loop");
      }
    }
  }

  inline void snapshot (std::string_view checkpoint) {
    if (auto* m = current ()) {
      m->refresh_total ();
      print (*m, "progress", checkpoint);
    }
  }

  inline void set_final_reason (std::string reason) {
    if (auto* m = current ())
      m->final_reason = std::move (reason);
  }

  inline bool finish (bool solved, std::string reason) {
    if (auto* m = current ()) {
      m->result = solved ? "solved" : "unknown";
      if (m->final_reason == "unknown")
        m->final_reason = std::move (reason);
    }
    return solved;
  }

  inline void set_equivariant_decline (std::string reason) {
    if (auto* m = current ())
      m->equivariant = "declined:" + reason;
  }

  inline void set_equivariant_attempt (size_t clients, size_t blocks, size_t orbits) {
    if (auto* m = current ()) {
      m->equivariant = "attempted";
      m->equivariant_clients = clients;
      m->equivariant_blocks = blocks;
      m->equivariant_orbits = orbits;
    }
  }

  inline void set_symmetry_structure (symmetry::structure_report report) {
    if (auto* m = current ()) {
      m->symmetry_families = std::move (report.families);
      m->symmetry_indices = std::move (report.indices);
      m->symmetry_matrix = std::move (report.matrix);
      m->symmetry_subsets = std::move (report.subsets);
      m->symmetry_selected = std::move (report.selected);
      m->symmetry_orbit_sizes = std::move (report.orbit_sizes);
      m->symmetry_blocks = std::move (report.blocks);
      m->symmetry_shared = std::move (report.shared);
    }
  }

#else

  struct scoped_child {
      explicit scoped_child (std::string) {}
      struct noop {
          template <typename T>
          noop& operator= (T&&) { return *this; }
      };
      noop* operator-> () { return nullptr; }
  };

  struct scoped_timer {
      explicit scoped_timer (long long*) {}
  };

  enum class fine_metric { cpre, picker, apply };
  struct scoped_fine_timer {
      explicit scoped_fine_timer (fine_metric) {}
  };
  struct scoped_downset_timer {};

  inline void observe_loop (size_t, int) {}
  inline void observe_action () {}
  inline void snapshot_action_progress () {}
  inline void observe_meets (size_t, size_t) {}
  inline void snapshot_intersection_progress () {}
  inline void snapshot_loop_progress (std::string_view) {}
  inline void snapshot (std::string_view) {}
  inline void set_final_reason (std::string) {}
  inline bool finish (bool solved, std::string) { return solved; }
  inline void set_equivariant_decline (std::string) {}
  inline void set_equivariant_attempt (size_t, size_t, size_t) {}
  inline void set_symmetry_structure (symmetry::structure_report) {}

#endif

}  // namespace acacia::diagnostics
