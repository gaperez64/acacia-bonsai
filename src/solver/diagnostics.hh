#pragma once

#include "configuration.hh"
#include "version.hh"
#include "solver/k_schedule.hh"
#include "solver/symmetry.hh"
#include <string_view>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

#ifndef ACACIA_ENABLE_DIAGNOSTICS
# define ACACIA_ENABLE_DIAGNOSTICS 0
#endif

namespace acacia::diagnostics {

  inline size_t env_size (const char* name, size_t fallback, bool allow_zero) {
    const char* text = std::getenv (name);
    if (text == nullptr or *text == '\0')
      return fallback;
    char* end = nullptr;
    const unsigned long value = std::strtoul (text, &end, 10);
    if (end == text or *end != '\0' or (value == 0 and not allow_zero))
      return fallback;
    return static_cast<size_t> (value);
  }

#if ACACIA_ENABLE_DIAGNOSTICS

  using clock = std::chrono::steady_clock;

  inline bool env_flag_enabled (const char* name) {
    const char* env = std::getenv (name);
    if (env == nullptr or *env == '\0')
      return false;
    std::string_view v {env};
    return v != "0" and v != "false" and v != "FALSE" and v != "off" and v != "OFF";
  }

  inline bool enabled () {
    static const bool value = env_flag_enabled ("ACACIA_DIAG");
    return value;
  }

  enum class preprocessing_census_mode { off, continue_solving, census_only };

  inline preprocessing_census_mode preprocessing_census () {
    static const preprocessing_census_mode value = [] {
      const char* env = std::getenv ("ACACIA_DIAG_PREPROCESSING_CENSUS");
      if (env == nullptr or *env == '\0')
        return preprocessing_census_mode::off;
      if (std::string_view {env} == "only")
        return preprocessing_census_mode::census_only;
      return env_flag_enabled ("ACACIA_DIAG_PREPROCESSING_CENSUS")
                 ? preprocessing_census_mode::continue_solving
                 : preprocessing_census_mode::off;
    }();
    return value;
  }

  inline bool alphabet_census_only () {
    static const bool value = env_flag_enabled ("ACACIA_DIAG_ALPHABET_CENSUS_ONLY");
    return value;
  }

  // The semantic-action census extends the alphabet census with the numbers
  // Sprint A needs: the per-input maxima, the inclusion-minimal residual-root
  // count, and how many transition sets the expansion actually decoded.  The
  // per-input maxima ride along on the walk the alphabet census already makes,
  // so they are free; dominance and decode-side validation each cost real work
  // and get their own switch.
  inline bool semantic_dominance_census () {
    static const bool value = env_flag_enabled ("ACACIA_DIAG_SEMANTIC_DOMINANCE");
    return value;
  }

  inline bool semantic_decode_census () {
    static const bool value = env_flag_enabled ("ACACIA_DIAG_SEMANTIC_DECODE");
    return value;
  }

  inline bool support_demand_enabled () {
    static const bool value = env_flag_enabled ("ACACIA_DIAG_SUPPORT_DEMAND");
    return value;
  }

  struct support_demand_metrics {
      bool graph_ready = false;
      size_t states = 0, edges = 0;
      size_t search_rows = 0, verification_rows = 0, union_rows = 0;
      size_t verification_only_rows = 0, union_edges = 0;
      int k = -1;
      size_t k_union_rows = 0, k_union_edges = 0;
      unsigned long long search_applications = 0, verification_applications = 0;
      size_t support_max = 0;
      // Exact histogram over 0..|Q|: O(|Q|) memory, independent of the number
      // of applications and K bumps. No reservoir error in median or p95.
      // Owning all accumulators here makes scoped_attempt's value snapshot
      // restore row unions, histograms and action IDs together.
      std::vector<unsigned long long> support_histogram;
      std::vector<size_t> out_degree;
      // Bits 1/2 are search/verification across K; bit 4 is this K's union.
      std::vector<unsigned char> rows;
      // IDs are flat ordinals in the existing input/action traversal order.
      // Addresses are lookup keys only; they never determine IDs or ordering.
      std::unordered_map<const void*, size_t> action_ids;
      std::vector<unsigned char> actions_used;

      size_t order_statistic (unsigned long long target) const {
        if (target == 0)
          return 0;
        unsigned long long cumulative = 0;
        for (size_t size = 0; size < support_histogram.size (); ++size) {
          cumulative += support_histogram[size];
          if (cumulative >= target)
            return size;
        }
        return support_max;
      }

      size_t percentile (unsigned percent) const {
        const auto samples = search_applications + verification_applications;
        // Nearest-rank p95, avoiding overflow in samples * percent.
        return order_statistic ((samples / 100) * percent
                                + ((samples % 100) * percent + 99) / 100);
      }

