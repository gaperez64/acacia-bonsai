#include "configuration.hh"
#include "native_test_hooks.hh"

#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <unistd.h>

namespace acacia {
  void native_attribution_test_exception () {
    const char* mode = std::getenv ("ACACIA_NATIVE_TEST_EXCEPTION");
    if (!mode)
      return;
    if (std::strcmp (mode, "bad-alloc") == 0)
      throw std::bad_alloc {};
    if (std::strcmp (mode, "standard") == 0)
      throw std::runtime_error ("injected exception");
    if (std::strcmp (mode, "unknown") == 0)
      throw 1;
  }

  void native_attribution_test_pause (TlsfGr1BothEventKind kind, const char* route) {
    const char* pause = std::getenv ("ACACIA_TEST_ATTRIBUTION_PAUSE");
    if (pause &&
        ((kind == TLSF_GR1_BOTH_EVENT_SELECTED && std::strcmp (pause, route) == 0) ||
         (kind == TLSF_GR1_BOTH_EVENT_CHECK_START && std::strcmp (pause, "checking") == 0)))
      while (true)
        ::pause ();
  }
}
