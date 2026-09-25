#pragma once

#include "native_gr1_arm.hh"
#include "native_proof_binding.hh"

#if ACACIA_NATIVE_ARMS
# include <tlsf/gr1_lift.h>
# include <cstring>

namespace acacia {
#ifdef ACACIA_NATIVE_TEST_HOOKS
  inline void native_lift_test_fault (TlsfGr1LiftResult& result) {
    const char* fault = std::getenv ("ACACIA_NATIVE_TEST_LIFT_FAULT");
    if (!fault) return;
    const auto replace_field = [] (char*& bytes, size_t& size, const char* field,
                                    const char* value) {
      boost::json::value parsed;
      if (native_json_object (bytes, size, parsed)) {
        if (value) parsed.as_object ()[field] = value;
        else parsed.as_object ().erase (field);
        const std::string changed = boost::json::serialize (parsed);
        char* replacement = static_cast<char*> (std::malloc (changed.size () + 1));
        if (!replacement) return;
        std::memcpy (replacement, changed.c_str (), changed.size () + 1);
        std::free (bytes);
        bytes = replacement;
        size = changed.size ();
      }
    };
    if (std::strcmp (fault, "source-hash") == 0)
      replace_field (result.evidence_json, result.evidence_size, "source_sha256",
                     "0000000000000000000000000000000000000000000000000000000000000000");
    else if (std::strcmp (fault, "swap-game") == 0) {
      std::swap (result.game_aag, result.certificate_aag);
      std::swap (result.game_size, result.certificate_size);
    }
    else if (std::strcmp (fault, "swapped-game-with-matching-hash") == 0) {
      std::swap (result.game_aag, result.certificate_aag);
      std::swap (result.game_size, result.certificate_size);
      char hash[65]{};
      if (tlsf_pipeline_source_sha256 (result.game_aag, result.game_size, hash))
        replace_field (result.evidence_json, result.evidence_size, "game_sha256", hash);
      if (tlsf_pipeline_source_sha256 (result.certificate_aag,
                                       result.certificate_size, hash))
        replace_field (result.evidence_json, result.evidence_size,
                       "certificate_sha256", hash);
    }
    else if (std::strcmp (fault, "swap-certificate") == 0) {
      std::swap (result.certificate_aag, result.policy_aag);
      std::swap (result.certificate_size, result.policy_size);
    }
    else if (std::strcmp (fault, "wrong-side") == 0)
      replace_field (result.certificate_json, result.certificate_json_size,
                     "side", "environment");
    else if (std::strcmp (fault, "wrong-method") == 0)
      replace_field (result.evidence_json, result.evidence_size, "method",
                     result.method == TLSF_GR1_CHECK_CERTIFICATE
                         ? "gr1-region-v1" : "certificate");
    else if (std::strcmp (fault, "missing-policy-hash") == 0)
      replace_field (result.evidence_json, result.evidence_size, "policy_sha256", nullptr);
    else if (std::strcmp (fault, "policy-hash") == 0)
      replace_field (result.evidence_json, result.evidence_size, "policy_sha256",
                     "0000000000000000000000000000000000000000000000000000000000000000");
    else if (std::strcmp (fault, "unexpected-region-policy-hash") == 0)
      replace_field (result.evidence_json, result.evidence_size, "policy_sha256",
                     "0000000000000000000000000000000000000000000000000000000000000000");
    else if (std::strcmp (fault, "corrupt-proof") == 0) {
      result.certificate_aag[0] = 'X';
      char hash[65]{};
      if (tlsf_pipeline_source_sha256 (result.certificate_aag,
                                       result.certificate_size, hash))
        replace_field (result.evidence_json, result.evidence_size,
                       "certificate_sha256", hash);
    }
  }
#endif

