#include "symcount_arm.hh"

#include "error_msg.hh"
#include "phase_records.hh"
#include "solver/encoded_family.hh"
#include "solver/obligation_helper.hh"
#include "solver/symmetric_count.hh"
#include "tlsf_frontend.hh"

#include <spot/tl/parse.hh>

namespace acacia {
  int run_symcount_arm (const tlsf_frontend::specification& objective, std::string_view tlsf_source,
                        uint64_t deadline_mono_ns) {
    validate_ltl_partition (objective.inputs, objective.outputs);
    for (const auto& f : objective.metadata.tlsf_indexed_families ())
      if (f.members.size () > UINT16_MAX) {
        worker_decline ("symcount_declined");
        worker_decline_detail ("index count exceeds uint16_t");
        return EXIT_CODE_UNKNOWN;
      }
    const auto parsed = spot::parse_infix_psl (objective.formula);
    if (!parsed.f || !parsed.errors.empty ()) {
      worker_stopped ("invalid_formula");
      return EXIT_CODE_UNKNOWN;
    }
    symcount::limits bound;
    bound.deadline_mono_ns = deadline_mono_ns;
    // Size from the scope/address-space cap; the parent enforces RSS sharing with parity workers.
    const auto share = obligation::rss_share (1);
    bound.max_vertices = bound.max_edges = std::max<uint64_t> (1, share / 512);
    auto result = symcount::solve (parsed.f, objective.inputs, objective.outputs,
                                   objective.metadata.tlsf_indexed_families (), bound);
    if (result.outcome == symcount::verdict::declined && !tlsf_source.empty () &&
        (!deadline_mono_ns || phase_clock (CLOCK_MONOTONIC) < deadline_mono_ns)) {
      const auto captured = tlsf_frontend::parse_with_value_origins (tlsf_source);
      using namespace std::chrono;
      const auto start = steady_clock::now ();
      const auto now = phase_clock (CLOCK_MONOTONIC);
      const auto remaining =
          deadline_mono_ns ? deadline_mono_ns - std::min (deadline_mono_ns, now) : UINT64_MAX;
      const auto time =
          nanoseconds (std::min<uint64_t> (remaining, seconds (15) / nanoseconds (1)));
      if (const auto cert = symmetry::check_encoded_family (captured, 1u << 22, start + time)) {
        auto encoded = symcount::solve_encoded (objective.formula, objective.inputs,
                                                 objective.outputs, *cert, bound, true);
        if (encoded.lifted && encoded.outcome != symcount::verdict::declined)
          result = std::move (encoded);
      }
    }
    if (result.outcome == symcount::verdict::declined) {
      if (deadline_mono_ns && phase_clock (CLOCK_MONOTONIC) >= deadline_mono_ns)
        result.reason = "deadline";
      worker_decline ("symcount_declined");
      worker_decline_detail (result.reason.c_str ());
      return EXIT_CODE_UNKNOWN;
    }
    const bool real = result.outcome == symcount::verdict::realizable;
    if (result.lifted)
      worker_verified (real ? "REAL" : "UNREAL", real ? "REAL" : "UNREAL");
    else if (auto* worker = active_worker_record ())
      worker_record_text (worker->proof_polarity, real ? "REAL" : "UNREAL");
    return real ? EXIT_CODE_REAL : EXIT_CODE_UNREAL;
  }
}
