#include "solver/synthesis_rendezvous.hh"
#include "phase_records.hh"

#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace acacia::synthesis {
  namespace {
    bool obligation_worker = false;
    void gate (const char* stage, const char* descriptor) {
      const char* value = std::getenv (descriptor);
      if (!value)
        return;
      legacy_phase readiness (stage);
      char line[128];
      const int length = snprintf (line, sizeof line,
                                   "{\"stage\":\"%s\",\"pid\":%ld}\n", stage, long (getpid ()));
      if (write (STDERR_FILENO, line, size_t (length)) != length)
        throw std::runtime_error ("test readiness failed");
      char release;
      ssize_t got;
      do {
        got = read (std::atoi (value), &release, 1);
      } while (got < 0 && errno == EINTR);
      if (got < 0)
        throw std::runtime_error ("test gate failed");
    }
  }
  void test_worker_started (bool obligation) { obligation_worker = obligation; }
  void test_before_worker_protection () {
    gate ("synthesis_starting", "ACACIA_TEST_SYNTHESIS_STARTUP_FD");
  }
  void test_before_verdict () {
    const bool helper = obligation_worker &&
                        std::getenv ("ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD");
    gate ("synthesis_ready", helper ? "ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD"
                                    : "ACACIA_TEST_SYNTHESIS_VERDICT_FD");
  }
  void test_after_selection () {
    gate ("synthesis_selected", "ACACIA_TEST_SYNTHESIS_EXPORT_FD");
    if (const char* path = std::getenv ("ACACIA_TEST_SYNTHESIS_FAIL_PID")) {
      std::ifstream failure (path);
      pid_t pid = -1;
      failure >> pid;
      if (pid == getpid ())
        throw std::runtime_error ("test controller construction failed");
    }
  }
  void test_before_publication () {
    gate ("synthesis_publishing", "ACACIA_TEST_SYNTHESIS_PUBLICATION_FD");
  }
}
#endif
