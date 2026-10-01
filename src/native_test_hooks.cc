#include "configuration.hh"
#include "native_test_hooks.hh"
#include "native_proof_binding.hh"

#if ACACIA_NATIVE_ARMS && defined(ACACIA_NATIVE_TEST_HOOKS)
#include <cstring>
#include <cstdlib>
#include <memory>
#include <utility>
#include <tlsf/pipeline.h>

namespace acacia {
  void native_lift_test_fault (TlsfGr1LiftResult& result) {
    const char* fault = std::getenv ("ACACIA_NATIVE_TEST_LIFT_FAULT");
    if (!fault)
      return;
    const auto replace_field = [] (char*& bytes, size_t& size, const char* field,
                                   const char* value) {
      native_json_doc parsed (nullptr, yyjson_doc_free);
      if (!native_json_object (bytes, size, parsed))
        return;
      std::unique_ptr<yyjson_mut_doc, decltype (&yyjson_mut_doc_free)> changed (
          yyjson_doc_mut_copy (parsed.get (), nullptr), yyjson_mut_doc_free);
      if (!changed)
        return;
      yyjson_mut_val* object = yyjson_mut_doc_get_root (changed.get ());
      yyjson_mut_obj_remove_key (object, field);
      if (value && !yyjson_mut_obj_add_strcpy (changed.get (), object, field, value))
        return;
      size_t changed_size = 0;
      char* replacement = yyjson_mut_write (changed.get (), 0, &changed_size);
      if (!replacement)
        return;
      std::free (bytes);
      bytes = replacement;
      size = changed_size;
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
      char hash[65] {};
      if (tlsf_pipeline_source_sha256 (result.game_aag, result.game_size, hash))
        replace_field (result.evidence_json, result.evidence_size, "game_sha256", hash);
      if (tlsf_pipeline_source_sha256 (result.certificate_aag, result.certificate_size, hash))
        replace_field (result.evidence_json, result.evidence_size, "certificate_sha256", hash);
    }
    else if (std::strcmp (fault, "swap-certificate") == 0) {
      std::swap (result.certificate_aag, result.policy_aag);
      std::swap (result.certificate_size, result.policy_size);
    }
    else if (std::strcmp (fault, "wrong-side") == 0)
      replace_field (result.certificate_json, result.certificate_json_size, "side", "environment");
    else if (std::strcmp (fault, "wrong-method") == 0)
      replace_field (
          result.evidence_json, result.evidence_size, "method",
          result.method == TLSF_GR1_CHECK_CERTIFICATE ? "gr1-region-v1" : "certificate");
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
      char hash[65] {};
      if (tlsf_pipeline_source_sha256 (result.certificate_aag, result.certificate_size, hash))
        replace_field (result.evidence_json, result.evidence_size, "certificate_sha256", hash);
    }
  }
  void native_corrupt_gr1_artifact (std::string& artifact) {
    if (std::getenv ("ACACIA_NATIVE_TEST_CORRUPT_PROOF"))
      artifact[0] = 'X';
  }
  void native_lift_test_options (TlsfGr1LiftOptions& options) {
    if (const char* fault = std::getenv ("ACACIA_NATIVE_TEST_LIFT_FAULT"))
      if (std::strcmp (fault, "region-method") == 0 ||
          std::strcmp (fault, "unexpected-region-policy-hash") == 0)
        options.phase_budget.policy_proof_ns = 1;
      else if (std::strcmp (fault, "missing-policy-hash") == 0 ||
               std::strcmp (fault, "swap-certificate") == 0)
        options.proof_order = TLSF_GR1_LIFT_POLICY_FIRST;
  }
}
#endif
