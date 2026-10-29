#include "parity_arm.hh"

#include "phase_records.hh"
#include "solver/accounting_identity.hh"
#include "solver/controller_writer.hh"
#include "solver/mealy_to_moore.hh"
#include "solver/obligation_helper.hh"
#include "solver/parity/route.hh"
#include "solver/parity/tlsf_dpa.hh"
#include "solver/synthesis_rendezvous.hh"

#include <optional>
#include <spot/tl/parse.hh>
#include <stdexcept>

namespace acacia {
  namespace {
    void record (const char* arm, const char* event, unsigned tier) {
      if (!phase_records_enabled ())
        return;
      auto* worker = active_worker_record ();
      if (!worker)
        return;
      char line[768];
      const int length =
          snprintf (line, sizeof line,
                    "{\"arm\":\"%s\",\"event\":\"%s\",\"worker\":%u,"
                    "\"worker_pid\":%ld,\"seq\":%llu,\"tier\":%u,"
                    "\"mono_ns\":%llu,\"trust\":\"exact_game\",\"same_library_checked\":false,"
                    "\"independent_checked\":false}\n",
                    arm, event, worker->index, long (worker->pid), ++worker->sequence, tier,
                    (unsigned long long) phase_clock (CLOCK_MONOTONIC));
      if (length > 0 && size_t (length) < sizeof line)
        phase_records_send (line, size_t (length));
      else
        phase_records_drop ();
    }
  }

  void parity_arm_exhausted (worker_record& record) {
    auto* previous = active_worker_record ();
    active_worker_record () = &record;
    worker_stopped ("rss_share");
    active_worker_record () = previous;
  }

