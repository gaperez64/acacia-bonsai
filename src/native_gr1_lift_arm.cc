#include "native_gr1_lift_arm.hh"

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
# include <string>

namespace acacia {
  namespace {
    void record_combined_event (void*, TlsfGr1BothEventKind kind, TlsfGr1BothEventRoute route,
                                int proof_unreal, const char* stage, const char*,
                                TlsfGr1LiftStatus failure) {
      auto* worker = active_worker_record ();
      if (!worker)
        return;
      const char* name = route == TLSF_GR1_BOTH_EVENT_R        ? "R"
                         : route == TLSF_GR1_BOTH_EVENT_U      ? "U"
                         : route == TLSF_GR1_BOTH_EVENT_DIRECT ? "direct"
                                                               : "seed_discovery";
      const char* side = proof_unreal < 0 ? "unknown" : proof_unreal ? "UNREAL" : "REAL";
      if (kind == TLSF_GR1_BOTH_EVENT_SELECTED) {
        worker_select_route (name, "oxidd", side);
      }
      else if (kind == TLSF_GR1_BOTH_EVENT_START) {
        worker_event (*worker, "route_start");
      }
      else if (kind == TLSF_GR1_BOTH_EVENT_CHECK_START) {
        worker_stage (stage);
        worker_record_text (worker->original_polarity, side);
        worker_record_text (worker->proof_polarity, side);
        worker_event (*worker, "verification_start");
      }
      else if (kind == TLSF_GR1_BOTH_EVENT_VERIFIED) {
        worker_event (*worker, "check_complete");
      }
      else if (kind == TLSF_GR1_BOTH_EVENT_DECLINE) {
        worker_stage (stage);
        worker_decline (stage);
      }
      else if (kind == TLSF_GR1_BOTH_EVENT_STOPPED) {
        worker_stage (stage);
        worker_stopped (native_failure_reason (native_failure (failure)));
      }
# ifdef ACACIA_NATIVE_TEST_HOOKS
      native_attribution_test_pause (kind, name);
# endif
    }
  }
  int run_native_gr1_lift_arm (const arg_parse_result& args, uint64_t deadline_mono_ns,
                               bool real_only) {
    const char* arm = real_only ? "both:gr1-real-lift:oxidd" : "both:gr1-lift:oxidd";
    if (!native_limit_address_space (arm))
      return EXIT_CODE_UNKNOWN;
    const uint64_t construction_started_ns = phase_clock (CLOCK_MONOTONIC);
    const auto budget = native_construction_budget (args.arms ? args.arms->size () : 1);
    TlsfGr1LiftStats stats {};
    TlsfGr1LiftOptions options {};
    options.deadline_mono_ns = deadline_mono_ns;
    options.budget = budget;
    options.env_budget.max_rss_bytes = budget.max_rss_bytes;
    options.proof_order = TLSF_GR1_LIFT_REGION_FIRST;
    options.env_candidate_ns = 12000000000ull;
    options.disable_env_lift = real_only;
    options.stats = &stats;
# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_options (options);
# endif
    TlsfGr1LiftError error {};
    TlsfGr1LiftTarget* raw_target = nullptr;
    TlsfGr1LiftOptions prepare_options = options;
    prepare_options.max_artifact_bytes = 64u * 1024u * 1024u;
    worker_route ("trusted_prepare", "oxidd");
    phase_scope prepare_phase (arm, "trusted_prepare");
    TlsfGr1LiftStatus preparation_cause = TLSF_GR1_LIFT_OK;
    auto status = tlsf_gr1_lift_target_prepare_exact_v1 (
        reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()), args.tlsf_source.size (),
        &prepare_options, &raw_target, &error, &preparation_cause);
    std::unique_ptr<TlsfGr1LiftTarget, decltype (&tlsf_gr1_lift_target_free)> target (
        raw_target, tlsf_gr1_lift_target_free);
    prepare_phase.finish ();
    if (status != TLSF_GR1_LIFT_OK) {
      native_budget_record (arm, error.stage, stats.work, construction_started_ns);
      native_arm_diagnostic (arm, error.stage, int (status),
                             native_budget_message (error.stage, error.message, stats.work),
                             native_failure (preparation_cause));
      return EXIT_CODE_UNKNOWN;
    }

