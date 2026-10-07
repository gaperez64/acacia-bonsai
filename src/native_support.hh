#pragma once

#include "configuration.hh"

#if ACACIA_NATIVE_ARMS
# include <string_view>
# include <tlsf/gr1_check.h>
# include <tlsf/gr1_lift.h>
# include <tlsf/gr1_reduction.h>
# include <tlsf/oxidd_options.h>

# include <string>
# include <tlsf/pipeline.h>

struct arg_parse_result;

namespace acacia {
# ifdef ACACIA_NATIVE_TEST_HOOKS
  void native_attribution_test_exception ();
# endif
  // C results own their allocations even when a route returns early.
  template <typename T, void (*Clear) (T*)>
  struct native_result_owner {
      T value {};
      ~native_result_owner () { Clear (&value); }
      native_result_owner (const native_result_owner&) = delete;
      native_result_owner& operator= (const native_result_owner&) = delete;
      native_result_owner () = default;
  };

  using native_reduction_owner = native_result_owner<TlsfGr1Reduction, tlsf_gr1_reduction_clear>;
  using native_lift_owner = native_result_owner<TlsfGr1LiftResult, tlsf_gr1_lift_result_clear>;
  using native_check_owner = native_result_owner<TlsfGr1CheckResult, tlsf_gr1_check_result_clear>;

  enum class native_failure_category { decline, resource, deadline, cancelled, error };
  void native_dual_gr1_after_rejection (const arg_parse_result& args, const char* arm,
                                        uint64_t deadline_mono_ns,
                                        const TlsfGr1ConstructionBudget& budget,
                                        std::string_view stage, native_failure_category cause);
  native_failure_category native_failure (TlsfGr1LiftStatus status);
  native_failure_category native_failure (TlsfGr1ReductionStatus status);
  native_failure_category native_failure (TlsfGr1CheckStatus status, TlsfGr1CheckVerdict verdict);
  native_failure_category native_failure (TlsfPipelineStatus status);
  native_failure_category native_failure (OxiddFailureKind kind);
  const char* native_failure_reason (native_failure_category category);
  void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                              std::string_view message,
                              native_failure_category category = native_failure_category::error);
  void native_diagnostic (std::string_view arm, std::string_view stage, int status,
                          std::string_view message,
                          native_failure_category category = native_failure_category::error);
  bool native_limit_address_space (std::string_view arm);
  TlsfGr1ConstructionBudget native_construction_budget (size_t arm_count);
  std::string native_budget_message (std::string_view stage, std::string_view reason,
                                     const TlsfGr1ConstructionWork& work);
  void native_budget_record (std::string_view arm, std::string_view stage,
                             const TlsfGr1ConstructionWork& work, uint64_t started_ns) noexcept;
}
#endif
