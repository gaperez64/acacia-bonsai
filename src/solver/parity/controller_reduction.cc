#include "solver/bounded_subprocess.hh"
#include "solver/mealy_to_moore.hh"
#include "solver/parity/route.hh"
#include "solver/scope_memory.hh"
#include "solver/synthesis_outputs.hh"
#include <spot/twaalgos/mealy_machine.hh>

#include <memory>
#include <set>
#include <spot/twaalgos/game.hh>
#include <spot/twaalgos/synthesis.hh>
#include <sstream>

namespace acacia::parity_route {
  namespace {
    constexpr rlim_t max_memory_bytes = rlim_t {512} * 1024 * 1024;
    constexpr size_t payload_bytes = 1024 * 1024;
    constexpr int unchanged_exit = 8, size_exit = 9;
    struct payload {
        size_t size;
        char text[payload_bytes];
    };

    void record (mealy_reduction option, bool reselect, const char* outcome, const char* cause,
                 uint64_t start, uint64_t deadline, rlim_t memory_bytes, pid_t child) noexcept {
      if (!phase_records_enabled ())
        return;
      const char* names[] = {"none", "bisim", "bisim-output", "reselect"};
      const auto now = phase_clock (CLOCK_MONOTONIC);
      worker_record local;
      auto* worker = active_worker_record ();
      if (!worker)
        worker = &local;
      char line[1024];
      const int size = snprintf (
          line, sizeof line,
          "{\"event\":\"parity_reduction_attempt\",\"worker\":%u,\"worker_pid\":%ld,"
          "\"seq\":%llu,\"requested_backend\":\"%s\",\"effective_backend\":\"%s\","
          "\"original_polarity\":\"%s\",\"proof_polarity\":\"%s\","
          "\"route\":\"%s\",\"stage\":\"%s\",\"reason\":\"%s\","
          "\"r_prepass\":%s,\"equivariance\":%s,\"dropped_records\":%llu,"
          "\"child_pid\":%ld,\"option\":\"%s\",\"attempt\":\"%s\",\"outcome\":\"%s\","
          "\"cause\":\"%s\",\"mono_ns\":%llu,\"wall_ns\":%llu,\"deadline_ns\":%llu,"
          "\"memory_bytes\":%llu}\n",
          worker->index, long (getpid ()), ++worker->sequence, worker->requested_backend,
          worker->effective_backend, worker->original_polarity, worker->proof_polarity,
          worker->route, worker->stage, worker->reason, worker->r_prepass ? "true" : "false",
          worker->equivariance ? "true" : "false", worker->dropped, long (child),
          names[unsigned (option)], reselect ? "reselect" : "reduce", outcome, cause,
          (unsigned long long) now, (unsigned long long) (now - start),
          (unsigned long long) deadline, (unsigned long long) memory_bytes);
      if (size > 0 && size_t (size) < sizeof line)
        phase_records_send (line, size_t (size));
      else
        phase_records_drop ();
    }

    bool within_size_caps (const spot::twa_graph_ptr& machine) {
      if (machine->num_states () > 512 || machine->num_edges () > 8192)
        return false;
      unsigned nodes = 0;
      for (const auto& edge : machine->edges ()) {
        nodes += bdd_nodecount (edge.cond);
        if (nodes > 65536)
          return false;
      }
      return true;
    }

    void simplify (spot::twa_graph_ptr& machine, mealy_reduction reduction) {
      spot::simplify_mealy_here (machine, reduction == mealy_reduction::bisim ? 1 : 2, false);
    }
  }

