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
  int run_native_gr1_lift_arm (const arg_parse_result& args, uint64_t deadline_mono_ns) {
    constexpr const char* arm = "both:gr1-lift:oxidd";
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
    options.stats = &stats;
# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_options (options);
# endif
    TlsfGr1LiftError error {};
    TlsfGr1LiftTarget* raw_target = nullptr;
    TlsfGr1LiftOptions prepare_options = options;
    prepare_options.max_artifact_bytes = 64u * 1024u * 1024u;
    phase_scope prepare_phase (arm, "trusted_prepare");
    auto status = tlsf_gr1_lift_target_prepare_exact (
        reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()), args.tlsf_source.size (),
        &prepare_options, &raw_target, &error);
    std::unique_ptr<TlsfGr1LiftTarget, decltype (&tlsf_gr1_lift_target_free)> target (
        raw_target, tlsf_gr1_lift_target_free);
    prepare_phase.finish ();
    if (status != TLSF_GR1_LIFT_OK) {
      native_budget_record (arm, error.stage, stats.work, construction_started_ns);
      native_arm_diagnostic (arm, error.stage, int (status),
                             native_budget_message (error.stage, error.message, stats.work));
      return EXIT_CODE_UNKNOWN;
    }

    TlsfGr1BothResult result {};
    struct ResultGuard {
        TlsfGr1BothResult& value;
        ~ResultGuard () { tlsf_gr1_both_result_clear (&value); }
    } guard {result};
    phase_scope combined_phase (arm, "combined_call");
    status = tlsf_gr1_both_from_target (target.get (), &options, &result, &error);
    combined_phase.finish ();
    if (phase_records_enabled ()) {
      const char* route = result.route == TLSF_GR1_BOTH_REAL_LIFT  ? "route_R"
                          : result.route == TLSF_GR1_BOTH_ENV_LIFT ? "route_U"
                                                                   : "route_direct";
      const char* polarity = result.seed_polarity == TLSF_GR1_SEEDS_REAL      ? "seed_REAL"
                             : result.seed_polarity == TLSF_GR1_SEEDS_UNREAL  ? "seed_UNREAL"
                             : result.seed_polarity == TLSF_GR1_SEEDS_MIXED   ? "seed_mixed"
                             : result.seed_polarity == TLSF_GR1_SEEDS_UNKNOWN ? "seed_unknown"
                                                                              : "seed_none";
      phase_finish (arm, route, phase_start ());
      phase_finish (arm, polarity, phase_start (), -1, -1, result.seed_solves);
      phase_finish (arm, "seed_cache", phase_start (), -1, -1,
                    result.seed_probes + result.seed_reductions + result.seed_solves +
                        result.seed_checks + result.seed_cache_hits);
      phase_finish (arm, "final_checks", phase_start (), -1, -1, result.target_checks);
      phase_finish (arm, "target_reductions", phase_start (), -1, -1, result.target_reductions);
      const std::string declines = result.decline_stages;
      for (size_t begin = 0; begin < declines.size ();) {
        const size_t end = declines.find (',', begin);
        const std::string stage = "decline_" + declines.substr (begin, end - begin);
        phase_finish (arm, stage.c_str (), phase_start ());
        if (end == std::string::npos)
          break;
        begin = end + 1;
      }
    }
    if (status != TLSF_GR1_LIFT_OK) {
      native_budget_record (arm, error.stage, stats.work, construction_started_ns);
      native_arm_diagnostic (arm, error.stage, int (status),
                             native_budget_message (error.stage, error.message, stats.work));
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
    return unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL;
  }
}
#endif