  int run_parity_arm (const arg_parse_result& args, bool oxidd, uint64_t deadline_mono_ns) {
    const char* arm = oxidd ? "both:parity:oxidd" : "both:parity:spot";
    worker_route ("original_game", "parity", "unknown");
    worker_stage ("parity_preprocessing");
    const char* stage = "preprocessing";
    try {
      if (!obligation::limit_address_space ()) {
        worker_stopped ("address_space_limit");
        return EXIT_CODE_UNKNOWN;
      }
      tlsf_frontend::specification child_local;
      const auto& objective = args.prepare_objective (child_local);
      const auto& metadata = objective.metadata;
      validate_ltl_partition (objective.inputs, objective.outputs);
      if (oxidd && !objective.ast) {
        worker_decline ("dpa_missing_ast");
        return EXIT_CODE_UNKNOWN;
      }
      const auto parsed = spot::parse_infix_psl (objective.formula);
      if (!parsed.f || !parsed.errors.empty ()) {
        worker_stopped ("invalid_formula");
        return EXIT_CODE_UNKNOWN;
      }
      // The frontend already lowers TLSF to effective Mealy. Translate the
      // original positive objective, without simplification, swapping or another X.
      parity_route::request input;
      input.formula = parsed.f;
      input.inputs = objective.inputs;
      input.outputs = objective.outputs;
      if (phase_records_enabled ())
        accounting_identity (input.formula, input.inputs, input.outputs,
                             oxidd ? "oxidd-parity" : "spot-parity",
                             oxidd ? "tlsf-dpa-stage1:max-odd:bounded-bfs"
                                   : "Generic:Deterministic:High:simul=0:ba-simul=0:det-simul=0:"
                                     "tls-impl=1:wdba-minimize=2",
                             "parity-route");
      parity_route::options config;
      config.synthesis = args.synth_fname.has_value ();
      unsigned tier = 0;
      config.on_classified = [&] (unsigned value, const std::string&) {
        tier = value;
        worker_route (oxidd       ? "tlsf_dpa"
                      : value < 4 ? "deterministic_buchi"
                                  : "acd_parity",
                      oxidd       ? "oxidd"
                      : value < 4 ? "spot-fast"
                                  : "spot",
                      "unknown");
        record (arm, "parity_tier", tier);
      };
      config.on_solved = [&] { record (arm, "parity_solved", tier); };
      config.on_verdict = [&] (bool real) {
        if (auto* worker = active_worker_record ())
          worker_record_text (worker->proof_polarity, real ? "REAL" : "UNREAL");
        record (arm, "parity_verdict", tier);
        if (args.synth_fname)
          synthesis::await_selection (real ? EXIT_CODE_REAL : EXIT_CODE_UNREAL);
      };
      if (oxidd) {
        tlsf_dpa::limits budget;
        // Share one 120-second construction/import deadline with the worker.
        constexpr uint64_t construction_ns = 120ULL * 1000000000ULL;
        const auto deadline = phase_clock (CLOCK_MONOTONIC) + construction_ns;
        budget.construction.deadline_mono_ns =
            deadline_mono_ns ? std::min (deadline, deadline_mono_ns) : deadline;
        const auto share = obligation::rss_share (args.arms ? args.arms->size () : 1);
        budget.construction.max_host_bytes = std::min<uint64_t> (
            budget.construction.max_host_bytes, std::max<uint64_t> (1, share / 4));
        budget.import_bytes =
            std::min<uint64_t> (budget.import_bytes, std::max<uint64_t> (1, share / 4));
        TlsfDpaStatus status;
        auto context = tlsf_dpa::context (nullptr, tlsf_dpa_free);
        {
          stage = "dpa_create";
          legacy_phase construction (stage);
          context = tlsf_dpa::create (tlsf_ast_root (objective.ast.get ()), input.inputs,
                                      input.outputs, budget, status);
        }
        tlsf_dpa::result imported;
        imported.status = status;
        if (context && status.code == TLSF_DPA_OK) {
          stage = "dpa_expand_import";
          legacy_phase expansion (stage);
          imported = tlsf_dpa::import (*context, budget);
        }
        if (phase_records_enabled ()) {
          char line[768];
          const int length =
              snprintf (line, sizeof line,
                        "{\"arm\":\"%s\",\"event\":\"dpa_import\",\"stage\":\"%s\","
                        "\"states\":%zu,\"imported_states\":%zu,\"edges\":%zu,\"labels\":%zu,"
                        "\"import_bytes\":%zu,\"reason\":\"%s\"}\n",
                        arm, imported.stage, context ? tlsf_dpa_state_count (context.get ()) : 0,
                        imported.states.size (), imported.edges, imported.labels,
                        imported.import_bytes, tlsf_dpa::reason (imported.status));
          if (length > 0 && size_t (length) < sizeof line)
            phase_records_send (line, size_t (length));
        }
        if (!imported.automaton) {
          if (imported.status.reason == TLSF_DPA_UNSUPPORTED)
            worker_decline (tlsf_dpa::reason (imported.status));
          else
            worker_stopped (tlsf_dpa::reason (imported.status));
          return EXIT_CODE_UNKNOWN;
        }
        input.automaton = std::move (imported.automaton);
        input.frame = parity_route::timing_frame::effective_mealy;
      }
      std::optional<legacy_phase> phase;
      const auto solve = oxidd ? parity_route::run_positive_dpa : parity_route::run;
      auto answer = solve (input, config, [&] (const char* name, const auto&, double s) {
        stage = name;
        if (s < 0)
          phase.emplace (name);
        else
          phase.reset ();
      });
      if (answer.verdict != "REALIZABLE" && answer.verdict != "UNREALIZABLE") {
        worker_stopped ("inconclusive");
        return EXIT_CODE_UNKNOWN;
      }
      const bool real = answer.verdict == "REALIZABLE";
      if (real && args.synth_fname) {
        legacy_phase export_phase ("parity_export");
        if (metadata.tlsf_target == "Moore")
          answer.circuit = synthesis::mealy_to_moore (
              answer.circuit, metadata.tlsf_semantics.find ("Moore") != std::string::npos);
        if (!synthesis::write_controller (*args.synth_fname, answer.circuit)) {
          worker_stopped ("controller_export_failed");
          return EXIT_CODE_UNKNOWN;
        }
      }
      return real ? EXIT_CODE_REAL : EXIT_CODE_UNREAL;
    } catch (const std::bad_alloc&) {
      if (std::strcmp (stage, "translate") == 0)
        worker_stopped ("translation_allocation");
      else
        worker_stopped ("allocation");
    } catch (const std::length_error&) {
      worker_stopped ("capacity");
    } catch (const std::exception& error) {
      const std::string message = error.what ();
      if (message.find ("too many acceptance sets") != std::string::npos ||
          message.find ("Too many acceptance sets") != std::string::npos ||
          message == "acceptance formula is too large")
        worker_decline ("acceptance_capacity");
      else
        worker_stopped ("parity_exception");
    } catch (...) {
      worker_stopped ("parity_exception");
    }
    return EXIT_CODE_UNKNOWN;
  }
}
