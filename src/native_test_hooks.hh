#pragma once
#if ACACIA_NATIVE_ARMS && defined(ACACIA_NATIVE_TEST_HOOKS)
# include <tlsf/gr1_lift.h>

# include <string>
namespace acacia {
  void native_corrupt_gr1_artifact (std::string& artifact);
  void native_lift_test_options (TlsfGr1LiftOptions& options);
  void native_lift_test_fault (TlsfGr1LiftResult& result);
  void native_attribution_test_exception ();
  void native_attribution_test_pause (TlsfGr1BothEventKind kind, const char* route);
}
#endif
