#pragma once

#include "arg_parser.hh"
#include "error_msg.hh"
#include "native_proof_binding.hh"

#if ACACIA_NATIVE_ARMS
# include <string_view>
# include <tlsf/gr1_check.h>
# include <tlsf/gr1_oxidd.h>
# include <tlsf/gr1_reduction.h>

# include <array>
# include <cerrno>
# include <cstdlib>
# include <iostream>
# include <memory>
# include <string>
# include <sys/resource.h>
# include <time.h>
# include <unistd.h>

namespace acacia {
  inline void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                                     std::string_view message) {
    const auto quote = [] (std::string_view value) {
      std::string out = "\"";
      for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\')
          out += '\\';
        if (ch < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[ch >> 4];
          out += hex[ch & 15];
        }
        else
          out += char (ch);
      }
      return out + '"';
    };
    const std::string line =
        std::string ("{\"arm\":") + quote (arm) + ",\"stage\":" + quote (stage) +
        ",\"status\":" + std::to_string (status) + ",\"message\":" + quote (message) + "}\n";
    // One write keeps concurrent children's JSON records intact on stderr.
    (void) ::write (STDERR_FILENO, line.data (), line.size ());
  }

  inline void native_diagnostic (bool unreal, std::string_view stage, int status,
                                 std::string_view message) {
    native_arm_diagnostic (unreal ? "unreal:gr1:oxidd" : "real:gr1:oxidd", stage, status, message);
  }

  inline int run_native_gr1_arm (const arg_parse_result& args, bool unreal,
                                 uint64_t deadline_mono_ns) {
    constexpr size_t artifact_cap = 64u * 1024u * 1024u;
    constexpr size_t solver_nodes = 1u << 22;
    constexpr size_t checker_nodes = 1u << 22;
    // The benchmark's cgroup remains the hard RSS bound. Limit address space
    // as a second guard for invocations without a cgroup.
    rlimit address {};
    if (getrlimit (RLIMIT_AS, &address) != 0) {
      native_diagnostic (unreal, "memory", errno, "could not read address-space cap");
      return EXIT_CODE_UNKNOWN;
    }
    {
      constexpr rlim_t cap = rlim_t {8} * 1024 * 1024 * 1024;
      if (address.rlim_cur == RLIM_INFINITY || address.rlim_cur > cap) {
        address.rlim_cur = cap;
        if (setrlimit (RLIMIT_AS, &address) != 0) {
          native_diagnostic (unreal, "memory", errno, "could not set address-space cap");
          return EXIT_CODE_UNKNOWN;
        }
      }
    }

    TlsfPipelineError pipeline_error {};
    TlsfPipelineOptions pipeline_options {};
    pipeline_options.certify = true;
    pipeline_options.template_mask = TPL_ALL;
    pipeline_options.source_sha256 = args.tlsf_sha256.c_str ();
    pipeline_options.require_unambiguous_origin = true;
    pipeline_options.error = &pipeline_error;
    std::unique_ptr<TlsfPipeline, decltype (&tlsf_pipeline_free)> pipeline (
        tlsf_pipeline_load_bytes (reinterpret_cast<const uint8_t*> (args.tlsf_source.data ()),
                                  args.tlsf_source.size (), &pipeline_options),
        tlsf_pipeline_free);
    if (!pipeline) {
      native_diagnostic (unreal, pipeline_error.stage, pipeline_error.status,
                         pipeline_error.message);
      return EXIT_CODE_UNKNOWN;
    }
    if (args.tlsf_sha256 != pipeline->source_sha256) {
      native_diagnostic (unreal, "source", -1, "snapshot hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }

    TlsfGr1ReductionOptions reduction_options {};
    reduction_options.semantics = TLSF_GR1_EXACT;
    reduction_options.deadline_mono_ns = deadline_mono_ns;
    reduction_options.max_artifact_bytes = artifact_cap;
    reduction_options.max_monitor_states = 10000;
    struct ReductionOwner {
        TlsfGr1Reduction value {};
        ~ReductionOwner () { tlsf_gr1_reduction_clear (&value); }
    } reduction;
    TlsfGr1ReductionError reduction_error {};
    auto reduced =
        tlsf_gr1_reduce (pipeline.get (), &reduction_options, &reduction.value, &reduction_error);
    if (reduced != TLSF_GR1_REDUCE_OK) {
      native_diagnostic (unreal, reduction_error.stage, reduced, reduction_error.message);
      return EXIT_CODE_UNKNOWN;
    }
    if (!reduction.value.game || !reduction.value.aag || !reduction.value.aag_size) {
      native_diagnostic (unreal, "reduction", -1, "missing exact game");
      return EXIT_CODE_UNKNOWN;
    }
    native_json_doc parsed_reduction (nullptr, yyjson_doc_free);
    auto* metadata = native_json_object (reduction.value.metadata_json,
                                         reduction.value.metadata_size, parsed_reduction);
    if (!metadata || !native_json_field (metadata, "semantics", "exact") ||
        !native_json_field (metadata, "source_sha256", args.tlsf_sha256) ||
        !native_sha256_matches (metadata, "game_sha256", reduction.value.aag,
                                reduction.value.aag_size)) {
      native_diagnostic (unreal, "reduction", -1, "source or game hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }

    OxiddFailure failure {};
    auto solve_options = oxidd_solve_options_default ();
    solve_options.failure = &failure;
    solve_options.node_cap = solver_nodes;
    solve_options.cache_cap = 1u << 20;
    solve_options.deadline_mono_ns = deadline_mono_ns;
    solve_options.max_artifact_bytes = artifact_cap;
    std::array<char*, 4> bytes {};
    std::array<size_t, 4> sizes {};
    Gr1CertificateOptions certificate {};
    certificate.semantics = GR1_CERTIFICATE_SEMANTICS_EXACT;
    certificate.aag_bytes = &bytes[0];
    certificate.json_bytes = &bytes[1];
    certificate.policy_aag_bytes = &bytes[2];
    certificate.policy_json_bytes = &bytes[3];
    certificate.aag_size = &sizes[0];
    certificate.json_size = &sizes[1];
    certificate.policy_aag_size = &sizes[2];
    certificate.policy_json_size = &sizes[3];
    certificate.max_artifact_bytes = artifact_cap;
    struct ArtifactOwner {
        std::array<char*, 4>& bytes;
        ~ArtifactOwner () {
          for (char* p : bytes)
            std::free (p);
        }
    } artifacts {bytes};
    int solved_unreal = 0;
    Aig* game = reduction.value.game;
    reduction.value.game = nullptr;  // the solver takes ownership
    std::unique_ptr<Aig, decltype (&aig_free)> strategy (
        solve_gr1_oxidd_ex_with_certificate (game, &solved_unreal, &solve_options, &certificate),
        aig_free);
    if (failure.kind != OXIDD_FAILURE_NONE || certificate.failed ||
        (!strategy && !solved_unreal)) {
      native_diagnostic (unreal, "solve", int (failure.kind),
                         certificate.failed ? certificate.error : "solver gave no decision");
      return EXIT_CODE_UNKNOWN;
    }
    if (bool (solved_unreal) != unreal) {
      native_diagnostic (unreal, "polarity", 0, "opposite side solved");
      return EXIT_CODE_UNKNOWN;
    }
    for (size_t i = 0; i < bytes.size (); ++i) {
      if (!bytes[i] || !sizes[i]) {
        native_diagnostic (unreal, "certificate", -1, "certificate or policy missing");
        return EXIT_CODE_UNKNOWN;
      }
    }
    std::array<std::string, 4> stable_artifacts;
    for (size_t i = 0; i < bytes.size (); ++i)
      stable_artifacts[i].assign (bytes[i], sizes[i]);
# ifdef ACACIA_NATIVE_TEST_HOOKS
    // This definition is set only on the debug test executable.
    if (std::getenv ("ACACIA_NATIVE_TEST_CORRUPT_PROOF"))
      stable_artifacts[0][0] = 'X';
# endif
    if (!native_proof_sidecars (stable_artifacts[1].data (), stable_artifacts[1].size (),
                                stable_artifacts[3].data (), stable_artifacts[3].size (), "exact",
                                true, unreal)) {
      native_diagnostic (unreal, "metadata", -1, "proof side or semantics mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    const std::string stable_game (reduction.value.aag, reduction.value.aag_size);
    strategy.reset ();
    for (char*& p : bytes) {
      std::free (p);
      p = nullptr;
    }
    const auto span = [] (const char* data, size_t size) -> TlsfGr1Bytes {
      return {reinterpret_cast<const uint8_t*> (data), size};
    };
    TlsfGr1CheckInput input {};
    input.game_aag = span (stable_game.data (), stable_game.size ());
    input.certificate_aag = span (stable_artifacts[0].data (), stable_artifacts[0].size ());
    input.certificate_json = span (stable_artifacts[1].data (), stable_artifacts[1].size ());
    input.policy_aag = span (stable_artifacts[2].data (), stable_artifacts[2].size ());
    input.policy_json = span (stable_artifacts[3].data (), stable_artifacts[3].size ());
    TlsfGr1CheckOptions check_options {};
    check_options.method = TLSF_GR1_CHECK_CERTIFICATE;
    check_options.node_cap = checker_nodes;
    check_options.cache_cap = 1u << 20;
    check_options.max_artifact_bytes = artifact_cap;
    check_options.deadline_mono_ns = deadline_mono_ns;
    struct CheckOwner {
        TlsfGr1CheckResult value {};
        ~CheckOwner () { tlsf_gr1_check_result_clear (&value); }
    } checked;
    const TlsfGr1CheckStatus status = tlsf_gr1_check (&input, &check_options, &checked.value);
    if (status != TLSF_GR1_CHECK_OK || checked.value.verdict != TLSF_GR1_CHECK_VERIFIED) {
      native_diagnostic (unreal, checked.value.stage[0] ? checked.value.stage : "check",
                         status == TLSF_GR1_CHECK_OK ? int (checked.value.verdict) : int (status),
                         checked.value.message[0] ? checked.value.message : "proof not verified");
      return EXIT_CODE_UNKNOWN;
    }
    return unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL;
  }
}
#endif
