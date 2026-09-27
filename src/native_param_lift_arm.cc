#include "configuration.hh"
#include "native_param_lift_arm.hh"
#include "arg_parser.hh"
#include "error_msg.hh"
#include "native_proof_binding.hh"
#include "native_support.hh"
#ifdef ACACIA_NATIVE_TEST_HOOKS
#include "native_test_hooks.hh"
#endif

#if ACACIA_NATIVE_ARMS
#include <cstring>
#include <memory>
#include <tlsf/gr1_lift.h>

namespace acacia {
  int run_native_param_lift_arm (const arg_parse_result& args, uint64_t deadline_mono_ns) {
    constexpr std::string_view arm = "real:param-lift:oxidd";
    if (!native_limit_address_space (arm))
      return EXIT_CODE_UNKNOWN;

    TlsfGr1LiftOptions options {};
    options.deadline_mono_ns = deadline_mono_ns;
# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_options (options);
# endif
    // All remaining caps are the bounded defaults of the native lifting API.
    native_lift_owner lifted;
    TlsfGr1LiftError lift_error {};
    const auto lift_status =
        tlsf_gr1_lift (reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()),
                       args.tlsf_source.size (), nullptr, 0, &options, &lifted.value, &lift_error);
    if (lift_status != TLSF_GR1_LIFT_OK) {
      native_arm_diagnostic (arm, lift_error.stage, int (lift_status), lift_error.message);
      return EXIT_CODE_UNKNOWN;
    }

# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_fault (lifted.value);
# endif
    const auto& result = lifted.value;
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
    // The lift result returns game bytes but no source-bound target-game handle.
    // Recompute the target reduction from this immutable snapshot before check.
    TlsfPipelineError pipeline_error {};
    TlsfPipelineOptions pipeline_options {};
    pipeline_options.certify = true;
    pipeline_options.template_mask = TPL_ALL;
    pipeline_options.require_unambiguous_origin = true;
    pipeline_options.error = &pipeline_error;
    std::unique_ptr<TlsfPipeline, decltype (&tlsf_pipeline_free)> pipeline (
        tlsf_pipeline_load_bytes (reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()),
                                  args.tlsf_source.size (), &pipeline_options),
        tlsf_pipeline_free);
    if (!pipeline || args.tlsf_sha256 != pipeline->source_sha256) {
      native_arm_diagnostic (arm, "source", -1, "could not bind target reduction to snapshot");
      return EXIT_CODE_UNKNOWN;
    }
    TlsfGr1ReductionOptions reduction_options {};
    reduction_options.semantics = result.semantics;
    reduction_options.deadline_mono_ns = deadline_mono_ns;
    reduction_options.max_artifact_bytes = TLSF_GR1_LIFT_DEFAULT_MAX_ARTIFACT_BYTES;
    reduction_options.max_monitor_states = TLSF_GR1_LIFT_DEFAULT_MAX_MONITOR_STATES;
    native_reduction_owner target;
    TlsfGr1ReductionError reduction_error {};
    if (tlsf_gr1_reduce (pipeline.get (), &reduction_options, &target.value, &reduction_error) !=
            TLSF_GR1_REDUCE_OK ||
        target.value.aag_size != result.game_size || !target.value.aag ||
        std::memcmp (target.value.aag, result.game_aag, result.game_size) != 0) {
      native_arm_diagnostic (arm, "source_game", -1,
                             "checked game differs from snapshot reduction");
      return EXIT_CODE_UNKNOWN;
    }
    tlsf_gr1_reduction_clear (&target.value);
    pipeline.reset ();
    const auto span = [] (const char* data, size_t size) -> TlsfGr1Bytes {
      return {reinterpret_cast<const uint8_t*> (data), size};
    };
    TlsfGr1CheckInput input {};
    input.game_aag = span (result.game_aag, result.game_size);
    input.certificate_aag = span (result.certificate_aag, result.certificate_size);
    input.certificate_json = span (result.certificate_json, result.certificate_json_size);
    if (certificate_method) {
      input.policy_aag = span (result.policy_aag, result.policy_size);
      input.policy_json = span (result.policy_json, result.policy_json_size);
    }
    TlsfGr1CheckOptions check_options {};
    check_options.method = result.method;
    check_options.node_cap = TLSF_GR1_LIFT_DEFAULT_CHECKER_NODES;
    check_options.cache_cap = TLSF_GR1_LIFT_DEFAULT_CHECKER_CACHE;
    check_options.max_artifact_bytes = TLSF_GR1_LIFT_DEFAULT_MAX_ARTIFACT_BYTES;
    check_options.deadline_mono_ns = deadline_mono_ns;
    native_check_owner checked;
    const auto check_status = tlsf_gr1_check (&input, &check_options, &checked.value);
    if (check_status != TLSF_GR1_CHECK_OK || checked.value.verdict != result.verdict) {
      native_arm_diagnostic (
          arm, checked.value.stage[0] ? checked.value.stage : "check",
          check_status == TLSF_GR1_CHECK_OK ? int (checked.value.verdict) : int (check_status),
          checked.value.message[0] ? checked.value.message : "proof not verified");
      return EXIT_CODE_UNKNOWN;
    }
    return EXIT_CODE_REAL;
  }
}
#endif
