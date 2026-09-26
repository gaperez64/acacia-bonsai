#pragma once

#if ACACIA_NATIVE_ARMS
# include <string_view>
# include <unordered_set>

# include <memory>
# include <tlsf/pipeline.h>
# include <vector>
# include <yyjson.h>

namespace acacia {
  using native_json_doc = std::unique_ptr<yyjson_doc, decltype (&yyjson_doc_free)>;

  // yyjson preserves all object members. Reject duplicates at every depth,
  // comparing decoded keys and their explicit lengths.
  inline bool native_json_unique_keys (yyjson_val* root) {
    std::vector<yyjson_val*> pending {root};
    while (!pending.empty ()) {
      yyjson_val* value = pending.back ();
      pending.pop_back ();
      if (yyjson_is_obj (value)) {
        std::unordered_set<std::string_view> keys;
        auto iter = yyjson_obj_iter_with (value);
        while (yyjson_val* key = yyjson_obj_iter_next (&iter)) {
          if (!keys.emplace (yyjson_get_str (key), yyjson_get_len (key)).second)
            return false;
          pending.push_back (yyjson_obj_iter_get_val (key));
        }
      }
      else if (yyjson_is_arr (value)) {
        auto iter = yyjson_arr_iter_with (value);
        while (yyjson_val* item = yyjson_arr_iter_next (&iter))
          pending.push_back (item);
      }
    }
    return true;
  }

  inline yyjson_val* native_json_object (const char* bytes, size_t size, native_json_doc& parsed) {
    parsed.reset ();
    if (!bytes || !size)
      return nullptr;
    parsed.reset (yyjson_read (bytes, size, 0));
    yyjson_val* root = parsed ? yyjson_doc_get_root (parsed.get ()) : nullptr;
    if (!yyjson_is_obj (root) || !native_json_unique_keys (root)) {
      parsed.reset ();
      return nullptr;
    }
    return root;
  }

  inline bool native_json_field (yyjson_val* object, const char* key, std::string_view expected) {
    yyjson_val* field = yyjson_obj_get (object, key);
    return yyjson_is_str (field) &&
           std::string_view (yyjson_get_str (field), yyjson_get_len (field)) == expected;
  }

  inline bool native_sha256_matches (yyjson_val* object, const char* key, const char* bytes,
                                     size_t size) {
    char actual[65] {};
    return bytes && tlsf_pipeline_source_sha256 (bytes, size, actual) &&
           native_json_field (object, key, actual);
  }

  inline bool native_lift_policy_hash_matches (yyjson_val* evidence, bool certificate_method,
                                               const char* policy, size_t policy_size) {
    if (!certificate_method)
      return !yyjson_obj_get (evidence, "policy_sha256");
    return native_sha256_matches (evidence, "policy_sha256", policy, policy_size);
  }

  inline bool native_proof_sidecars (const char* certificate, size_t certificate_size,
                                     const char* policy, size_t policy_size,
                                     std::string_view semantics, bool needs_policy,
                                     bool unreal = false) {
    native_json_doc parsed_certificate (nullptr, yyjson_doc_free);
    native_json_doc parsed_policy (nullptr, yyjson_doc_free);
    yyjson_val* cert = native_json_object (certificate, certificate_size, parsed_certificate);
    const std::string_view side = unreal ? "environment" : "system";
    const std::string_view status = unreal ? "unrealizable" : "realizable";
    if (!cert || !native_json_field (cert, "side", side) ||
        !native_json_field (cert, "status", status) ||
        !native_json_field (cert, "reduction_semantics", semantics))
      return false;
    if (!needs_policy)
      return !policy && !policy_size;
    yyjson_val* pol = native_json_object (policy, policy_size, parsed_policy);
    return pol && native_json_field (pol, "side", side) &&
           native_json_field (pol, "reduction_semantics", semantics);
  }
}
#endif