      double median () const {
        const auto samples = search_applications + verification_applications;
        if (samples == 0)
          return 0;
        return (double (order_statistic (samples / 2 + samples % 2))
                + double (order_statistic (samples / 2 + 1))) / 2.0;
      }

      std::string used_ids () const {
        std::string ids;
        for (size_t id = 0; id < actions_used.size (); ++id)
          if (actions_used[id]) {
            if (not ids.empty ())
              ids += ',';
            ids += std::to_string (id);
          }
        return ids.empty () ? "-" : ids;
      }
  };

  struct child_metrics {
      support_demand_metrics support_demand {};
      long long support_action_construction_ms = 0;
      double support_verification_ms = 0.0;
      std::string support_formula_fnv1a64 = "-";
      std::string support_phase = "not-started";
      std::string support_backend = "unknown";
      std::string automaton_provider = "frozen-graph";
      std::string game_backend = "unknown";
      std::string candidate_mode = "only";
      std::string instance = "-";
      std::string path = "unknown";
      std::string result = "unknown";
      std::string final_reason = "unknown";
      std::string translation_pref = "unknown";
      std::string k_schedule = acacia::k_schedule::name (ACACIA_K_SCHEDULE);
      std::string source_format = "ltl";
      std::string tlsf_semantics = "-";
      std::string tlsf_target = "-";
      std::string tlsf_effective_target = "-";
      std::string syntactic_bypass = "not-run";
      std::string forced_contradiction = "not-run";
      std::string forced_contradiction_kind = "";
      std::string fast_class = "not-run";
      std::string fast_verdict = "fallback";
      std::string preprocessor = "unknown";
      std::string equivariant = "not-run";
      std::string local_probe_status = "none";
      std::string forward_result = "not-run";
      std::string forward_resource_reason = "none";
      std::string forward_final_reason = "not-run";
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
      long long forced_contradiction_ms = 0;
      double profile_dominance_ms = 0.0;
      long long translation_ms = 0;
      long long fast_class_ms = 0;
      long long fast_solve_ms = 0;
      long long preproc_ms = 0;
      long long cap_census_ms = 0;
      long long simulation_census_ms = 0;
      long long solve_ms = 0;
      double cpre_ms = 0.0;
      double picker_ms = 0.0;
      double apply_ms = 0.0;
      double downset_ms = 0.0;
      double forward_certificate_verify_ms = 0.0;
      double forward_total_ms = 0.0;

      bool rsimp_changed = false;
      bool forward_backend = false;
      unsigned forced_contradiction_delay = 0;
      size_t aut_states = 0;
      size_t aut_edges = 0;
      std::size_t forced_contradiction_invariants = 0;
      std::size_t forced_contradiction_responses = 0;
      std::size_t profile_actions_before = 0;
      std::size_t profile_actions_after = 0;
      std::size_t profile_dominance_tests = 0;
      std::size_t profile_dominance_endpoint_visits = 0;
      std::size_t profile_dominance_declined = 0;
      size_t preproc_states_before = 0;
      size_t preproc_states_after = 0;
      size_t preproc_edges_before = 0;
      size_t preproc_edges_after = 0;
      size_t cap_k = 0;
      size_t cap_states_at_k = 0;
      size_t cap_states_finite = 0;
      size_t cap_states_zero = 0;
      size_t cap_counting_states = 0;
      size_t cap_finite_counting_states = 0;
      size_t simulation_states_after = 0;
      size_t simulation_states_removed = 0;
      size_t bool_threshold = 0;
      size_t max_f = 0;
      size_t max_f_size = 0;
      unsigned long long local_probe_runs = 0;
      unsigned long long local_probe_forward_apps = 0;
      unsigned long long local_probe_skipped_over_budget = 0;
      unsigned long long local_probe_nodes = 0;
      int forward_K = -1;
      unsigned long long forward_env_nodes = 0;
      unsigned long long forward_ctrl_nodes = 0;
      unsigned long long forward_env_expanded = 0;
      unsigned long long forward_ctrl_expanded = 0;
      unsigned long long forward_losing_antichain_size = 0;
      unsigned long long forward_losing_insertions = 0;
      unsigned long long forward_invalidation_scans = 0;
      unsigned long long forward_nodes_checked = 0;
      unsigned long long forward_nodes_invalidated = 0;
      unsigned long long forward_raw_actions = 0;
      unsigned long long forward_actions_skipped = 0;
      unsigned long long forward_covers_created = 0;
      unsigned long long forward_covers_resolved = 0;
      unsigned long long forward_cover_search_visits = 0;
      unsigned long long forward_distinct_successors = 0;
      unsigned long long forward_minimal_successors = 0;
      unsigned long long forward_strategy_rank_nodes = 0;
      unsigned long long forward_rank_bytes = 0;
      unsigned long long forward_node_bytes = 0;
      unsigned long long forward_index_bytes = 0;
      unsigned long long forward_total_bytes = 0;
      unsigned long long cpre_skipped = 0;
      unsigned long long k_bumped_by_local_refutation = 0;
      unsigned long long actions_seen = 0;
      unsigned long long meets_computed = 0;
      unsigned long long meet_batches = 0;
      unsigned long long alphabet_input_paths = 0;
      unsigned long long alphabet_input_nodes = 0;
      unsigned long long alphabet_output_paths = 0;
      unsigned long long alphabet_output_nodes = 0;
      unsigned long long alphabet_bdd_nodes = 0;
      unsigned long long alphabet_max_output_paths = 0;
      unsigned long long alphabet_max_output_nodes = 0;
      unsigned long long alphabet_minimal_output_nodes = 0;
      unsigned long long alphabet_dominance_tests = 0;
      unsigned long long alphabet_dominance_declines = 0;
      unsigned long long alphabet_census_ms = 0;
      unsigned long long alphabet_dominance_ms = 0;
      unsigned long long decoded_transition_sets = 0;
      unsigned long long decoded_unique_transition_sets = 0;
      unsigned long long decode_ms = 0;
      int loops = 0;
      int k_attempts = 0;
      int k_last_next = 0;
      int last_k = -1;
      int tlsf_gr_level = -1;
      size_t equivariant_clients = 0;
      size_t equivariant_blocks = 0;
      size_t equivariant_orbits = 0;
      clock::time_point started = clock::now ();

