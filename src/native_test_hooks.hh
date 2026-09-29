#pragma once
#if ACACIA_NATIVE_ARMS && defined(ACACIA_NATIVE_TEST_HOOKS)
#include <string>
#include <tlsf/gr1_lift.h>
namespace acacia {
  void native_corrupt_gr1_artifact (std::string& artifact);
  void native_lift_test_options (TlsfGr1LiftPhaseBudget& budget);
  void native_lift_test_fault (TlsfGr1LiftResult& result);
}
#endif