  inline int run_native_param_lift_arm (const arg_parse_result& args,
                                        uint64_t deadline_mono_ns) {
    constexpr std::string_view arm = "real:param-lift:oxidd";
    rlimit address{};
    if (getrlimit (RLIMIT_AS, &address) != 0) {
      native_arm_diagnostic (arm, "memory", errno, "could not read address-space cap");
      return EXIT_CODE_UNKNOWN;
    }
    constexpr rlim_t cap = rlim_t {8} * 1024 * 1024 * 1024;
    if (address.rlim_cur == RLIM_INFINITY || address.rlim_cur > cap) {
      address.rlim_cur = cap;
      if (setrlimit (RLIMIT_AS, &address) != 0) {
        native_arm_diagnostic (arm, "memory", errno, "could not set address-space cap");
        return EXIT_CODE_UNKNOWN;
      }
    }

    TlsfGr1LiftOptions options{};
    options.abi_version = TLSF_GR1_LIFT_ABI_VERSION;
    options.struct_size = sizeof options;
    options.deadline_mono_ns = deadline_mono_ns;
#ifdef ACACIA_NATIVE_TEST_HOOKS
    if (const char* fault = std::getenv ("ACACIA_NATIVE_TEST_LIFT_FAULT"))
      if (std::strcmp (fault, "region-method") == 0 ||
          std::strcmp (fault, "unexpected-region-policy-hash") == 0)
        options.policy_proof_fraction = 1e-12;
#endif
    // All remaining caps are the bounded defaults of the native lifting API.
    struct LiftOwner {
      TlsfGr1LiftResult value{};
      ~LiftOwner () { tlsf_gr1_lift_result_clear (&value); }
    } lifted;
    TlsfGr1LiftError lift_error{};
    const auto lift_status = tlsf_gr1_lift_v1 (
        reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()),
        args.tlsf_source.size (), nullptr, 0, &options, &lifted.value, &lift_error);
    if (lift_status != TLSF_GR1_LIFT_OK) {
      native_arm_diagnostic (arm, lift_error.stage, int (lift_status), lift_error.message);
      return EXIT_CODE_UNKNOWN;
    }

#ifdef ACACIA_NATIVE_TEST_HOOKS
    native_lift_test_fault (lifted.value);
#endif
    const auto& result = lifted.value;
    const bool certificate_method = result.method == TLSF_GR1_CHECK_CERTIFICATE &&
                                    result.verdict == TLSF_GR1_CHECK_VERIFIED;
    const bool region_method = result.method == TLSF_GR1_CHECK_REGION &&
                               result.verdict == TLSF_GR1_CHECK_REGION_VERIFIED;
    if ((!certificate_method && !region_method) ||
        (result.semantics != TLSF_GR1_EXACT && result.semantics != TLSF_GR1_STRICT) ||
        !result.game_aag || !result.game_size ||
        !result.certificate_aag || !result.certificate_size ||
        !result.certificate_json || !result.certificate_json_size ||
        (certificate_method && (!result.policy_aag || !result.policy_size ||
                                !result.policy_json || !result.policy_json_size)) ||
        !result.evidence_json || !result.evidence_size) {
      native_arm_diagnostic (arm, "artifact", -1, "incomplete or unverified system proof");
      return EXIT_CODE_UNKNOWN;
    }
    boost::json::value parsed_evidence;
    const auto* evidence = native_json_object (result.evidence_json, result.evidence_size,
                                                parsed_evidence);
    if (!evidence || !native_json_field (*evidence, "format", TLSF_GR1_LIFT_EVIDENCE_FORMAT)) {
      native_arm_diagnostic (arm, "evidence", -1, "invalid lift evidence JSON");
      return EXIT_CODE_UNKNOWN;
    }
    if (args.tlsf_sha256.size () != 64 ||
        !native_sha256_matches (*evidence, "source_sha256", args.tlsf_source.data (),
                                args.tlsf_source.size ()) ||
        !native_json_field (*evidence, "source_sha256", args.tlsf_sha256)) {
      native_arm_diagnostic (arm, "source", -1, "snapshot hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    if (!native_sha256_matches (*evidence, "game_sha256", result.game_aag,
                                result.game_size)) {
      native_arm_diagnostic (arm, "game", -1, "game artifact hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    if (!native_sha256_matches (*evidence, "certificate_sha256",
                                result.certificate_aag, result.certificate_size)) {
      native_arm_diagnostic (arm, "certificate", -1, "certificate artifact hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    if (!native_lift_policy_hash_matches (*evidence, certificate_method,
                                           result.policy_aag, result.policy_size)) {
      native_arm_diagnostic (arm, "policy", -1, "policy artifact hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    const std::string_view semantics = result.semantics == TLSF_GR1_EXACT ? "exact" : "strict";
    const std::string_view method = certificate_method ? "certificate" : "gr1-region-v1";
    const std::string_view verdict = certificate_method ? "VERIFIED" : "REGION_VERIFIED";
    if (!native_json_field (*evidence, "reduction_semantics", semantics) ||
        !native_json_field (*evidence, "method", method) ||
        !native_json_field (*evidence, "verdict", verdict) ||
        !native_json_field (*evidence, "move_source", "target_transition") ||
        !native_proof_sidecars (result.certificate_json, result.certificate_json_size,
                                result.policy_json, result.policy_json_size,
                                semantics, certificate_method)) {
      native_arm_diagnostic (arm, "metadata", -1, "proof side, method or semantics mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    // The lift ABI returns game bytes but no source-bound target-game handle.
    // Recompute the target reduction from this immutable snapshot before check.
    TlsfPipelineError pipeline_error{};
    TlsfPipelineOptionsV2 pipeline_options{};
    pipeline_options.abi_version = TLSF_PIPELINE_OPTIONS_ABI_VERSION;
    pipeline_options.struct_size = sizeof pipeline_options;
    pipeline_options.certify = true;
    pipeline_options.template_mask = TPL_ALL;
    pipeline_options.require_unambiguous_origin = true;
    pipeline_options.error = &pipeline_error;
    std::unique_ptr<TlsfPipeline, decltype (&tlsf_pipeline_free)> pipeline (
        tlsf_pipeline_load_bytes_v2 (
            reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()),
            args.tlsf_source.size (), &pipeline_options), tlsf_pipeline_free);
    if (!pipeline || args.tlsf_sha256 != pipeline->source_sha256) {
      native_arm_diagnostic (arm, "source", -1, "could not bind target reduction to snapshot");
      return EXIT_CODE_UNKNOWN;
    }
    TlsfGr1ReductionOptions reduction_options{};
    reduction_options.abi_version = TLSF_GR1_REDUCTION_ABI_VERSION;
    reduction_options.struct_size = sizeof reduction_options;
    reduction_options.semantics = result.semantics;
    reduction_options.deadline_mono_ns = deadline_mono_ns;
    reduction_options.max_artifact_bytes = TLSF_GR1_LIFT_DEFAULT_MAX_ARTIFACT_BYTES;
    reduction_options.max_monitor_states = TLSF_GR1_LIFT_DEFAULT_MAX_MONITOR_STATES;
    struct ReductionOwner {
      TlsfGr1Reduction value{};
      ~ReductionOwner () { tlsf_gr1_reduction_clear (&value); }
    } target;
    TlsfGr1ReductionError reduction_error{};
    if (tlsf_gr1_reduce_v1 (pipeline.get (), &reduction_options, &target.value,
                            &reduction_error) != TLSF_GR1_REDUCE_OK ||
        target.value.aag_size != result.game_size || !target.value.aag ||
        std::memcmp (target.value.aag, result.game_aag, result.game_size) != 0) {
      native_arm_diagnostic (arm, "source_game", -1, "checked game differs from snapshot reduction");
      return EXIT_CODE_UNKNOWN;
    }
    tlsf_gr1_reduction_clear (&target.value);
    pipeline.reset ();
    const auto span = [] (const char* data, size_t size) -> TlsfGr1Bytes {
      return {reinterpret_cast<const uint8_t*> (data), size};
    };
    TlsfGr1CheckInput input{};
    input.game_aag = span (result.game_aag, result.game_size);
    input.certificate_aag = span (result.certificate_aag, result.certificate_size);
    input.certificate_json = span (result.certificate_json, result.certificate_json_size);
    if (certificate_method) {
      input.policy_aag = span (result.policy_aag, result.policy_size);
      input.policy_json = span (result.policy_json, result.policy_json_size);
    }
    TlsfGr1CheckOptions check_options{};
    check_options.abi_version = TLSF_NATIVE_ABI_VERSION;
    check_options.method = result.method;
    check_options.node_cap = TLSF_GR1_LIFT_DEFAULT_CHECKER_NODES;
    check_options.cache_cap = TLSF_GR1_LIFT_DEFAULT_CHECKER_CACHE;
    check_options.max_artifact_bytes = TLSF_GR1_LIFT_DEFAULT_MAX_ARTIFACT_BYTES;
    check_options.deadline_mono_ns = deadline_mono_ns;
    struct CheckOwner {
      TlsfGr1CheckResult value{};
      ~CheckOwner () { tlsf_gr1_check_result_clear (&value); }
    } checked;
    const auto check_status = tlsf_gr1_check (&input, &check_options, &checked.value);
    if (check_status != TLSF_GR1_CHECK_OK || checked.value.verdict != result.verdict) {
      native_arm_diagnostic (arm, checked.value.stage[0] ? checked.value.stage : "check",
                         check_status == TLSF_GR1_CHECK_OK
                             ? int (checked.value.verdict) : int (check_status),
                         checked.value.message[0] ? checked.value.message : "proof not verified");
      return EXIT_CODE_UNKNOWN;
    }
    return EXIT_CODE_REAL;
  }
}
#endif
