#include "symcount_arm.hh"

#include "arg_parser.hh"
#include "error_msg.hh"
#include "phase_records.hh"
#include "solver/obligation_helper.hh"
#include "solver/symmetric_count.hh"

#include <spot/tl/parse.hh>

namespace acacia {
  int run_symcount_arm (const arg_parse_result& args, uint64_t deadline_mono_ns) {
    worker_route ("original_game", "symcount", "unknown");
    worker_stage ("symcount");
    if (!obligation::limit_address_space ()) {
      worker_stopped ("address_space_limit");
      return EXIT_CODE_UNKNOWN;
    }
    tlsf_frontend::specification child_local;
    const auto& objective = args.prepare_objective (child_local);
    validate_ltl_partition (objective.inputs, objective.outputs);
    const auto parsed = spot::parse_infix_psl (objective.formula);
    if (!parsed.f || !parsed.errors.empty ()) {
      worker_stopped ("invalid_formula");
      return EXIT_CODE_UNKNOWN;
    }
    symcount::limits bound;
    bound.deadline_mono_ns = deadline_mono_ns;
    // About 256 bytes per vertex across the game, the Spot arena and the check.
    bound.max_vertices =
        std::max<uint64_t> (1, obligation::rss_share (args.arms ? args.arms->size () : 1) / 256);
    const auto result = symcount::solve (parsed.f, objective.inputs, objective.outputs,
                                         objective.metadata.tlsf_indexed_families, bound);
    if (result.outcome == symcount::verdict::declined) {
      worker_decline ("symcount_declined");
      worker_decline_detail (result.reason.c_str ());
      return EXIT_CODE_UNKNOWN;
    }
    const bool real = result.outcome == symcount::verdict::realizable;
    if (auto* worker = active_worker_record ())
      worker_record_text (worker->proof_polarity, real ? "REAL" : "UNREAL");
    return real ? EXIT_CODE_REAL : EXIT_CODE_UNREAL;
  }
}
