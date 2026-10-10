#include <tlsf/gr1_lift.h>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
  void check (bool condition) {
    assert (condition);
    if (!condition)
      std::abort ();
  }

  struct observation {
      std::vector<TlsfGr1BothEventKind> events;
      TlsfGr1LiftStatus failure = TLSF_GR1_LIFT_OK;
      bool in_check = false;
      unsigned cancel_after = 0, check_polls = 0;
  };
  int cancelled (void* context) {
    auto& state = *static_cast<observation*> (context);
    return state.in_check && state.cancel_after && ++state.check_polls >= state.cancel_after;
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
  for (unsigned cancel_after : {0u, 1u, 2u}) {
    const bool cancel = cancel_after != 0;
    observation state;
    state.cancel_after = cancel_after;
    options.cancelled = cancelled;
    options.cancel_ctx = &state;
    TlsfGr1BothObserverV1 observer {observe, &state};
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
    TlsfGr1BothObserverV1 observer {observe, &state};
    TlsfGr1BothResult result {};
    const auto status = tlsf_gr1_both_from_target_v1 (invalid ? nullptr : target, &options,
                                                      &observer, &result, &error);
    check (status == (invalid ? TLSF_GR1_LIFT_INVALID : TLSF_GR1_LIFT_DEADLINE));
    check (!state.events.empty () && state.events.back () == TLSF_GR1_BOTH_EVENT_STOPPED);
    check (state.failure == status);
    check (!result.target_checks && !result.proof.game_aag && !result.proof.certificate_aag);
    tlsf_gr1_both_result_clear (&result);
  }
  tlsf_gr1_lift_target_free (target);
}
