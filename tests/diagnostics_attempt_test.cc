#include "solver/diagnostics.hh"

#include <cstdlib>
#include <string>

int main () {
#if ACACIA_ENABLE_DIAGNOSTICS
  setenv ("ACACIA_DIAG", "1", 1);
  acacia::diagnostics::scoped_child child {"test"};
  auto* metrics = acacia::diagnostics::current ();
  if (metrics == nullptr)
    return 1;
  metrics->translation_ms = 7;
  metrics->final_reason = "unknown";

  {
    acacia::diagnostics::scoped_attempt attempt;
    metrics->translation_ms = 19;
    acacia::diagnostics::finish (false, "discarded-witness");
  }
  if (metrics->translation_ms != 7 or metrics->result != "unknown" or
      metrics->final_reason != "unknown")
    return 2;

  {
    acacia::diagnostics::scoped_attempt attempt;
    metrics->translation_ms = 23;
    acacia::diagnostics::finish (true, "accepted-witness");
    attempt.commit ();
  }
  if (metrics->translation_ms != 23 or metrics->result != "solved" or
      metrics->final_reason != "accepted-witness")
    return 3;
#endif
  return 0;
}