  // Bound structural parsing without Spot's unbounded reconstruction of gate BDDs.
  std::optional<unsigned> checked_aiger_size (const std::string& text, const request& input,
                                              uint64_t deadline) {
    const auto expired = [&] { return deadline && phase_clock (CLOCK_MONOTONIC) >= deadline; };
    if (text.size () > payload_bytes || expired ())
      return std::nullopt;
    std::istringstream stream (text);
    std::string line, kind, extra;
    uint64_t m, i, l, o, a;
    if (!std::getline (stream, line))
      return std::nullopt;
    std::istringstream header (line);
    if (!(header >> kind >> m >> i >> l >> o >> a) || kind != "aag" || header >> extra ||
        m > text.size () || i > m || l > m || a > m || m != i + l + a ||
        i != input.inputs.size () || o != input.outputs.size ())
      return std::nullopt;
    const auto row = [&] (std::initializer_list<uint64_t*> values) {
      if (expired () || !std::getline (stream, line))
        return false;
      std::istringstream fields (line);
      for (auto* value : values)
        if (!(fields >> *value) || *value > 2 * m + 1)
          return false;
      return !(fields >> extra);
    };
    uint64_t current, left, right;
    for (uint64_t n = 0; n < i; ++n)
      if (!row ({&current}) || current != 2 * (n + 1))
        return std::nullopt;
    for (uint64_t n = 0; n < l; ++n)
      if (!row ({&current, &left}) || current != 2 * (i + n + 1))
        return std::nullopt;
    for (uint64_t n = 0; n < o; ++n)
      if (!row ({&current}))
        return std::nullopt;
    for (uint64_t n = 0; n < a; ++n)
      if (!row ({&current, &left, &right}) || current != 2 * (i + l + n + 1) || left >= current ||
          right >= current)
        return std::nullopt;
    for (char type : {'i', 'o'}) {
      const auto& names = type == 'i' ? input.inputs : input.outputs;
      std::set<std::string> remaining;
      size_t bytes = 0;
      for (const auto& name : names) {
        if (expired () || name.size () > text.size () - bytes || !remaining.insert (name).second)
          return std::nullopt;
        bytes += name.size ();
      }
      for (unsigned n = 0; n < names.size (); ++n)
        if (expired () || !std::getline (stream, line))
          return std::nullopt;
        else {
          const auto prefix = type + std::to_string (n) + " ";
          if (!line.starts_with (prefix) || !remaining.erase (line.substr (prefix.size ())))
            return std::nullopt;
        }
    }
    if (std::getline (stream, line))
      return std::nullopt;
    return unsigned (a + l);
  }

