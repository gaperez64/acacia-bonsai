#include "configuration.hh"
#include "native_gr1_arm.hh"
#include "arg_parser.hh"
#include "error_msg.hh"
#include "native_proof_binding.hh"
#include "native_support.hh"
#include "phase_records.hh"
#ifdef ACACIA_NATIVE_TEST_HOOKS
#include "native_test_hooks.hh"
#endif

#if ACACIA_NATIVE_ARMS
#include <array>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <string>
#include <tlsf/gr1_check.h>
#include <tlsf/gr1_oxidd.h>
#include <tlsf/gr1_reduction.h>

namespace acacia {
  struct reduction_record_context {
      const char* arm;
      const TlsfGr1ReductionStats* stats;
  };
  static void record_reduction_stage (void* opaque, TlsfGr1ReductionStatsStage stage,
                                      const TlsfGr1ReductionStageStats* row) noexcept {
    constexpr const char* stages[] = {
      "reduce_source", "reduce_monitors", "reduce_encode", "reduce_publish"};
    auto& context = *static_cast<reduction_record_context*> (opaque);
    const phase_memory memory {long (row->rss_kb), long (row->peak_rss_kb),
                               size_t (row->arena), size_t (row->hblkhd),
                               size_t (row->uordblks), size_t (row->fordblks)};
    phase_finish (context.arm, stages[stage],
                  {phase_clock (CLOCK_MONOTONIC) - row->wall_ns,
                   phase_clock (CLOCK_PROCESS_CPUTIME_ID) - row->cpu_ns},
                  -1, -1, stage == TLSF_GR1_REDUCE_STATS_MONITORS
                      ? context.stats->monitor_count : 0,
                  0, &memory,
                  stage == TLSF_GR1_REDUCE_STATS_MONITORS
                      ? context.stats->monitor_count : 0,
                  stage == TLSF_GR1_REDUCE_STATS_MONITORS
                      ? context.stats->monitor_states : 0);
  }
  int run_native_gr1_arm (const arg_parse_result& args, bool unreal, bool both,
                          uint64_t deadline_mono_ns) {
    const char* arm = both ? "both:gr1:oxidd" : unreal ? "unreal:gr1:oxidd" : "real:gr1:oxidd";
    constexpr size_t artifact_cap = 64u * 1024u * 1024u;
    constexpr size_t solver_nodes = 1u << 22;
    constexpr size_t checker_nodes = 1u << 22;
    if (!native_limit_address_space (arm))
      return EXIT_CODE_UNKNOWN;
    const uint64_t construction_started_ns = phase_clock (CLOCK_MONOTONIC);

    phase_scope pipeline_phase (arm, "pipeline_load_expand");
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
      native_diagnostic (arm, pipeline_error.stage, pipeline_error.status,
                         pipeline_error.message);
      return EXIT_CODE_UNKNOWN;
    }
    pipeline_phase.finish ();
    phase_scope source_phase (arm, "source_provenance");
    if (args.tlsf_sha256 != pipeline->source_sha256) {
      native_diagnostic (arm, "source", -1, "snapshot hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    source_phase.finish ();

    phase_scope reduction_phase (arm, "reduction_monitor");
    TlsfGr1ReductionOptions reduction_options {};
    reduction_options.semantics = TLSF_GR1_EXACT;
    reduction_options.deadline_mono_ns = deadline_mono_ns;
    reduction_options.max_artifact_bytes = artifact_cap;
    reduction_options.max_monitor_states = 10000;
    const auto budget = native_construction_budget (args.arms ? args.arms->size () : 1);
    TlsfGr1ReductionStats reduction_stats {};
    reduction_options.budget = budget;
    reduction_options.stats = &reduction_stats;
    reduction_options.stats_callback = phase_records_enabled () ? record_reduction_stage : nullptr;
    reduction_record_context reduction_context {arm, &reduction_stats};
    reduction_options.stats_context = &reduction_context;
    native_reduction_owner reduction;
    TlsfGr1ReductionError reduction_error {};
    auto reduced = tlsf_gr1_reduce (
        pipeline.get (), &reduction_options, &reduction.value, &reduction_error);
    const auto& work = reduction_stats.work;
    if (reduced != TLSF_GR1_REDUCE_OK) {
      native_budget_record (arm, reduction_error.stage, work, construction_started_ns);
      if (phase_records_enabled ()) {
        const std::string stage = std::string ("reduce_decline_") + reduction_error.stage;
        phase_finish (arm, stage.c_str (), phase_start (), -1, -1,
                      work.formula_nodes, 0, nullptr, work.monitors_completed, work.states);
      }
      native_diagnostic (arm, reduction_error.stage, reduced,
                         native_budget_message (reduction_error.stage,
                                                reduction_error.message, work));
      return EXIT_CODE_UNKNOWN;
    }
    if (!reduction.value.game || !reduction.value.aag || !reduction.value.aag_size) {
      native_diagnostic (arm, "reduction", -1, "missing exact game");
      return EXIT_CODE_UNKNOWN;
    }
    native_json_doc parsed_reduction (nullptr, yyjson_doc_free);
    auto* metadata = native_json_object (reduction.value.metadata_json,
                                         reduction.value.metadata_size, parsed_reduction);
    if (!metadata || !native_json_field (metadata, "semantics", "exact") ||
        !native_json_field (metadata, "source_sha256", args.tlsf_sha256) ||
        !native_sha256_matches (metadata, "game_sha256", reduction.value.aag,
                                reduction.value.aag_size)) {
      native_diagnostic (arm, "reduction", -1, "source or game hash mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    reduction_phase.finish ();

    phase_scope solve_phase (arm, "solve_export_total");
    OxiddFailure failure {};
    Gr1SolveOptions solve_options {};
    solve_options.oxidd = oxidd_solve_options_default ();
    solve_options.oxidd.failure = &failure;
    solve_options.oxidd.node_cap = solver_nodes;
    solve_options.oxidd.cache_cap = 1u << 20;
    solve_options.oxidd.deadline_mono_ns = deadline_mono_ns;
    solve_options.oxidd.max_artifact_bytes = artifact_cap;
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
    Gr1CertificateStats solver_stats {};
    solve_options.certificate = &certificate;
    solve_options.stats = phase_records_enabled () ? &solver_stats : nullptr;
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
        solve_gr1_oxidd (game, &solved_unreal, &solve_options), aig_free);
    solve_phase.finish ();
    if (phase_records_enabled ()) {
      const auto emit = [&] (const char* phase, uint64_t wall, uint64_t cpu,
                           size_t nodes, size_t bytes_count) {
        if (!wall) return;
        phase_finish (arm, phase,
                      {phase_clock (CLOCK_MONOTONIC) - wall,
                       phase_clock (CLOCK_PROCESS_CPUTIME_ID) - cpu},
                      static_cast<long long> (nodes), -1, 0, bytes_count);
      };
      emit ("solve", solver_stats.solve_wall_ns, solver_stats.solve_cpu_ns,
            solver_stats.nodes_at_export, 0);
      emit ("certificate_policy_export", solver_stats.export_wall_ns,
            solver_stats.export_cpu_ns, solver_stats.nodes_before_teardown,
            sizes[0] + sizes[1] + sizes[2] + sizes[3]);
      emit ("teardown_bdd_manager", solver_stats.teardown_wall_ns,
            solver_stats.teardown_cpu_ns, 0, 0);
    }
    if (failure.kind != OXIDD_FAILURE_NONE || certificate.failed ||
        (!strategy && !solved_unreal)) {
      native_diagnostic (arm, "solve", int (failure.kind),
                         certificate.failed ? certificate.error : "solver gave no decision");
      return EXIT_CODE_UNKNOWN;
    }
    const bool proof_unreal = bool (solved_unreal);
    if (!both && proof_unreal != unreal) {
      native_diagnostic (arm, "polarity", 0, "opposite side solved");
      return EXIT_CODE_UNKNOWN;
    }
    phase_scope proof_phase (arm, "proof_binding");
    for (size_t i = 0; i < bytes.size (); ++i) {
      if (!bytes[i] || !sizes[i]) {
        native_diagnostic (arm, "certificate", -1, "certificate or policy missing");
        return EXIT_CODE_UNKNOWN;
      }
    }
    std::array<std::string, 4> stable_artifacts;
    for (size_t i = 0; i < bytes.size (); ++i)
      stable_artifacts[i].assign (bytes[i], sizes[i]);
# ifdef ACACIA_NATIVE_TEST_HOOKS
    native_corrupt_gr1_artifact (stable_artifacts[0]);
# endif
    if (!native_proof_sidecars (stable_artifacts[1].data (), stable_artifacts[1].size (),
                                stable_artifacts[3].data (), stable_artifacts[3].size (), "exact",
                                true, proof_unreal)) {
      native_diagnostic (arm, "metadata", -1, "proof side or semantics mismatch");
      return EXIT_CODE_UNKNOWN;
    }
    const std::string stable_game (reduction.value.aag, reduction.value.aag_size);
    proof_phase.finish ();
    phase_scope strategy_teardown (arm, "teardown_strategy");
    strategy.reset ();
    strategy_teardown.finish ();
    phase_scope export_teardown (arm, "teardown_export_buffers");
    for (char*& p : bytes) {
      std::free (p);
      p = nullptr;
    }
    export_teardown.finish ();
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
    native_check_owner checked;
    phase_scope check_phase (arm, "independent_check");
    const TlsfGr1CheckStatus status = tlsf_gr1_check (&input, &check_options, &checked.value);
    check_phase.finish (static_cast<long long> (checked.value.peak_nodes));
    if (status != TLSF_GR1_CHECK_OK || checked.value.verdict != TLSF_GR1_CHECK_VERIFIED) {
      native_diagnostic (arm, checked.value.stage[0] ? checked.value.stage : "check",
                         status == TLSF_GR1_CHECK_OK ? int (checked.value.verdict) : int (status),
                         checked.value.message[0] ? checked.value.message : "proof not verified");
      return EXIT_CODE_UNKNOWN;
    }
    return proof_unreal ? EXIT_CODE_UNREAL : EXIT_CODE_REAL;
  }
}
#endif
