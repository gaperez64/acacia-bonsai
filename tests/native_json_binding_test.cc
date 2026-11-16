#define ACACIA_NATIVE_ARMS 1
#include "../src/native_proof_binding.hh"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tlsf/gr1_lift.h>

namespace {
  void require (bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error (message);
  }

  void invariant_i10 () {
    using namespace acacia;
    const std::string sides[] = {"system", "environment", "unknown"};
    const std::string statuses[] = {"realizable", "unrealizable", "unknown"};
    const std::string semantics[] = {"exact", "strict", "unknown"};
    const std::string policies[] = {
        "", R"({"side":"system","reduction_semantics":"exact"})",
        R"({"side":"environment","reduction_semantics":"exact"})",
        R"({"side":"system","reduction_semantics":"strict"})",
        R"({"side":"environment","reduction_semantics":"strict"})",
        R"({"side":"unknown","reduction_semantics":"exact"})", R"({"side":"system"})"};
    size_t rows = 0;
    // Independent truth table: acceptance is the diagonal in side, status
    // and semantics, with exactly the method's required policy presence.
    for (int unreal = 0; unreal < 2; ++unreal)
      for (int strict = 0; strict < 2; ++strict)
        for (bool needs_policy : {false, true})
          for (int side = 0; side < 3; ++side)
            for (int status = 0; status < 3; ++status)
              for (int sem = 0; sem < 3; ++sem)
                for (int p = 0; p < 7; ++p) {
                  const auto cert = "{\"side\":\"" + sides[side] + "\",\"status\":\"" +
                      statuses[status] + "\",\"reduction_semantics\":\"" + semantics[sem] + "\"}";
                  const bool expected = side == unreal && status == unreal && sem == strict &&
                      p == (needs_policy ? 1 + unreal + 2 * strict : 0);
                  const auto& policy = policies[p];
                  const bool actual = native_proof_sidecars (
                      cert.data (), cert.size (), p ? policy.data () : nullptr, policy.size (),
                      semantics[strict], needs_policy, unreal);
                  require (actual == expected, "I10 side/status/semantics/policy oracle failed");
                  ++rows;
                }
    const auto& cert = R"({"side":"system","status":"realizable","reduction_semantics":"exact"})";
    require (!native_proof_sidecars (cert, std::strlen (cert), "", 0, "exact", false) &&
             !native_proof_sidecars (cert, std::strlen (cert), nullptr, 1, "exact", false),
             "I10 absent policy must be a null, zero-size span");

    // Published SHA-256("abc") known answer, never computed by the binding
    // implementation to construct its own expected evidence.
    constexpr auto abc_hash = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    for (const char* key : {"source_sha256", "game_sha256", "certificate_sha256", "policy_sha256"}) {
      const std::string evidence = "{\"" + std::string (key) + "\":\"" + abc_hash + "\"}";
      native_json_doc doc (nullptr, yyjson_doc_free);
      auto* root = native_json_object (evidence.data (), evidence.size (), doc);
      require (root && native_sha256_matches (root, key, "abc", 3) &&
               !native_sha256_matches (root, key, "abC", 3) &&
               !native_sha256_matches (root, key, "abc", 4) &&
               !native_sha256_matches (root, key, nullptr, 3),
               "I10 exact-byte hash oracle failed");
    }
    const std::string evidences[] = {"{}", "{\"policy_sha256\":null}",
                                    "{\"policy_sha256\":\"wrong\"}",
                                    "{\"policy_sha256\":\"" + std::string (abc_hash) + "\"}"};
    for (const auto& evidence : evidences) {
      native_json_doc doc (nullptr, yyjson_doc_free);
      auto* root = native_json_object (evidence.data (), evidence.size (), doc);
      require (root && native_lift_policy_hash_matches (root, false, nullptr, 0) ==
                           (evidence == "{}") &&
               native_lift_policy_hash_matches (root, true, "abc", 3) ==
                           (evidence.find (abc_hash) != std::string::npos),
               "I10 certificate/region policy hash oracle failed");
    }

    // Two snapshots differ only by whitespace, so their games are identical.
    // Even valid proofs with matching artifact hashes cannot cross snapshots.
    const std::string source =
        R"(INFO { TITLE: "I10" SEMANTICS: Mealy TARGET: Mealy }
           GLOBAL { PARAMETERS { n = 5; } }
           MAIN { INPUTS { a[n]; } OUTPUTS { b[n]; }
                  GUARANTEES { &&[0 <= i < n] G F b[i]; } })";
    using target_owner = std::unique_ptr<TlsfGr1LiftTarget, decltype (&tlsf_gr1_lift_target_free)>;
    target_owner targets[] = {{nullptr, tlsf_gr1_lift_target_free},
                              {nullptr, tlsf_gr1_lift_target_free}};
    TlsfGr1LiftResult proofs[2] {};
    const auto clear = [&] (TlsfGr1LiftResult*) {
      for (auto& proof : proofs) tlsf_gr1_lift_result_clear (&proof);
    };
    std::unique_ptr<TlsfGr1LiftResult, decltype (clear)> cleanup (proofs, clear);
    for (int i = 0; i < 2; ++i) {
      const std::string snapshot = source + std::string (i, '\n');
      TlsfGr1LiftTarget* raw = nullptr;
      TlsfGr1LiftError error {};
      const auto prepared = tlsf_gr1_lift_target_prepare (
          reinterpret_cast<const uint8_t*> (snapshot.data ()), snapshot.size (),
          nullptr, 0, nullptr, &raw, &error);
      targets[i].reset (raw);
      require (prepared == TLSF_GR1_LIFT_OK, "I10 target preparation failed");
      require (!tlsf_gr1_lift_target_matches (raw, &proofs[i]),
               "I10 unverified target must not match");
      const auto lifted = tlsf_gr1_lift_from_target (raw, nullptr, &proofs[i], &error);
      if (lifted != TLSF_GR1_LIFT_OK)
        std::cerr << "I10 lift: " << error.stage << ": " << error.message << '\n';
      require (lifted == TLSF_GR1_LIFT_OK, "I10 target proof construction failed");
    }
    require (proofs[0].game_size == proofs[1].game_size &&
             std::memcmp (proofs[0].game_aag, proofs[1].game_aag, proofs[0].game_size) == 0,
             "I10 whitespace twins must have identical game artifacts");
    for (int i = 0; i < 2; ++i)
      for (int j = 0; j < 2; ++j)
        require (bool (tlsf_gr1_lift_target_matches (targets[i].get (), &proofs[j])) == (i == j),
                 "I10 exact snapshot target oracle failed");
    for (int mutation = 0; mutation < 3; ++mutation) {
      auto changed = proofs[0]; // Borrowed spans; only the original owns them.
      if (mutation == 0)
        changed.semantics = changed.semantics == TLSF_GR1_EXACT ? TLSF_GR1_STRICT : TLSF_GR1_EXACT;
      if (mutation == 1)
        changed.method = changed.method == TLSF_GR1_CHECK_CERTIFICATE
                             ? TLSF_GR1_CHECK_REGION : TLSF_GR1_CHECK_CERTIFICATE;
      if (mutation == 2) changed.verdict = TLSF_GR1_CHECK_UNKNOWN;
      require (!tlsf_gr1_lift_target_matches (targets[0].get (), &changed),
               "I10 mutated semantics/method/verdict must not match the checked target");
    }
    std::cout << "I10: " << rows << " sidecar rows, exact-byte hashes, policy presence, "
                 "snapshot twins and target mutations passed\n";
  }
}

static int json_binding_checks () {
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

int main () {
  try {
    invariant_i10 ();
    return json_binding_checks ();
  }
  catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 15;
  }
}
