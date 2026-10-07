#include "native_param_lift_arm.hh"

#include "arg_parser.hh"
#include "configuration.hh"
#include "error_msg.hh"
#include "native_proof_binding.hh"
#include "native_support.hh"
#include "phase_records.hh"
#ifdef ACACIA_NATIVE_TEST_HOOKS
# include "native_test_hooks.hh"
#endif

#if ACACIA_NATIVE_ARMS
# include <tlsf/gr1_lift.h>

# include <cstring>
# include <memory>

namespace acacia {
  struct lift_record_context {
      const char* arm;
      const TlsfGr1LiftStats* stats;
  };
  static void record_lift_stage (void* opaque, TlsfGr1LiftStatsStage stage,
                                 const TlsfGr1LiftStageStats* row) noexcept {
    constexpr const char* stages[] = {
        "lift_source",        "lift_target_reduce",   "lift_seed_window",
        "lift_seed_solve",    "lift_schema_learning", "lift_candidate_instantiation",
        "lift_policy_export", "lift_internal_check",  "lift_publish"};
    auto& context = *static_cast<lift_record_context*> (opaque);
    const auto& stats = *context.stats;
    const phase_memory memory {long (row->rss_kb),     long (row->peak_rss_kb),
                               size_t (row->arena),    size_t (row->hblkhd),
                               size_t (row->uordblks), size_t (row->fordblks)};
    const auto work = stage == TLSF_GR1_LIFT_STATS_SEED_WINDOW ? stats.seed_probes
                      : stage == TLSF_GR1_LIFT_STATS_SCHEMA_LEARNING ||
                              stage == TLSF_GR1_LIFT_STATS_CANDIDATE_INSTANTIATION ||
                              stage == TLSF_GR1_LIFT_STATS_POLICY_EXPORT
                          ? row->calls
                      : stage == TLSF_GR1_LIFT_STATS_SEED_SOLVE     ? 1ULL
                      : stage == TLSF_GR1_LIFT_STATS_INTERNAL_CHECK ? 1ULL
                                                                    : 0ULL;
    const auto bytes = stage == TLSF_GR1_LIFT_STATS_CANDIDATE_INSTANTIATION ? stats.candidate_bytes
                       : stage == TLSF_GR1_LIFT_STATS_POLICY_EXPORT         ? stats.policy_bytes
                                                                            : 0;
    const auto nodes = stage == TLSF_GR1_LIFT_STATS_SCHEMA_LEARNING
                           ? static_cast<long long> (stats.schema_nodes_after_learning)
                       : stage == TLSF_GR1_LIFT_STATS_CANDIDATE_INSTANTIATION
                           ? static_cast<long long> (stats.schema_nodes_after_candidate)
                       : stage == TLSF_GR1_LIFT_STATS_INTERNAL_CHECK
                           ? static_cast<long long> (stats.internal_check_peak_nodes)
                           : -1;
    phase_finish (context.arm, stages[stage],
                  {phase_clock (CLOCK_MONOTONIC) - row->wall_ns,
                   phase_clock (CLOCK_PROCESS_CPUTIME_ID) - row->cpu_ns},
                  nodes, -1, work, bytes, &memory,
                  stage == TLSF_GR1_LIFT_STATS_SEED_WINDOW ? stats.monitor_count_total : 0,
                  stage == TLSF_GR1_LIFT_STATS_SEED_WINDOW ? stats.monitor_states_total : 0);
  }
  int run_native_param_lift_arm (const arg_parse_result& args, uint64_t deadline_mono_ns) {
    constexpr const char* arm = "real:param-lift:oxidd";
    if (!native_limit_address_space (arm))
      return EXIT_CODE_UNKNOWN;
    const uint64_t construction_started_ns = phase_clock (CLOCK_MONOTONIC);

    TlsfGr1LiftOptions options {};
    options.deadline_mono_ns = deadline_mono_ns;
    const auto budget = native_construction_budget (args.arms ? args.arms->size () : 1);
    const TlsfGr1StructureGuardOptionsV1 structure_guard {args.native_structure_guard_scale};
    TlsfGr1LiftStats stats {};
    options.budget = budget;
    options.proof_order = TLSF_GR1_LIFT_REGION_FIRST;
    options.stats = &stats;
    options.stats_callback = phase_records_enabled () ? record_lift_stage : nullptr;
    lift_record_context lift_context {arm, &stats};
    options.stats_context = &lift_context;
# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_options (options);
# endif
    // All remaining caps are the bounded defaults of the native lifting API.
    native_lift_owner lifted;
    TlsfGr1LiftError lift_error {};
    worker_route ("R", "oxidd", "REAL");
    phase_scope lift_phase (arm, "lift_call");
    const auto* source = reinterpret_cast<const uint8_t*> (args.tlsf_source.data ());
    TlsfGr1LiftTarget* raw_target = nullptr;
    TlsfGr1LiftStatus failure_status = TLSF_GR1_LIFT_OK;
    auto lift_status = tlsf_gr1_lift_target_prepare_v2 (source, args.tlsf_source.size (), nullptr,
                                                        0, &options, &structure_guard, &raw_target,
                                                        &lift_error, &failure_status);
    std::unique_ptr<TlsfGr1LiftTarget, decltype (&tlsf_gr1_lift_target_free)> target (
        raw_target, tlsf_gr1_lift_target_free);
    if (lift_status == TLSF_GR1_LIFT_OK)
      lift_status = tlsf_gr1_lift_from_target_v2 (target.get (), &options, &structure_guard,
                                                  &lifted.value, &lift_error, &failure_status);
    lift_phase.finish ();
    const auto& work = stats.work;
    if (phase_records_enabled ()) {
      if (stats.final_stage[0]) {
        const std::string decline_phase = std::string ("lift_decline_") + stats.final_stage;
        phase_finish (arm, decline_phase.c_str (), phase_start (), -1, -1,
                      stats.seed_probes + stats.seed_solves + work.formula_nodes, 0, nullptr,
                      work.monitors_completed, work.states);
      }
    }
    if (lift_status != TLSF_GR1_LIFT_OK) {
      native_budget_record (arm, lift_error.stage, work, construction_started_ns);
      native_arm_diagnostic (arm, lift_error.stage, int (lift_status),
                             native_budget_message (lift_error.stage, lift_error.message, work),
                             native_failure (failure_status));
      return EXIT_CODE_UNKNOWN;
    }

# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_fault (lifted.value);
# endif
    const auto& result = lifted.value;
    phase_scope binding_phase (arm, "proof_binding");
    const bool certificate_method =
        result.method == TLSF_GR1_CHECK_CERTIFICATE && result.verdict == TLSF_GR1_CHECK_VERIFIED;
    const bool region_method =
        result.method == TLSF_GR1_CHECK_REGION && result.verdict == TLSF_GR1_CHECK_REGION_VERIFIED;
    if ((!certificate_method && !region_method) ||
        (result.semantics != TLSF_GR1_EXACT && result.semantics != TLSF_GR1_STRICT) ||
        !result.game_aag || !result.game_size || !result.certificate_aag ||
        !result.certificate_size || !result.certificate_json || !result.certificate_json_size ||
        (certificate_method && (!result.policy_aag || !result.policy_size || !result.policy_json ||
                                !result.policy_json_size)) ||
        !result.evidence_json || !result.evidence_size) {
      native_arm_diagnostic (arm, "artifact", -1, "incomplete or unverified system proof");
      return EXIT_CODE_UNKNOWN;
    }
    if (phase_records_enabled ())
      phase_finish (arm, certificate_method ? "lift_method_certificate" : "lift_method_region",
                    phase_start ());
    native_json_doc parsed_evidence (nullptr, yyjson_doc_free);
    auto* evidence =
        native_json_object (result.evidence_json, result.evidence_size, parsed_evidence);
    if (!evidence || !native_json_field (evidence, "format", TLSF_GR1_LIFT_EVIDENCE_FORMAT)) {
      native_arm_diagnostic (arm, "evidence", -1, "invalid lift evidence JSON");
      return EXIT_CODE_UNKNOWN;
    }
    if (args.tlsf_sha256.size () != 64 ||
        !native_sha256_matches (evidence, "source_sha256", args.tlsf_source.data (),
                                args.tlsf_source.size ()) ||
        !native_json_field (evidence, "source_sha256", args.tlsf_sha256)) {
      native_arm_diagnostic (arm, "source", -1, "snapshot hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    if (!native_sha256_matches (evidence, "game_sha256", result.game_aag, result.game_size)) {
      native_arm_diagnostic (arm, "game", -1, "game artifact hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    if (!native_sha256_matches (evidence, "certificate_sha256", result.certificate_aag,
                                result.certificate_size)) {
      native_arm_diagnostic (arm, "certificate", -1, "certificate artifact hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    if (!native_lift_policy_hash_matches (evidence, certificate_method, result.policy_aag,
                                          result.policy_size)) {
      native_arm_diagnostic (arm, "policy", -1, "policy artifact hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    const std::string_view semantics = result.semantics == TLSF_GR1_EXACT ? "exact" : "strict";
    const std::string_view method = certificate_method ? "certificate" : "gr1-region-v1";
    const std::string_view verdict = certificate_method ? "VERIFIED" : "REGION_VERIFIED";
    if (!native_json_field (evidence, "reduction_semantics", semantics) ||
        !native_json_field (evidence, "method", method) ||
        !native_json_field (evidence, "verdict", verdict) ||
        !native_json_field (evidence, "move_source", "target_transition") ||
        !native_proof_sidecars (result.certificate_json, result.certificate_json_size,
                                result.policy_json, result.policy_json_size, semantics,
                                certificate_method)) {
      native_arm_diagnostic (arm, "metadata", -1, "proof side, method or semantics mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    binding_phase.finish ();
    if (!tlsf_gr1_lift_target_matches (target.get (), &result)) {
      native_arm_diagnostic (arm, "source_game", -1,
                             "checked game differs from snapshot reduction");
      return EXIT_CODE_UNKNOWN;
    }
    worker_verified ("REAL", "REAL");
    return EXIT_CODE_REAL;
  }
}
#endif
