#include "phase_records.hh"
#include "solver/synthesis_rendezvous.hh"

#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
# include <cerrno>
# include <cstdio>
# include <cstdlib>
# include <fstream>
# include <unistd.h>

namespace acacia::synthesis {
  namespace {
    bool obligation_worker = false, parity_worker = false;
    void gate (const char* stage, const char* descriptor) {
      const char* value = std::getenv (descriptor);
      if (!value)
        return;
      legacy_phase readiness (stage);
      char line[192];
      const int length =
          snprintf (line, sizeof line, "{\"stage\":\"%s\",\"pid\":%ld,\"worker_kind\":\"%s\"}\n",
                    stage, long (getpid ()),
                    parity_worker       ? "parity"
                    : obligation_worker ? "obligation"
                                        : "legacy");
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
  void test_worker_started (bool obligation, bool parity) {
    obligation_worker = obligation;
    parity_worker = parity;
  }
  void test_before_worker_protection () {
    gate ("synthesis_starting", "ACACIA_TEST_SYNTHESIS_STARTUP_FD");
  }
  void test_before_verdict () {
    if (parity_worker) {
      if (const char* bytes = std::getenv ("ACACIA_TEST_SYNTHESIS_PARITY_RSS_BYTES")) {
        const auto size = std::strtoull (bytes, nullptr, 10);
        auto* memory = static_cast<volatile unsigned char*> (std::malloc (size));
        if (!memory)
          throw std::bad_alloc ();
        for (size_t i = 0; i < size; i += 4096)
          memory[i] = 1;
      }
      if (std::getenv ("ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD")) {
        gate ("synthesis_ready", "ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD");
        return;
      }
    }
    const bool helper =
        obligation_worker && std::getenv ("ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD");
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