  void reduce_controller (result& incumbent, const request& input, const options& config,
                          uint64_t deadline, const spot::twa_graph_ptr& arena,
                          const char* scope_directory) {
    if (config.reduction == mealy_reduction::none)
      return;
    const auto start = phase_clock (CLOCK_MONOTONIC);
    const char* outcome = "fallback";
    const char* cause = "error";
    pid_t child = -1;
    rlim_t memory_bytes = 0;
    try {
      // Forking and polling need headroom even when the invocation is almost over.
      if (start >= deadline || deadline - start < 2000000)
        cause = "deadline";
      else if (arena && (arena->num_states () > 2048 || arena->num_edges () > 8192 ||
                         arena->num_sets () > 4))
        cause = "size";
      else {
        bool no_limit = false;
        const auto limit = scope_memory ("memory.max", scope_directory, &no_limit);
        constexpr uint64_t reserve = uint64_t {64} * 1024 * 1024;
        const auto rss_kb = resource_snapshot (getpid ()).rss_kb;
        const uint64_t resident = rss_kb > 0 ? uint64_t (rss_kb) * 1024 : 0;
        cause = "scope_memory";
        if (no_limit) {
          memory_bytes = max_memory_bytes;
          cause = "resource";
        }
        else if (const auto used = scope_memory ("memory.current", scope_directory);
                 limit && used) {
          const auto threshold = *limit - *limit / 8;
          const uint64_t headroom = threshold > *used ? threshold - *used : 0;
          // Cover a full resident copy-on-write plus transfer; leave half the rest unused.
          const auto margin = std::max (reserve, resident) + sizeof (payload);
          if (headroom > margin)
            memory_bytes = std::min<uint64_t> (max_memory_bytes, (headroom - margin) / 2);
          cause = "scope_headroom";
        }
        if (memory_bytes) {
          // Charge both resident sets and transfer against the supervisor's worker share.
          const auto parent_bytes = resident + sizeof (payload);
          const auto share = config.worker_rss_share;
          const auto available = share > parent_bytes ? share - parent_bytes : 0;
          if (available < memory_bytes) {
            memory_bytes = available;
            cause = "rss_share";
          }
          if (!resident || memory_bytes < resident + reserve)
            memory_bytes = 0;
        }
        if (!memory_bytes) {
          record (config.reduction, bool (arena), "skipped", cause, start, deadline, 0, -1);
          return;
        }
        const unsigned original_size =
            incumbent.reduced_aiger.empty ()
                ? incumbent.circuit->num_gates () + incumbent.circuit->num_latches ()
                : checked_aiger_size (incumbent.reduced_aiger, input, deadline).value ();
        const auto unmap = [] (payload* data) { munmap (data, sizeof (payload)); };
        auto* mapping = static_cast<payload*> (mmap (
            nullptr, sizeof (payload), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
        if (mapping == MAP_FAILED)
          throw std::bad_alloc ();
        std::unique_ptr<payload, decltype (unmap)> data (mapping, unmap);
        const auto attempt = subprocess::run<true> (deadline, memory_bytes, [&] {
          auto candidate = incumbent.controller;
          if (arena) {
            auto alternative = spot::make_twa_graph (spot::const_twa_graph_ptr (arena),
                                                     spot::twa::prop_set::all (), true);
            // Re-solving the full arena preserves liveness when choosing different edges.
            alternative->get_graph ().sort_edges_of_<true> (
                [] (const auto& left, const auto& right) { return left.dst < right.dst; });
            if (!spot::solve_parity_game (alternative, true))
              return subprocess::error_exit;
            spot::synthesis_info synthesis;
            synthesis.moore = input.moore && input.frame == timing_frame::original;
            synthesis.minimize_lvl = 0;
            candidate = spot::solved_game_to_separated_mealy (alternative, synthesis);
          }
          if (!within_size_caps (candidate))
            return size_exit;
          simplify (candidate, config.reduction);
          if (!spot::is_separated_mealy (candidate) ||
              !spot::is_input_deterministic_mealy (candidate))
            return subprocess::error_exit;
          const auto outputs = spot::get_synthesis_outputs (candidate);
          for (unsigned state = 0; state < candidate->num_states (); ++state) {
            bdd covered = bddfalse;
            for (const auto& edge : candidate->out (state))
              covered |= bdd_exist (edge.cond, outputs);
            if (covered != bddtrue)
              return subprocess::error_exit;
          }
          synthesis::validate_output_partition ({candidate}, {input.outputs});
          auto circuit = spot::mealy_machine_to_aig (candidate, config.aiger_mode.c_str (),
                                                     input.inputs, input.outputs);
          if (input.moore && input.frame == timing_frame::effective_mealy)
            circuit = synthesis::mealy_to_moore (circuit, true);
          if (config.adapt_circuit)
            circuit = config.adapt_circuit (std::move (circuit));
          if (circuit->num_gates () + circuit->num_latches () >= original_size)
            return unchanged_exit;
          std::ostringstream encoded;
          spot::print_aiger (encoded, circuit);
          const auto text = encoded.str ();
          const auto size = checked_aiger_size (text, input);
          if (!size || *size >= original_size)
            return subprocess::error_exit;
          memcpy (data->text, text.data (), text.size ());
          data->size = text.size ();
          return 0;
        });
        child = attempt.child;
        cause = attempt.cause;
        if (std::strcmp (cause, "none") == 0) {
          if (attempt.code == 0 && data->size && data->size <= payload_bytes) {
            std::string text (data->text, data->size);
            const auto size = checked_aiger_size (text, input, deadline);
            if (!size)
              cause = phase_clock (CLOCK_MONOTONIC) >= deadline ? "deadline" : "invalid";
            else if (*size >= original_size)
              cause = "not_smaller";
            else if (phase_clock (CLOCK_MONOTONIC) >= deadline)
              cause = "deadline";
            else {
              incumbent.reduced_aiger = std::move (text);
              outcome = "accepted";
            }
          }
          else
            cause = attempt.code == unchanged_exit ? "not_smaller"
                    : attempt.code == size_exit    ? "size"
                                                   : "invalid";
        }
      }
    } catch (const std::bad_alloc&) {
      cause = "resource";
    } catch (...) {
      cause = phase_clock (CLOCK_MONOTONIC) >= deadline ? "deadline" : "error";
    }
    record (config.reduction, bool (arena), outcome, cause, start, deadline, memory_bytes, child);
  }
}
