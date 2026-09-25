// Test-only LD_PRELOAD replacement for the Spot call made by native reduction.
// It reports the calling child PID, then holds that call until the parent kills it.
#include <spot/twaalgos/translate.hh>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

spot::twa_graph_ptr spot::translator::run (spot::formula*) {
  const char* value = std::getenv ("ACACIA_NATIVE_TEST_SPOT_FD");
  if (!value) _exit (90);
  char* end = nullptr;
  const long fd = std::strtol (value, &end, 10);
  if (end == value || *end || fd < 0) _exit (91);
  char pid[32];
  const int size = std::snprintf (pid, sizeof pid, "%ld\n", long (getpid ()));
  if (size <= 0 || ::write (int (fd), pid, size) != size) _exit (92);
  for (;;) pause ();
}