      void observe_k (int k) {
        if (last_k != k) {
          last_k = k;
          ++k_attempts;
        }
      }

      void observe_loop (size_t f_size, int k) {
        ++loops;
        max_f = std::max (max_f, f_size);
        max_f_size = std::max (max_f_size, f_size);
        observe_k (k);
      }

      void refresh_total () {
        total_ms = std::chrono::duration_cast<std::chrono::milliseconds> (clock::now () - started)
                       .count ();
      }
  };

  inline thread_local child_metrics* current_child = nullptr;
  inline thread_local child_metrics* active_cpre_metrics = nullptr;
  inline thread_local clock::time_point active_cpre_started {};

  inline child_metrics* current () { return enabled () ? current_child : nullptr; }

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
    }();
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
         // The diag consumers parse key=value, so a new field is additive.
         // A campaign can now record the configuration rather than a flag list.
         << " preset=" << (*acacia_preset () ? acacia_preset () : "-")
         << " pid=" << getpid () << " diag_kind=" << kind << " checkpoint=" << checkpoint
         << " instance=" << m.instance << " path=" << m.path
         << " translation_pref=" << m.translation_pref << " source_format=" << m.source_format
         << " tlsf_semantics=" << m.tlsf_semantics << " tlsf_target=" << m.tlsf_target
         << " tlsf_effective_target=" << m.tlsf_effective_target
         << " tlsf_gr_level=" << m.tlsf_gr_level << " rsimp_ms=" << m.rsimp_ms
         << " rsimp_changed=" << (m.rsimp_changed ? 1 : 0)
         << " syntactic_bypass=" << m.syntactic_bypass
         << " syntactic_bypass_ms=" << m.syntactic_bypass_ms
         << " forced_contradiction=" << m.forced_contradiction
         << " forced_contradiction_kind=" << m.forced_contradiction_kind
         << " forced_contradiction_delay=" << m.forced_contradiction_delay
         << " forced_contradiction_invariants=" << m.forced_contradiction_invariants
         << " forced_contradiction_responses=" << m.forced_contradiction_responses
         << " forced_contradiction_ms=" << m.forced_contradiction_ms
         << " profile_actions_before=" << m.profile_actions_before
         << " profile_actions_after=" << m.profile_actions_after
         << " profile_dominance_tests=" << m.profile_dominance_tests
         << " profile_dominance_endpoint_visits=" << m.profile_dominance_endpoint_visits
         << " profile_dominance_declined=" << m.profile_dominance_declined
         << " profile_dominance_ms=" << m.profile_dominance_ms
         << " translation_ms=" << m.translation_ms << " aut_states=" << m.aut_states
         << " aut_edges=" << m.aut_edges << " fast_class=" << m.fast_class
         << " fast_class_ms=" << m.fast_class_ms << " fast_solve_ms=" << m.fast_solve_ms
         << " fast_verdict=" << m.fast_verdict << " preproc=" << m.preprocessor
         << " preproc_ms=" << m.preproc_ms << " preproc_states_before=" << m.preproc_states_before
         << " preproc_states_after=" << m.preproc_states_after
         << " preproc_edges_before=" << m.preproc_edges_before
         << " preproc_edges_after=" << m.preproc_edges_after
         << " cap_census_ms=" << m.cap_census_ms << " cap_k=" << m.cap_k
         << " cap_states_at_k=" << m.cap_states_at_k
         << " cap_states_finite=" << m.cap_states_finite
         << " cap_states_zero=" << m.cap_states_zero
         << " cap_counting_states=" << m.cap_counting_states
         << " cap_finite_counting_states=" << m.cap_finite_counting_states
         << " simulation_census_ms=" << m.simulation_census_ms
         << " simulation_states_after=" << m.simulation_states_after
         << " simulation_states_removed=" << m.simulation_states_removed
         << " bool_threshold=" << m.bool_threshold << " max_f=" << m.max_f
         << " max_f_size=" << m.max_f_size << " loops=" << m.loops
         << " k_attempts=" << m.k_attempts << " k_schedule=" << m.k_schedule
         << " k_last_next=" << m.k_last_next << " local_probe_runs=" << m.local_probe_runs
         << " local_probe_status=" << m.local_probe_status
         << " local_probe_forward_apps=" << m.local_probe_forward_apps
         << " local_probe_skipped_over_budget=" << m.local_probe_skipped_over_budget
         << " local_probe_nodes=" << m.local_probe_nodes
         << " forward_backend=" << (m.forward_backend ? 1 : 0)
         << " forward_K=" << m.forward_K << " forward_result=" << m.forward_result
         << " forward_resource_reason=" << m.forward_resource_reason
         << " forward_env_nodes=" << m.forward_env_nodes
         << " forward_ctrl_nodes=" << m.forward_ctrl_nodes
         << " forward_env_expanded=" << m.forward_env_expanded
         << " forward_ctrl_expanded=" << m.forward_ctrl_expanded
         << " forward_losing_antichain_size=" << m.forward_losing_antichain_size
         << " forward_losing_insertions=" << m.forward_losing_insertions
         << " forward_invalidation_scans=" << m.forward_invalidation_scans
         << " forward_nodes_checked=" << m.forward_nodes_checked
         << " forward_nodes_invalidated=" << m.forward_nodes_invalidated
         << " forward_raw_actions=" << m.forward_raw_actions
         << " forward_actions_skipped=" << m.forward_actions_skipped
         << " forward_covers_created=" << m.forward_covers_created
         << " forward_covers_resolved=" << m.forward_covers_resolved
         << " forward_cover_search_visits=" << m.forward_cover_search_visits
         << " forward_distinct_successors=" << m.forward_distinct_successors
         << " forward_minimal_successors=" << m.forward_minimal_successors
         << " forward_strategy_rank_nodes=" << m.forward_strategy_rank_nodes
         << " forward_rank_bytes=" << m.forward_rank_bytes
         << " forward_node_bytes=" << m.forward_node_bytes
         << " forward_index_bytes=" << m.forward_index_bytes
         << " forward_total_bytes=" << m.forward_total_bytes
         << " forward_certificate_verify_ms=" << m.forward_certificate_verify_ms
         << " forward_total_ms=" << m.forward_total_ms
         << " forward_final_reason=" << m.forward_final_reason
         << " cpre_skipped=" << m.cpre_skipped
         << " k_bumped_by_local_refutation=" << m.k_bumped_by_local_refutation
         << " cpre_ms=" << m.cpre_ms
         << " picker_ms=" << m.picker_ms << " apply_ms=" << m.apply_ms
         << " downset_ms=" << m.downset_ms << " actions_seen=" << m.actions_seen
         << " meets_computed=" << m.meets_computed << " meet_batches=" << m.meet_batches
         << " alphabet_input_paths=" << m.alphabet_input_paths
         << " alphabet_input_nodes=" << m.alphabet_input_nodes
         << " alphabet_output_paths=" << m.alphabet_output_paths
         << " alphabet_output_nodes=" << m.alphabet_output_nodes
         << " alphabet_bdd_nodes=" << m.alphabet_bdd_nodes
         << " alphabet_max_output_paths=" << m.alphabet_max_output_paths
         << " alphabet_max_output_nodes=" << m.alphabet_max_output_nodes
         << " alphabet_minimal_output_nodes=" << m.alphabet_minimal_output_nodes
         << " alphabet_dominance_tests=" << m.alphabet_dominance_tests
         << " alphabet_dominance_declines=" << m.alphabet_dominance_declines
         << " alphabet_census_ms=" << m.alphabet_census_ms
         << " alphabet_dominance_ms=" << m.alphabet_dominance_ms
         << " decoded_transition_sets=" << m.decoded_transition_sets
         << " decoded_unique_transition_sets=" << m.decoded_unique_transition_sets
         << " decode_ms=" << m.decode_ms << " equivariant=" << m.equivariant
         << " eq_clients=" << m.equivariant_clients << " eq_blocks=" << m.equivariant_blocks
         << " eq_orbits=" << m.equivariant_orbits << " sym_families=" << m.symmetry_families
         << " sym_indices=" << m.symmetry_indices << " sym_matrix=" << m.symmetry_matrix
         << " sym_subsets=" << m.symmetry_subsets << " sym_selected=" << m.symmetry_selected
         << " sym_orbit_sizes=" << m.symmetry_orbit_sizes << " sym_blocks=" << m.symmetry_blocks
         << " sym_shared=" << m.symmetry_shared << " solve_ms=" << m.solve_ms
         << " total_ms=" << m.total_ms << " result=" << m.result
         << " final_reason=" << m.final_reason
         << " support_demand_enabled=" << support_demand_enabled ()
         << " support_graph_ready=" << m.support_demand.graph_ready
         << " support_graph_states=" << m.support_demand.states
         << " support_graph_edges=" << m.support_demand.edges
         << " support_search_rows=" << m.support_demand.search_rows
         << " support_verification_rows=" << m.support_demand.verification_rows
         << " support_verification_only_rows=" << m.support_demand.verification_only_rows
         << " support_union_rows=" << m.support_demand.union_rows
         << " support_union_edges=" << m.support_demand.union_edges
         << " support_k=" << m.support_demand.k
         << " support_k_union_rows=" << m.support_demand.k_union_rows
         << " support_k_union_edges=" << m.support_demand.k_union_edges
         << " support_search_applications=" << m.support_demand.search_applications
         << " support_verification_applications=" << m.support_demand.verification_applications
         << " support_median=" << m.support_demand.median ()
         << " support_p95=" << m.support_demand.percentile (95)
         << " support_max=" << m.support_demand.support_max
         << " support_action_profiles_used="
         << std::count (m.support_demand.actions_used.begin (),
                        m.support_demand.actions_used.end (), 1)
         << " support_action_profile_ids=" << m.support_demand.used_ids ()
         << " support_action_construction_ms=" << m.support_action_construction_ms
         << " support_verification_ms=" << m.support_verification_ms
         << " support_formula_fnv1a64=" << m.support_formula_fnv1a64
         << " support_phase=" << m.support_phase
         << " support_backend=" << m.support_backend
         << " automaton_provider=" << m.automaton_provider
         << " game_backend=" << m.game_backend
         << " candidate_mode=" << m.candidate_mode
         // Unknown/zero denominators are absent values, never fabricated ratios.
         << " support_rho_q="
         << (m.support_demand.graph_ready and m.support_demand.states != 0
                 ? std::to_string (double (m.support_demand.union_rows) / m.support_demand.states)
                 : "-")
         << " support_rho_e="
         << (m.support_demand.graph_ready and m.support_demand.edges != 0
                 ? std::to_string (double (m.support_demand.union_edges) / m.support_demand.edges)
                 : "-") << '\n';
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

      child_metrics* operator->() { return current (); }
      child_metrics& get () { return metrics; }

    private:
      child_metrics metrics;
      child_metrics* previous = nullptr;
      clock::time_point started;
  };

  // A speculative solve (for example, an unrealizability witness) must not
  // make the enclosing formula look as though that attempt were its final
  // path.  Roll all metrics back unless the caller explicitly commits the
  // attempt.  Keeping the original start time in the snapshot means the
  // enclosing child's total wall time still includes discarded attempts.
  class scoped_attempt {
    public:
      scoped_attempt ()
        : metrics {current ()},
          before {metrics != nullptr ? *metrics : child_metrics {}} {}

      scoped_attempt (const scoped_attempt&) = delete;
      scoped_attempt& operator= (const scoped_attempt&) = delete;

      ~scoped_attempt () {
        if (metrics != nullptr and not committed)
          *metrics = std::move (before);
      }

      void commit () { committed = true; }

    private:
      child_metrics* metrics = nullptr;
      child_metrics before;
      bool committed = false;
  };

  class scoped_timer {
    public:
      explicit scoped_timer (long long* target)
        : target {enabled () ? target : nullptr},
          started {clock::now ()} {}

      ~scoped_timer () {
        if (target != nullptr)
          *target +=
              std::chrono::duration_cast<std::chrono::milliseconds> (clock::now () - started)
                  .count ();
      }

    private:
      long long* target = nullptr;
      clock::time_point started;
  };

  enum class fine_metric { cpre, picker, apply, support_verification };

  class scoped_fine_timer {
    public:
      explicit scoped_fine_timer (fine_metric metric)
        : metrics {current ()},
          metric {metric},
          started {clock::now ()} {
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
          case fine_metric::cpre: break;
          case fine_metric::picker: metrics->picker_ms += elapsed; break;
          case fine_metric::apply: metrics->apply_ms += elapsed; break;
          case fine_metric::support_verification: metrics->support_verification_ms += elapsed; break;
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

  inline void set_k_last_next (int next_k) {
    if (auto* m = current ())
      m->k_last_next = next_k;
  }

  inline void snapshot (std::string_view checkpoint) {
    if (auto* m = current ()) {
      m->refresh_total ();
      print (*m, "progress", checkpoint);
    }
  }

  template <typename Aut>
  inline void set_support_graph (const Aut& aut) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ()) {
      auto& d = m->support_demand;
      // Called on the frozen, post-preprocessing graph, before action creation.
      // A forward-to-backward fallback on that same graph retains its union.
      if (d.graph_ready)
        return;
      d.states = aut->num_states ();
      d.out_degree.assign (d.states, 0);
      for (size_t q = 0; q < d.states; ++q)
        for ([[maybe_unused]] const auto& edge : aut->out (q))
          ++d.out_degree[q];
      for (const auto degree : d.out_degree)
        d.edges += degree;
      d.rows.assign (d.states, 0);
      d.support_histogram.assign (d.states + 1, 0);
      d.graph_ready = true;
      m->support_phase = "action-construction";
    }
  }

  inline void set_support_formula (std::string_view formula) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ()) {
      // Stable digest of Spot's serialized, transformed worker formula.
      std::uint64_t hash = UINT64_C (14695981039346656037);
      for (const unsigned char byte : formula) {
        hash ^= byte;
        hash *= UINT64_C (1099511628211);
      }
      std::ostringstream digest;
      digest << std::hex << hash;
      m->support_formula_fnv1a64 = digest.str ();
      m->support_phase = "translation";
    }
  }

  inline void set_support_phase (std::string_view phase) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ())
      m->support_phase = phase;
  }

  inline void set_support_backend (std::string_view backend) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ())
      m->support_backend = backend;
  }

  inline void set_support_k (int k) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ()) {
      auto& d = m->support_demand;
      if (d.k == k)
        return;
      if (d.k != -1)
        snapshot ("support-demand-k");
      d.k = k;
      d.k_union_rows = d.k_union_edges = 0;
      for (auto& row : d.rows)
        row &= 3;
    }
  }

  template <typename Actions>
  inline void set_support_actions (const Actions& actions) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ()) {
      auto& d = m->support_demand;
      d.action_ids.clear ();
      size_t id = 0;
      for (const auto& input : actions)
        for (const auto& action : input.second)
          d.action_ids.emplace (&action, id++);
      d.actions_used.resize (id, 0);
      m->support_phase = "search";
    }
  }

  template <typename Rank, typename Action>
  inline void observe_support_demand (const Rank& rank, const Action& action,
                                      bool verification = false) {
    if (not support_demand_enabled ())
      return;
    if (auto* m = current ()) {
      auto& d = m->support_demand;
      if (not d.graph_ready)
        return;
      size_t support = 0;
      for (size_t q = 0; q < rank.size (); ++q) {
        // avec is indexed by destination; rank[q] is the SOURCE value.
        // Dense scans of semantically absent (-1) coordinates request no row.
        if (rank[q] == -1)
          continue;
        ++support;
        auto& row = d.rows[q];
        if ((row & 3) == 0) {
          ++d.union_rows;
          d.union_edges += d.out_degree[q];
        }
        if ((row & 4) == 0) {
          ++d.k_union_rows;
          d.k_union_edges += d.out_degree[q];
        }
        if (verification) {
          if ((row & 2) == 0) {
            ++d.verification_rows;
            if ((row & 1) == 0)
              ++d.verification_only_rows;
          }
          row |= 2;
        } else {
          if ((row & 1) == 0) {
            ++d.search_rows;
            if ((row & 2) != 0)
              --d.verification_only_rows;
          }
          row |= 1;
        }
        row |= 4;
      }
      ++d.support_histogram[support];
      d.support_max = std::max (d.support_max, support);
      if (verification)
        ++d.verification_applications;
      else
        ++d.search_applications;
      if (const auto found = d.action_ids.find (&action); found != d.action_ids.end ())
        d.actions_used[found->second] = 1;
      // Forward search has no fixed-point loop checkpoints. Retain bounded
      // progress at powers of two so a killed search still reports observed
      // demand, with the campaign explicitly marking it as incomplete.
      const auto applications = d.search_applications + d.verification_applications;
      if (applications <= 4 or (applications & (applications - 1)) == 0)
        snapshot ("support-demand-progress");
    }
  }

  inline void set_alphabet_census (unsigned long long input_paths, unsigned long long input_nodes,
                                   unsigned long long output_paths,
                                   unsigned long long output_nodes, unsigned long long bdd_nodes) {
    if (auto* m = current ()) {
      m->alphabet_input_paths = input_paths;
      m->alphabet_input_nodes = input_nodes;
      m->alphabet_output_paths = output_paths;
      m->alphabet_output_nodes = output_nodes;
      m->alphabet_bdd_nodes = bdd_nodes;
    }
  }

  inline void set_semantic_action_census (unsigned long long max_output_paths,
                                          unsigned long long max_output_nodes,
                                          unsigned long long minimal_output_nodes,
                                          unsigned long long dominance_tests,
                                          unsigned long long dominance_declines,
                                          unsigned long long census_ms,
                                          unsigned long long dominance_ms) {
    if (auto* m = current ()) {
      m->alphabet_max_output_paths = max_output_paths;
      m->alphabet_max_output_nodes = max_output_nodes;
      m->alphabet_minimal_output_nodes = minimal_output_nodes;
      m->alphabet_dominance_tests = dominance_tests;
      m->alphabet_dominance_declines = dominance_declines;
      m->alphabet_census_ms = census_ms;
      m->alphabet_dominance_ms = dominance_ms;
    }
  }

  inline void set_decode_census (unsigned long long transition_sets,
                                 unsigned long long unique_transition_sets,
                                 unsigned long long elapsed_ms) {
    if (auto* m = current ()) {
      m->decoded_transition_sets = transition_sets;
      m->decoded_unique_transition_sets = unique_transition_sets;
      m->decode_ms = elapsed_ms;
    }
  }

  inline void set_local_probe (std::string status, unsigned long long forward_applications,
                               unsigned long long nodes, bool skipped_cpre,
                               bool bumped_k) {
    if (auto* m = current ()) {
      ++m->local_probe_runs;
      m->local_probe_status = std::move (status);
      m->local_probe_forward_apps += forward_applications;
      m->local_probe_nodes += nodes;
      if (skipped_cpre)
        ++m->cpre_skipped;
      if (bumped_k)
        ++m->k_bumped_by_local_refutation;
    }
  }

  inline void trace_local_probe (int k, unsigned long long loop, std::size_t region_size,
                                 const char* status, unsigned long long forward_apps,
                                 unsigned long long nodes) {
    static const bool enabled = [] {
      const char* env = std::getenv ("ACACIA_LOCAL_CERTIFICATE_TRACE");
      return env != nullptr and *env != '\0';
    } ();
    if (enabled)
      std::cerr << "ACACIA_PROBE k=" << k << " loop=" << loop << " size=" << region_size
                << " status=" << status << " fwd=" << forward_apps << " nodes=" << nodes
                << '\n';
  }

  inline void set_local_probe_skipped_over_budget () {
    if (auto* m = current ())
      ++m->local_probe_skipped_over_budget;
  }

  inline void set_forward_backend () {
    if (auto* m = current ())
      m->forward_backend = true;
  }

  inline void set_forward_attempt (int k, std::string result,
                                   std::string resource_reason,
                                   unsigned long long env_nodes,
                                   unsigned long long ctrl_nodes,
                                   unsigned long long env_expanded,
                                   unsigned long long ctrl_expanded,
                                   unsigned long long losing_antichain_size,
                                   unsigned long long losing_insertions,
                                   unsigned long long invalidation_scans,
                                   unsigned long long nodes_checked,
                                   unsigned long long nodes_invalidated,
                                   unsigned long long raw_actions,
                                   unsigned long long actions_skipped,
                                   unsigned long long covers_created,
                                   unsigned long long covers_resolved,
                                   unsigned long long cover_search_visits,
                                   unsigned long long distinct_successors,
                                   unsigned long long minimal_successors,
                                   unsigned long long strategy_rank_nodes,
                                   unsigned long long rank_bytes,
                                   unsigned long long node_bytes,
                                   unsigned long long index_bytes,
                                   unsigned long long total_bytes) {
    if (auto* m = current ()) {
      m->observe_k (k);
      m->forward_K = k;
      m->forward_result = std::move (result);
      m->forward_resource_reason = std::move (resource_reason);
      m->forward_env_nodes = env_nodes;
      m->forward_ctrl_nodes = ctrl_nodes;
      m->forward_env_expanded = env_expanded;
      m->forward_ctrl_expanded = ctrl_expanded;
      m->forward_losing_antichain_size = losing_antichain_size;
      m->forward_losing_insertions = losing_insertions;
      m->forward_invalidation_scans = invalidation_scans;
      m->forward_nodes_checked = nodes_checked;
      m->forward_nodes_invalidated = nodes_invalidated;
      m->forward_raw_actions = raw_actions;
      m->forward_actions_skipped = actions_skipped;
      m->forward_covers_created = covers_created;
      m->forward_covers_resolved = covers_resolved;
      m->forward_cover_search_visits = cover_search_visits;
      m->forward_distinct_successors = distinct_successors;
      m->forward_minimal_successors = minimal_successors;
      m->forward_strategy_rank_nodes = strategy_rank_nodes;
      m->forward_rank_bytes = rank_bytes;
      m->forward_node_bytes = node_bytes;
      m->forward_index_bytes = index_bytes;
      m->forward_total_bytes = total_bytes;
    }
  }

  inline void set_forward_certificate_verify_ms (double milliseconds) {
    if (auto* m = current ())
      m->forward_certificate_verify_ms = milliseconds;
  }

  inline void set_forward_total_ms (double milliseconds) {
    if (auto* m = current ())
      m->forward_total_ms = milliseconds;
  }

  inline void set_forward_final_reason (std::string reason) {
    if (auto* m = current ())
      m->forward_final_reason = std::move (reason);
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
          noop& operator= (T&&) {
            return *this;
          }
      };
      noop* operator->() { return nullptr; }
  };

  struct scoped_attempt {
      void commit () {}
  };

  inline bool alphabet_census_only () { return false; }
  inline bool support_demand_enabled () { return false; }
  template <typename Aut>
  inline void set_support_graph (const Aut&) {}
  inline void set_support_k (int) {}
  inline void set_support_formula (std::string_view) {}
  inline void set_support_phase (std::string_view) {}
  inline void set_support_backend (std::string_view) {}
  template <typename Actions>
  inline void set_support_actions (const Actions&) {}
  template <typename Rank, typename Action>
  inline void observe_support_demand (const Rank&, const Action&, bool = false) {}

  struct scoped_timer {
      explicit scoped_timer (long long*) {}
  };

  enum class fine_metric { cpre, picker, apply, support_verification };
  struct scoped_fine_timer {
      explicit scoped_fine_timer (fine_metric) {}
  };
  struct scoped_downset_timer {
      // Keep this diagnostics-off RAII stub non-trivial so named timer objects
      // do not trigger -Wunused-variable.  The empty inline bodies generate no code.
      scoped_downset_timer () {}
      ~scoped_downset_timer () {}
  };

  inline void observe_loop (size_t, int) {}
  inline void set_k_last_next (int) {}
  inline void observe_action () {}
  inline void snapshot_action_progress () {}
  inline void observe_meets (size_t, size_t) {}
  inline void snapshot_intersection_progress () {}
  inline void snapshot_loop_progress (std::string_view) {}
  inline void snapshot (std::string_view) {}
  inline void set_alphabet_census (unsigned long long, unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long) {}
  inline void set_semantic_action_census (unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          unsigned long long, unsigned long long,
                                          unsigned long long) {}
  inline void set_decode_census (unsigned long long, unsigned long long, unsigned long long) {}
  inline void set_local_probe (std::string, unsigned long long, unsigned long long, bool, bool) {}
  inline void trace_local_probe (int, unsigned long long, std::size_t, const char*,
                                 unsigned long long, unsigned long long) {}
  inline void set_local_probe_skipped_over_budget () {}
  inline void set_forward_backend () {}
  inline void set_forward_attempt (int, std::string, std::string,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long,
                                   unsigned long long, unsigned long long) {}
  inline void set_forward_certificate_verify_ms (double) {}
  inline void set_forward_total_ms (double) {}
  inline void set_forward_final_reason (std::string) {}
  inline void set_final_reason (std::string) {}
  inline bool finish (bool solved, std::string) { return solved; }
  inline void set_equivariant_decline (std::string) {}
  inline void set_equivariant_attempt (size_t, size_t, size_t) {}
  inline void set_symmetry_structure (symmetry::structure_report) {}

#endif

}  // namespace acacia::diagnostics

// Timing a phase is the same two lines everywhere: fetch the record, and start
// a scoped timer on one of its fields, tolerating a null record. Written out,
// it also has to be fenced, because the no-op diagnostics have no such fields
// to name -- so seven call sites in solver_invoker.cc each carried an #if
// around two lines. The fence belongs here instead.
#if ACACIA_ENABLE_DIAGNOSTICS
# define ACACIA_DIAG_SCOPED_TIMER(field)                                      \
    auto* acacia_diag_record_##field = ::acacia::diagnostics::current ();      \
    ::acacia::diagnostics::scoped_timer acacia_diag_timer_##field (           \
        acacia_diag_record_##field ? &acacia_diag_record_##field->field        \
                                   : nullptr)
#else
# define ACACIA_DIAG_SCOPED_TIMER(field) ((void) 0)
#endif
