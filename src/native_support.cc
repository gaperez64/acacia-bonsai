#include "native_support.hh"

#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "native_proof_binding.hh"
#include "phase_records.hh"

#if ACACIA_NATIVE_ARMS
# include <algorithm>
# include <cerrno>
# include <cstdio>
# include <cstring>
# include <memory>
# include <string>
# include <sys/resource.h>
# include <unistd.h>

namespace acacia {
  native_failure_category native_failure (TlsfGr1LiftStatus status) {
    switch (status) {
      case TLSF_GR1_LIFT_LIMIT: return native_failure_category::resource;
      case TLSF_GR1_LIFT_UNSUPPORTED:
      case TLSF_GR1_LIFT_DECLINED: return native_failure_category::decline;
      case TLSF_GR1_LIFT_DEADLINE: return native_failure_category::deadline;
      case TLSF_GR1_LIFT_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (TlsfGr1ReductionStatus status) {
    switch (status) {
      case TLSF_GR1_REDUCE_LIMIT: return native_failure_category::resource;
      case TLSF_GR1_REDUCE_UNSUPPORTED:
      case TLSF_GR1_REDUCE_DECLINED: return native_failure_category::decline;
      case TLSF_GR1_REDUCE_DEADLINE: return native_failure_category::deadline;
      case TLSF_GR1_REDUCE_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (TlsfGr1CheckStatus status, TlsfGr1CheckVerdict verdict) {
    switch (status) {
      case TLSF_GR1_CHECK_LIMIT: return native_failure_category::resource;
      case TLSF_GR1_CHECK_OK:
        return verdict == TLSF_GR1_CHECK_CERT_FAILED || verdict == TLSF_GR1_CHECK_REFUTED
                   ? native_failure_category::decline
                   : native_failure_category::error;
      case TLSF_GR1_CHECK_DEADLINE: return native_failure_category::deadline;
      case TLSF_GR1_CHECK_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (TlsfPipelineStatus status) {
    switch (status) {
      case TLSF_PIPELINE_LIMIT: return native_failure_category::resource;
      case TLSF_PIPELINE_DECLINED: return native_failure_category::decline;
      default: return native_failure_category::error;
    }
  }

  native_failure_category native_failure (OxiddFailureKind status) {
    switch (status) {
      case OXIDD_FAILURE_BDD:
      case OXIDD_FAILURE_HOST:
      case OXIDD_FAILURE_ARTIFACT_LIMIT: return native_failure_category::resource;
      case OXIDD_FAILURE_DEADLINE: return native_failure_category::deadline;
      case OXIDD_FAILURE_CANCELLED: return native_failure_category::cancelled;
      default: return native_failure_category::error;
    }
  }

  const char* native_failure_reason (native_failure_category category) {
    switch (category) {
      case native_failure_category::decline: return "decline";
      case native_failure_category::resource: return "resource";
      case native_failure_category::deadline: return "deadline";
      case native_failure_category::cancelled: return "cancelled";
      case native_failure_category::error: return "error";
    }
    return "error";
  }

  void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                              std::string_view message, native_failure_category category) {
    if (auto* worker = active_worker_record ()) {
      // Preserve the obstruction even if the child exits before its caller can
      // publish a terminal packet. Messages remain on the incumbent stderr path.
      const std::string reason (stage);
      worker_stage (reason.c_str ());
      if (category == native_failure_category::decline) {
        if (!worker->stopped && std::strcmp (worker->reason, reason.c_str ()) != 0)
          worker_decline (reason.c_str ());
      }
      else if (!worker->stopped) {
        worker_stopped (native_failure_reason (category));
      }
    }
    const auto quote = [] (std::string_view value) {
      std::string out = "\"";
      for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\')
          out += '\\';
        if (ch < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[ch >> 4];
          out += hex[ch & 15];
        }
        else
          out += char (ch);
      }
      return out + '"';
    };
    const std::string line =
        std::string ("{\"arm\":") + quote (arm) + ",\"stage\":" + quote (stage) +
        ",\"status\":" + std::to_string (status) + ",\"message\":" + quote (message) + "}\n";
    // One write keeps concurrent children's JSON records intact on stderr.
    (void) ::write (STDERR_FILENO, line.data (), line.size ());
  }

  void native_diagnostic (std::string_view arm, std::string_view stage, int status,
                          std::string_view message, native_failure_category category) {
    native_arm_diagnostic (arm, stage, status, message, category);
  }

  bool native_limit_address_space (std::string_view arm) {
    rlimit address {};
    if (getrlimit (RLIMIT_AS, &address) != 0) {
      native_arm_diagnostic (arm, "memory", errno, "could not read address-space cap");
      return false;
    }
    constexpr rlim_t cap = rlim_t {8} * 1024 * 1024 * 1024;
    if (address.rlim_cur == RLIM_INFINITY || address.rlim_cur > cap) {
      address.rlim_cur = cap;
      if (setrlimit (RLIMIT_AS, &address) != 0) {
        native_arm_diagnostic (arm, "memory", errno, "could not set address-space cap");
        return false;
      }
    }
    return true;
  }

  TlsfGr1ConstructionBudget native_construction_budget (size_t arm_count) {
    constexpr uint64_t address_cap = uint64_t {8} * 1024 * 1024 * 1024;
    uint64_t invocation_limit = address_cap;
    // cgroup v2 exposes the current scope's path, not its memory.max at the
    // mount root. A missing or unbounded scope falls back to our RLIMIT_AS.
    if (FILE* membership = std::fopen ("/proc/self/cgroup", "r")) {
      char line[4096] {};
      if (std::fgets (line, sizeof line, membership)) {
        if (const char* group = std::strstr (line, "::/")) {
          std::string path = "/sys/fs/cgroup";
          path += group + 2;
          if (!path.empty () && path.back () == '\n')
            path.pop_back ();
          path += "/memory.max";
          if (FILE* file = std::fopen (path.c_str (), "r")) {
            unsigned long long scoped = 0;
            if (std::fscanf (file, "%llu", &scoped) == 1 && scoped > 0)
              invocation_limit = std::min (invocation_limit, uint64_t (scoped));
            std::fclose (file);
          }
        }
      }
      std::fclose (membership);
    }
    TlsfGr1ConstructionBudget budget {};
    budget.max_formula_nodes = 4096;
    budget.max_ap_count = 2048;
    budget.max_conjuncts = 1024;
    budget.max_conjunct_nodes = 1024;
    budget.max_temporal_depth = 16;
    budget.max_predicted_monitor_states = 1u << 24;
    budget.max_total_states = 10000;
    budget.max_total_edges = 150000;
    budget.max_rss_bytes = invocation_limit / std::max<size_t> (1, arm_count) / 2;
    return budget;
  }

  std::string native_budget_message (std::string_view stage, std::string_view reason,
                                     const TlsfGr1ConstructionWork& work) {
    if (stage.substr (0, 7) != "budget-" && stage != "allocation" && stage != "aag-size" &&
        stage != "publish-size" && stage != "metadata-size")
      return std::string (reason);
    return std::string (reason) + " formula_nodes=" + std::to_string (work.formula_nodes) +
           " ap=" + std::to_string (work.ap_count) +
           " conjuncts=" + std::to_string (work.conjuncts) +
           " max_conjunct_nodes=" + std::to_string (work.max_conjunct_nodes) +
           " temporal_depth=" + std::to_string (work.max_temporal_depth) +
           " predicted_states=" + std::to_string (work.predicted_monitor_states) +
           " monitors=" + std::to_string (work.monitors_completed) +
           " states=" + std::to_string (work.states) + " edges=" + std::to_string (work.edges) +
           " peak_rss_bytes=" + std::to_string (work.peak_rss_bytes);
  }

  void native_budget_record (std::string_view arm, std::string_view stage,
                             const TlsfGr1ConstructionWork& work, uint64_t started_ns) noexcept {
    if (!phase_records_enabled () ||
        (stage.substr (0, 7) != "budget-" && stage != "allocation" && stage != "aag-size" &&
         stage != "publish-size" && stage != "metadata-size"))
      return;
    char line[1024];
    const auto value = [] (uint64_t n) { return static_cast<unsigned long long> (n); };
    const int n =
        std::snprintf (line, sizeof line,
                       "{\"arm\":\"%.*s\",\"phase\":\"budget_decline\",\"stage\":\"%.*s\","
                       "\"elapsed_ns\":%llu,\"formula_nodes\":%llu,\"ap_count\":%llu,"
                       "\"conjuncts\":%llu,\"max_conjunct_nodes\":%llu,"
                       "\"max_temporal_depth\":%llu,\"predicted_monitor_states\":%llu,"
                       "\"monitors\":%llu,\"states\":%llu,\"edges\":%llu,"
                       "\"peak_rss_bytes\":%llu}\n",
                       int (arm.size ()), arm.data (), int (stage.size ()), stage.data (),
                       value (phase_clock (CLOCK_MONOTONIC) - started_ns),
                       value (work.formula_nodes), value (work.ap_count), value (work.conjuncts),
                       value (work.max_conjunct_nodes), value (work.max_temporal_depth),
                       value (work.predicted_monitor_states), value (work.monitors_completed),
                       value (work.states), value (work.edges), value (work.peak_rss_bytes));
    if (n > 0 && size_t (n) < sizeof line)
      phase_records_send (line, size_t (n));
  }
  namespace {
    void dual_record (const char* event, const char* outcome, const char* stage, int status,
                      const char* cause) noexcept {
      if (!phase_records_enabled ())
        return;
      char line[512];
      auto* worker = active_worker_record ();
      const int n =
          snprintf (line, sizeof line,
                    "{\"event\":\"dual_gr1_%s\",\"worker\":%u,\"outcome\":\"%s\","
                    "\"stage\":\"%s\",\"status\":%d,\"cause\":\"%s\",\"mono_ns\":%llu}\n",
                    event, worker ? worker->index : 0, outcome, stage, status, cause,
                    static_cast<unsigned long long> (phase_clock (CLOCK_MONOTONIC)));
      if (n > 0 && size_t (n) < sizeof line)
        phase_records_send (line, size_t (n));
      else
        phase_records_drop ();
    }
    struct dual_record_context {
        const char* arm;
        phase_stamp construction, reduction;
        bool constructed = false;
    };
    void dual_constructed (void* opaque, const char*, size_t size) {
      auto& context = *static_cast<dual_record_context*> (opaque);
      context.constructed = true;
      phase_finish (context.arm, "dual_construct", context.construction, -1, -1, 0, size);
      dual_record ("construction", "constructed", "dual-construct", 0, "none");
      worker_stage ("dual_reduce");
      context.reduction = phase_start ();
      if (auto* worker = active_worker_record ())
        worker_event (*worker, "route_start");
    }
  }

  void native_dual_gr1_after_rejection (const arg_parse_result& args, const char* arm,
                                        uint64_t deadline_mono_ns,
                                        const TlsfGr1ConstructionBudget& budget,
                                        std::string_view stage, native_failure_category cause) {
    if (!args.dual_gr1_recognize || stage != "mp-class" ||
        cause != native_failure_category::decline)
      return;
    dual_record ("original_rejection", "rejected", "mp-class", TLSF_GR1_REDUCE_UNSUPPORTED,
                 "decline");
    worker_route ("dual_gr1", "reduction");
    phase_scope load_phase (arm, "dual_load");
    TlsfPipelineError load_error {};
    TlsfPipelineOptions load_options {};
    load_options.certify = true;
    load_options.template_mask = TPL_ALL;
    load_options.source_sha256 = args.tlsf_sha256.c_str ();
    load_options.require_unambiguous_origin = true;
    load_options.error = &load_error;
    std::unique_ptr<TlsfPipeline, decltype (&tlsf_pipeline_free)> pipeline (
        tlsf_pipeline_load_bytes (reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()),
                                  args.tlsf_source.size (), &load_options),
        tlsf_pipeline_free);
    load_phase.finish ();
    if (!pipeline) {
      dual_record ("construction", "rejected", load_error.stage, int (load_error.status),
                   native_failure_reason (native_failure (load_error.status)));
      worker_stage (load_error.stage);
      if (native_failure (load_error.status) == native_failure_category::decline)
        worker_decline ("dual-construction");
      else
        worker_stopped (native_failure_reason (native_failure (load_error.status)));
      return;
    }
    TlsfGr1ReductionOptions options {};
    options.semantics = TLSF_GR1_EXACT;
    // Reuse incumbent global structural/state/artifact limits and the absolute
    // invocation deadline. No deadline reset, seed work, solve or proof API.
    options.deadline_mono_ns = deadline_mono_ns;
    options.max_artifact_bytes = 64u * 1024u * 1024u;
    options.max_monitor_states = 10000;
    options.budget = budget;
    TlsfGr1ReductionStats stats {};
    options.stats = &stats;
    native_result_owner<TlsfGr1DualRecognitionV1, tlsf_gr1_dual_recognition_clear_v1> result;
    TlsfGr1ReductionError error {};
    TlsfGr1ReductionStatus failure = TLSF_GR1_REDUCE_OK;
    dual_record_context context {arm, phase_start (), {}, false};
    const TlsfGr1DualObserverV1 observer {dual_constructed, &context};
    worker_stage ("dual_construct");
    auto status = tlsf_gr1_recognize_dual_v1 (pipeline.get (), &options, &observer, &result.value,
                                              &error, &failure);
    if (context.constructed)
      phase_finish (arm, "dual_reduce", context.reduction, -1, -1, stats.work.formula_nodes,
                    result.value.reduction.aag_size, nullptr, stats.monitor_count,
                    stats.monitor_states);
    else
      phase_finish (arm, "dual_construct", context.construction);
    if (status == TLSF_GR1_REDUCE_OK) {
      native_json_doc parsed (nullptr, yyjson_doc_free);
      auto& reduced = result.value.reduction;
      auto* metadata = native_json_object (reduced.metadata_json, reduced.metadata_size, parsed);
      if (!context.constructed || !reduced.game || !reduced.aag_size || !metadata ||
          !native_json_field (metadata, "orientation", "dual") ||
          !native_json_field (metadata, "semantics", "exact") ||
          !native_json_field (metadata, "source_sha256", args.tlsf_sha256) ||
          !native_sha256_matches (metadata, "game_sha256", reduced.aag, reduced.aag_size) ||
          !native_sha256_matches (metadata, "construction_sha256", result.value.construction_json,
                                  result.value.construction_size)) {
        status = failure = TLSF_GR1_REDUCE_ERROR;
        snprintf (error.stage, sizeof error.stage, "%s", "dual-binding");
      }
    }
    dual_record (
        context.constructed ? "reduction" : "construction",
        status == TLSF_GR1_REDUCE_OK ? "accepted" : "rejected", error.stage, int (status),
        status == TLSF_GR1_REDUCE_OK ? "none" : native_failure_reason (native_failure (failure)));
    worker_stage (error.stage);
    if (status == TLSF_GR1_REDUCE_OK)
      worker_decline ("dual-recognition-only");
    else if (native_failure (failure) == native_failure_category::decline)
      worker_decline (error.stage);
    else
      worker_stopped (native_failure_reason (native_failure (failure)));
  }

}
#endif
