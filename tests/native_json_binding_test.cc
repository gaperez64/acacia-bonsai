#define ACACIA_NATIVE_ARMS 1
#include "../src/native_proof_binding.hh"

#include <cstring>
#include <string>

int main () {
  using namespace acacia;
  const auto valid = [] (const char* json) {
    native_json_doc parsed (nullptr, yyjson_doc_free);
    return native_json_object (json, std::strlen (json), parsed) != nullptr;
  };
  if (!valid (R"({"s\u0069de":"system","extra":[{"key":1}]})"))
    return 1;
  if (valid (R"({"side":"system","side":"system"})"))
    return 2;
  if (valid (R"({"side":"system","s\u0069de":"system"})"))
    return 3;
  if (valid (R"({"extra":{"key":1,"k\u0065y":2}})"))
    return 4;
  if (valid (R"({"extra":[{"key":1,"key":2}]})"))
    return 5;
  if (valid (R"({"key":"value"} trailing)"))
    return 6;

  native_json_doc parsed (nullptr, yyjson_doc_free);
  constexpr char embedded_nul[] = R"({"side":"system\u0000"})";
  auto* root = native_json_object (embedded_nul, sizeof embedded_nul - 1, parsed);
  if (!root || native_json_field (root, "side", "system"))
    return 7;
  parsed.reset ();

  char hash[65] {};
  if (!tlsf_pipeline_source_sha256 ("abc", 3, hash))
    return 8;
  const std::string hash_json = std::string (R"({"source_sha256":")") + hash + "\"}";
  root = native_json_object (hash_json.data (), hash_json.size (), parsed);
  if (!root || !native_sha256_matches (root, "source_sha256", "abc", 3))
    return 9;
  parsed.reset ();
  const std::string long_hash_json = std::string (R"({"source_sha256":")") + hash + R"(\u0000"})";
  root = native_json_object (long_hash_json.data (), long_hash_json.size (), parsed);
  if (!root || native_sha256_matches (root, "source_sha256", "abc", 3))
    return 10;
  parsed.reset ();

  constexpr char duplicate_certificate[] =
      R"({"side":"system","status":"realizable","reduction_semantics":"exact","nested":{"x":1,"\u0078":2}})";
  if (native_proof_sidecars (duplicate_certificate, sizeof duplicate_certificate - 1, nullptr, 0,
                             "exact", false))
    return 11;

  const std::string real_certificate =
      R"({"side":"system","status":"realizable","reduction_semantics":"exact"})";
  const std::string unreal_certificate =
      R"({"side":"environment","status":"unrealizable","reduction_semantics":"exact"})";
  const std::string real_policy = R"({"side":"system","reduction_semantics":"exact"})";
  const std::string unreal_policy = R"({"side":"environment","reduction_semantics":"exact"})";
  const auto bound = [] (const std::string& cert, const std::string& policy, bool unreal) {
    return native_proof_sidecars (cert.data (), cert.size (), policy.data (), policy.size (),
                                  "exact", true, unreal);
  };
  if (!bound (real_certificate, real_policy, false) ||
      !bound (unreal_certificate, unreal_policy, true))
    return 12;
  if (bound (real_certificate, real_policy, true) ||
      bound (unreal_certificate, unreal_policy, false) ||
      bound (real_certificate, unreal_policy, false) ||
      bound (unreal_certificate, real_policy, true))
    return 13;
  if (bound (R"({"side":"system","status":"unrealizable","reduction_semantics":"exact"})",
             real_policy, false) ||
      bound (R"({"side":"environment","status":"realizable","reduction_semantics":"exact"})",
             unreal_policy, true) ||
      bound (R"({"side":"system","status":"realizable","reduction_semantics":"approximate"})",
             real_policy, false) ||
      bound (R"({"side":"environment","status":"unrealizable","reduction_semantics":"approximate"})",
             unreal_policy, true))
    return 14;
  return 0;
}
