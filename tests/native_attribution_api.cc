#include <tlsf/gr1_lift.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  void check (bool condition) {
    assert (condition);
    if (!condition)
      std::abort ();
  }

  struct observation {
      std::vector<TlsfGr1BothEventKind> events;
      std::vector<std::string> phases;
      unsigned phase_entries = 0;
      TlsfGr1LiftStatus failure = TLSF_GR1_LIFT_OK;
      bool in_check = false;
      unsigned cancel_after = 0, check_polls = 0;
  };
  int cancelled (void* context) {
    auto& state = *static_cast<observation*> (context);
    return state.in_check && state.cancel_after && ++state.check_polls >= state.cancel_after;
  }
  void phase (void* context, const char* name, int entering, const char* artifact) {
    auto& state = *static_cast<observation*> (context);
    if (entering) {
      ++state.phase_entries;
      state.phases.emplace_back (name);
      if (artifact)
        check (std::strlen (artifact) == 64);
    }
    else {
      check (!state.phases.empty () && state.phases.back () == name);
      state.phases.pop_back ();
    }
  }
  void broken_phase (void*, const char*, int, const char*) {
    throw std::runtime_error ("recording unavailable");
  }
  void observe (void* context, TlsfGr1BothEventKind kind, TlsfGr1BothEventRoute route, int side,
                const char*, const char*, TlsfGr1LiftStatus failure) {
    auto& state = *static_cast<observation*> (context);
    state.events.push_back (kind);
    if (kind == TLSF_GR1_BOTH_EVENT_STOPPED)
      state.failure = failure;
    if (kind == TLSF_GR1_BOTH_EVENT_CHECK_START) {
      check (route == TLSF_GR1_BOTH_EVENT_DIRECT && side == 0);
      state.in_check = true;
    }
  }
}

int main () {
  constexpr char source[] =
      "INFO { TITLE: \"plain\" SEMANTICS: Mealy TARGET: Mealy }\n"
      "MAIN { INPUTS { a; } OUTPUTS { b; } GUARANTEES { G F b; } }\n";
  TlsfGr1LiftOptions options {};
  options.solver_nodes = options.checker_nodes = 1u << 20;
  options.solver_cache = options.checker_cache = 1u << 18;
  TlsfGr1LiftTarget* target = nullptr;
  TlsfGr1LiftError error {};
  const auto prepared = tlsf_gr1_lift_target_prepare_exact (
      reinterpret_cast<const uint8_t*> (source), sizeof source - 1, &options, &target, &error);
  check (prepared == TLSF_GR1_LIFT_OK && target);
  // Optional observations, including a failed observer, must leave the proof
  // artifacts and amount of solver/checker work identical to an unobserved call.
  TlsfGr1BothResult baseline {};
  check (tlsf_gr1_both_from_target_v1 (target, &options, nullptr, &baseline, &error) ==
         TLSF_GR1_LIFT_OK);
  for (auto callback : {phase, broken_phase}) {
    observation state;
    TlsfGr1BothObserverV1 observer {observe, &state, callback};
    TlsfGr1BothResult recorded {};
    check (tlsf_gr1_both_from_target_v1 (target, &options, &observer, &recorded, &error) ==
           TLSF_GR1_LIFT_OK);
    check (recorded.route == baseline.route && recorded.seed_polarity == baseline.seed_polarity &&
           recorded.target_checks == baseline.target_checks &&
           recorded.target_reductions == baseline.target_reductions &&
           recorded.seed_probes == baseline.seed_probes &&
           recorded.seed_reductions == baseline.seed_reductions &&
           recorded.seed_solves == baseline.seed_solves &&
           recorded.seed_checks == baseline.seed_checks &&
           recorded.seed_cache_hits == baseline.seed_cache_hits);
    const auto same = [] (const char* left, size_t left_size, const char* right,
                          size_t right_size) {
      return left_size == right_size && (!left_size || std::memcmp (left, right, left_size) == 0);
    };
    const auto& a = baseline.proof;
    const auto& b = recorded.proof;
    check (a.method == b.method && a.verdict == b.verdict && a.semantics == b.semantics);
    check (same (a.game_aag, a.game_size, b.game_aag, b.game_size));
    check (same (a.certificate_aag, a.certificate_size, b.certificate_aag, b.certificate_size));
    check (same (a.certificate_json, a.certificate_json_size, b.certificate_json,
                 b.certificate_json_size));
    check (same (a.policy_aag, a.policy_size, b.policy_aag, b.policy_size));
    check (same (a.policy_json, a.policy_json_size, b.policy_json, b.policy_json_size));
    if (callback == phase)
      check (state.phase_entries >= 3 && state.phases.empty ());
    tlsf_gr1_both_result_clear (&recorded);
  }
  tlsf_gr1_both_result_clear (&baseline);
  for (unsigned cancel_after : {0u, 1u, 2u}) {
    const bool cancel = cancel_after != 0;
    observation state;
    state.cancel_after = cancel_after;
    options.cancelled = cancelled;
    options.cancel_ctx = &state;
    TlsfGr1BothObserverV1 observer {observe, &state, phase};
    TlsfGr1BothResult result {};
    const auto status =
        tlsf_gr1_both_from_target_v1 (target, &options, &observer, &result, &error);
    const std::vector<TlsfGr1BothEventKind> expected {
        TLSF_GR1_BOTH_EVENT_SELECTED,
        TLSF_GR1_BOTH_EVENT_START,
        TLSF_GR1_BOTH_EVENT_DECLINE,
        TLSF_GR1_BOTH_EVENT_SELECTED,
        TLSF_GR1_BOTH_EVENT_START,
        TLSF_GR1_BOTH_EVENT_CHECK_START,
        cancel ? TLSF_GR1_BOTH_EVENT_STOPPED : TLSF_GR1_BOTH_EVENT_VERIFIED};
    check (state.events == expected);
    check (state.phase_entries >= 3 && state.phases.empty ());
    if (cancel_after == 2)
      check (state.check_polls >= 2);
    check (status == (cancel_after == 1   ? TLSF_GR1_LIFT_CANCELLED
                       : cancel_after == 2 ? TLSF_GR1_LIFT_DECLINED
                                           : TLSF_GR1_LIFT_OK));
    check (result.target_checks == (cancel_after == 1 ? 0 : 1));
    if (cancel) {
      check (state.failure == TLSF_GR1_LIFT_CANCELLED);
      check (!result.proof.game_aag && !result.proof.certificate_aag);
      check (std::strcmp (error.stage, "target_check") == 0);
    }
    else {
      check (tlsf_gr1_lift_target_matches (target, &result.proof));
    }
    tlsf_gr1_both_result_clear (&result);
  }
  for (bool invalid : {false, true}) {
    options.cancelled = nullptr;
    options.cancel_ctx = nullptr;
    options.deadline_mono_ns = 1;
    observation state;
    TlsfGr1BothObserverV1 observer {observe, &state, phase};
    TlsfGr1BothResult result {};
    const auto status = tlsf_gr1_both_from_target_v1 (invalid ? nullptr : target, &options,
                                                      &observer, &result, &error);
    check (status == (invalid ? TLSF_GR1_LIFT_INVALID : TLSF_GR1_LIFT_DEADLINE));
    check (!state.events.empty () && state.events.back () == TLSF_GR1_BOTH_EVENT_STOPPED);
    check (state.failure == status);
    check (state.phases.empty ());
    check (!result.target_checks && !result.proof.game_aag && !result.proof.certificate_aag);
    tlsf_gr1_both_result_clear (&result);
  }
  tlsf_gr1_lift_target_free (target);
}