    TlsfGr1BothResult result {};
    struct ResultGuard {
        TlsfGr1BothResult& value;
        ~ResultGuard () { tlsf_gr1_both_result_clear (&value); }
    } guard {result};
    phase_scope combined_phase (arm, "combined_call");
    const TlsfGr1BothObserverV1 observer {record_combined_event, nullptr};
    status =
        phase_records_enabled ()
            ? tlsf_gr1_both_from_target_v1 (target.get (), &options, &observer, &result, &error)
            : tlsf_gr1_both_from_target (target.get (), &options, &result, &error);
    combined_phase.finish ();
    if (real_only && result.route == TLSF_GR1_BOTH_ENV_LIFT) {
      native_arm_diagnostic (arm, "route", -1, "R-only arm selected U");
      return EXIT_CODE_UNKNOWN;
    }
    if (phase_records_enabled ()) {
      const char* polarity = result.seed_polarity == TLSF_GR1_SEEDS_REAL      ? "seed_REAL"
                             : result.seed_polarity == TLSF_GR1_SEEDS_UNREAL  ? "seed_UNREAL"
                             : result.seed_polarity == TLSF_GR1_SEEDS_MIXED   ? "seed_mixed"
                             : result.seed_polarity == TLSF_GR1_SEEDS_UNKNOWN ? "seed_unknown"
                                                                              : "seed_none";
      phase_finish (arm, polarity, phase_start (), -1, -1, result.seed_solves);
      phase_finish (arm, "seed_cache", phase_start (), -1, -1,
                    result.seed_probes + result.seed_reductions + result.seed_solves +
                        result.seed_checks + result.seed_cache_hits);
      phase_finish (arm, "final_checks", phase_start (), -1, -1, result.target_checks);
      phase_finish (arm, "target_reductions", phase_start (), -1, -1, result.target_reductions);
    }
    if (status != TLSF_GR1_LIFT_OK) {
      native_budget_record (arm, error.stage, stats.work, construction_started_ns);
      native_arm_diagnostic (arm, error.stage, int (status),
                             native_budget_message (error.stage, error.message, stats.work),
                             native_failure (status));
      return EXIT_CODE_UNKNOWN;
    }
# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_fault (result.proof);
# endif
    const auto& proof = result.proof;
    phase_scope binding_phase (arm, "proof_binding");
    const bool region =
        proof.method == TLSF_GR1_CHECK_REGION && proof.verdict == TLSF_GR1_CHECK_REGION_VERIFIED;
    const bool certificate =
        proof.method == TLSF_GR1_CHECK_CERTIFICATE && proof.verdict == TLSF_GR1_CHECK_VERIFIED;
    if ((!region && !certificate) || proof.semantics != TLSF_GR1_EXACT || !proof.game_aag ||
        !proof.game_size || !proof.certificate_aag || !proof.certificate_size ||
        !proof.certificate_json || !proof.certificate_json_size ||
        (certificate && (!proof.policy_aag || !proof.policy_size || !proof.policy_json ||
                         !proof.policy_json_size)) ||
        !proof.evidence_json || !proof.evidence_size || result.target_checks < 1 ||
        (result.route != TLSF_GR1_BOTH_DIRECT && result.target_checks != 1)) {
      native_arm_diagnostic (arm, "artifact", -1, "incomplete checked proof");
      return EXIT_CODE_UNKNOWN;
    }
    native_json_doc parsed (nullptr, yyjson_doc_free);
    auto* evidence = native_json_object (proof.evidence_json, proof.evidence_size, parsed);
    bool unreal = certificate && result.route == TLSF_GR1_BOTH_ENV_LIFT;
    const std::string_view route = result.route == TLSF_GR1_BOTH_REAL_LIFT  ? "R"
                                   : result.route == TLSF_GR1_BOTH_ENV_LIFT ? "U"
                                                                            : "direct";
    if (certificate && result.route == TLSF_GR1_BOTH_DIRECT) {
      native_json_doc cert_doc (nullptr, yyjson_doc_free);
      auto* cert =
          native_json_object (proof.certificate_json, proof.certificate_json_size, cert_doc);
      if (!cert)
        return EXIT_CODE_UNKNOWN;
      auto* side = yyjson_obj_get (cert, "side");
      if (!yyjson_is_str (side))
        return EXIT_CODE_UNKNOWN;
      unreal = std::string_view (yyjson_get_str (side)) == "environment";
    }
    if (!evidence || !native_json_field (evidence, "format", TLSF_GR1_LIFT_EVIDENCE_FORMAT) ||
        !native_json_field (evidence, "route", route) ||
        !native_json_field (evidence, "reduction_semantics", "exact") ||
        !native_json_field (evidence, "method", region ? "gr1-region-v1" : "certificate") ||
        !native_json_field (evidence, "verdict", region ? "REGION_VERIFIED" : "VERIFIED") ||
        !native_json_field (evidence, "move_source", "target_transition") ||
        !native_sha256_matches (evidence, "source_sha256", args.tlsf_source.data (),
                                args.tlsf_source.size ()) ||
        !native_json_field (evidence, "source_sha256", args.tlsf_sha256) ||
        !native_sha256_matches (evidence, "game_sha256", proof.game_aag, proof.game_size) ||
        !native_sha256_matches (evidence, "certificate_sha256", proof.certificate_aag,
                                proof.certificate_size) ||
        !native_lift_policy_hash_matches (evidence, certificate, proof.policy_aag,
                                          proof.policy_size) ||
        !native_proof_sidecars (proof.certificate_json, proof.certificate_json_size,
                                proof.policy_json, proof.policy_json_size, "exact", certificate,
                                unreal) ||
        !tlsf_gr1_lift_target_matches (target.get (), &proof)) {
      native_arm_diagnostic (arm, "binding", -1, "checked proof binding mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    binding_phase.finish ();
    worker_verified (unreal ? "UNREAL" : "REAL", unreal ? "UNREAL" : "REAL");
    return unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL;
  }
}
#endif
