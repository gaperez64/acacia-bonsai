#pragma once

#if ACACIA_NATIVE_ARMS
#include <memory>
#include <string_view>
#include <tlsf/pipeline.h>
#include <yyjson.h>

namespace acacia {
  using native_json_doc = std::unique_ptr<yyjson_doc, decltype (&yyjson_doc_free)>;
  bool native_json_unique_keys (yyjson_val* root);
  yyjson_val* native_json_object (const char* bytes, size_t size, native_json_doc& parsed);
  bool native_json_field (yyjson_val* object, const char* key, std::string_view expected);
  bool native_sha256_matches (yyjson_val* object, const char* key, const char* bytes, size_t size);
  bool native_lift_policy_hash_matches (yyjson_val* evidence, bool certificate_method,
                                        const char* policy, size_t policy_size);
  bool native_proof_sidecars (const char* certificate, size_t certificate_size,
                              const char* policy, size_t policy_size,
                              std::string_view semantics, bool needs_policy,
                              bool unreal = false);
}
#endif
