#pragma once

#if ACACIA_NATIVE_ARMS
# include <tlsf/pipeline.h>
# include <boost/json.hpp>
# include <string>
# include <string_view>

namespace acacia {
  inline const boost::json::object* native_json_object (const char* bytes, size_t size,
                                                         boost::json::value& parsed) {
    if (!bytes || !size) return nullptr;
    boost::system::error_code error;
    parsed = boost::json::parse (boost::json::string_view (bytes, size), error);
    return !error && parsed.is_object () ? &parsed.as_object () : nullptr;
  }

  inline bool native_json_field (const boost::json::object& object, const char* key,
                                  std::string_view expected) {
    const auto* field = object.if_contains (key);
    return field && field->is_string () &&
           std::string_view (field->as_string ().data (), field->as_string ().size ()) ==
               expected;
  }

  inline bool native_sha256_matches (const boost::json::object& object, const char* key,
                                      const char* bytes, size_t size) {
    char actual[65]{};
    return bytes && tlsf_pipeline_source_sha256 (bytes, size, actual) &&
           native_json_field (object, key, actual);
  }

  inline bool native_lift_policy_hash_matches (const boost::json::object& evidence,
                                                bool certificate_method,
                                                const char* policy, size_t policy_size) {
    if (!certificate_method) return !evidence.if_contains ("policy_sha256");
    return native_sha256_matches (evidence, "policy_sha256", policy, policy_size);
  }

  inline bool native_proof_sidecars (const char* certificate, size_t certificate_size,
                                     const char* policy, size_t policy_size,
                                     std::string_view semantics, bool needs_policy,
                                     bool unreal = false) {
    boost::json::value parsed_certificate, parsed_policy;
    const auto* cert = native_json_object (certificate, certificate_size,
                                           parsed_certificate);
    const std::string_view side = unreal ? "environment" : "system";
    const std::string_view status = unreal ? "unrealizable" : "realizable";
    if (!cert || !native_json_field (*cert, "side", side) ||
        !native_json_field (*cert, "status", status) ||
        !native_json_field (*cert, "reduction_semantics", semantics))
      return false;
    if (!needs_policy) return !policy && !policy_size;
    const auto* pol = native_json_object (policy, policy_size, parsed_policy);
    return pol && native_json_field (*pol, "side", side) &&
           native_json_field (*pol, "reduction_semantics", semantics);
  }
}
#endif
