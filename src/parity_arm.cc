#include "parity_arm.hh"

#include "phase_records.hh"
#include "solver/accounting_identity.hh"
#include "solver/controller_writer.hh"
#include "solver/mealy_to_moore.hh"
#include "solver/obligation_helper.hh"
#include "solver/parity/route.hh"
#include "solver/synthesis_rendezvous.hh"

#include <optional>
#include <spot/tl/parse.hh>
#include <stdexcept>

namespace acacia {
  namespace {
    void record (const char* event, unsigned tier, const parity::statistics& stats = {}) {
      if (!phase_records_enabled ())
        return;
      auto* worker = active_worker_record ();
      if (!worker)
        return;
      char line[768];
      const int length = snprintf (
          line, sizeof line,
          "{\"arm\":\"both:parity:spot\",\"event\":\"%s\",\"worker\":%u,"
          "\"worker_pid\":%ld,\"seq\":%llu,\"tier\":%u,\"k\":%u,\"t\":%u,"
          "\"pairs\":%u,\"fallback\":%s,\"fallback_reason\":\"%s\","
          "\"mono_ns\":%llu,\"trust\":\"exact_game\",\"same_library_checked\":false,"
          "\"independent_checked\":false}\n",
          event, worker->index, long (worker->pid), ++worker->sequence, tier, stats.k, stats.t,
          stats.pairs, stats.fallback ? "true" : "false", stats.fallback_reason.c_str (),
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

  int run_parity_arm (const arg_parse_result& args) {
    worker_route ("original_game", "parity", "unknown");
    worker_stage ("parity_preprocessing");
    const char* stage = "preprocessing";
    try {
      if (!obligation::limit_address_space ()) {
        worker_stopped ("address_space_limit");
        return EXIT_CODE_UNKNOWN;
      }
      std::string text = args.formula;
      auto inputs = args.inputs, outputs = args.outputs;
      auto metadata = args.metadata;
      if (args.tlsf_specified) {
        auto spec = tlsf_frontend::parse (args.tlsf_source);
        text = std::move (spec.formula);
        inputs = std::move (spec.inputs);
        outputs = std::move (spec.outputs);
        metadata = std::move (spec.metadata);
      }
      validate_ltl_partition (inputs, outputs);
      const auto parsed = spot::parse_infix_psl (text);
      if (!parsed.f || !parsed.errors.empty ()) {
        worker_stopped ("invalid_formula");
        return EXIT_CODE_UNKNOWN;
      }
      // The frontend already lowers TLSF to effective Mealy. Translate the
      // original positive objective, without simplification, swapping or another X.
      parity_route::request input;
      input.formula = parsed.f;
      input.inputs = std::move (inputs);
      input.outputs = std::move (outputs);
      if (phase_records_enabled ())
        accounting_identity (
            input.formula, input.inputs, input.outputs, "spot-parity",
            "Generic:Deterministic:High:simul=0:ba-simul=0:det-simul=0:tls-impl=1:wdba-minimize=2",
            "parity-route");
      parity_route::options config;
      config.synthesis = args.synth_fname.has_value ();
      unsigned tier = 0;
      config.on_classified = [&] (unsigned value, const std::string&) {
        tier = value;
        worker_route (value < 4 ? "deterministic_buchi" : "acd_parity",
                      value < 4 ? "spot-fast" : "strpm-simd", "unknown");
        record ("parity_tier", tier);
      };
      config.bounds.parameter_observer = [&] (const auto& stats) {
        record ("parity_parameters", tier, stats);
      };
      config.bounds.fallback_observer = [&] (const auto& stats, double seconds) {
        record (seconds < 0 ? "parity_fallback_start" : "parity_fallback_end", tier, stats);
      };
      config.on_solved = [&] (const auto& stats) { record ("parity_solved", tier, stats); };
      config.on_verdict = [&] (bool real) {
        if (auto* worker = active_worker_record ())
          worker_record_text (worker->proof_polarity, real ? "REAL" : "UNREAL");
        record ("parity_verdict", tier);
        if (args.synth_fname)
          synthesis::await_selection (real ? EXIT_CODE_REAL : EXIT_CODE_UNREAL);
      };
      std::optional<legacy_phase> phase;
      auto answer =
          parity_route::run (input, config, [&] (const char* name, const auto&, double s) {
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
