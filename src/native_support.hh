#pragma once

#if ACACIA_NATIVE_ARMS
# include <string_view>
# include <tlsf/gr1_check.h>
# include <tlsf/gr1_lift.h>
# include <tlsf/gr1_reduction.h>

namespace acacia {
  // C results own their allocations even when a route returns early.
  template <typename T, void (*Clear)(T*)> struct native_result_owner {
      T value {};
      ~native_result_owner () { Clear (&value); }
      native_result_owner (const native_result_owner&) = delete;
      native_result_owner& operator= (const native_result_owner&) = delete;
      native_result_owner () = default;
  };

  using native_reduction_owner =
      native_result_owner<TlsfGr1Reduction, tlsf_gr1_reduction_clear>;
  using native_lift_owner =
      native_result_owner<TlsfGr1LiftResult, tlsf_gr1_lift_result_clear>;
  using native_check_owner =
      native_result_owner<TlsfGr1CheckResult, tlsf_gr1_check_result_clear>;

  void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                              std::string_view message);
  void native_diagnostic (bool unreal, std::string_view stage, int status,
                          std::string_view message);
  bool native_limit_address_space (std::string_view arm);
}
#endif
