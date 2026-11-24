#include "phase_records.hh"
#include "solver/synthesis_rendezvous.hh"

#ifdef ACACIA_TLSF_PREPARATION_TEST_HOOKS
# include <cstdio>
# include <cstdlib>
# include <cstring>
# include <tlsf/convert.h>
# include <tlsf/dpa.h>
# include <unistd.h>

namespace {
  const pid_t preparation_parent = getpid ();
}
extern "C" TlsfConversionResult* __real_tlsf_convert (const char*, const char*);
extern "C" TlsfConversionResult* __wrap_tlsf_convert (const char* source,
                                                      const char* effective_target) {
  if (getpid () == preparation_parent && std::getenv ("ACACIA_TEST_TLSF_PARENT_FAILURE"))
    return nullptr;
  return __real_tlsf_convert (source, effective_target);
}

// Faults stay in the integration executable; production reads no test settings.
extern "C" TlsfDpaStatus __real_tlsf_dpa_create (const TlsfAstNode*, const char* const*, uint32_t,
                                                 const char* const*, uint32_t,
                                                 const TlsfDpaBudget*, TlsfDpa**);
extern "C" TlsfDpaStatus __wrap_tlsf_dpa_create (const TlsfAstNode* root,
                                                 const char* const* inputs, uint32_t ni,
                                                 const char* const* outputs, uint32_t no,
                                                 const TlsfDpaBudget* budget, TlsfDpa** out) {
  if (std::getenv ("ACACIA_TEST_DPA_CREATE_TRACE"))
    std::fprintf (stderr, "test_dpa_create %u %llu\n", unsigned (budget->allow_unproved),
                  (unsigned long long) budget->deadline_mono_ns);
  if (const char* decline = std::getenv ("ACACIA_TEST_DPA_CREATE_DECLINE")) {
    const char* names[] = {"state", "host", "bdd", "deadline"};
    const TlsfDpaReason reasons[] = {TLSF_DPA_STATE_LIMIT, TLSF_DPA_HOST_LIMIT, TLSF_DPA_BDD_LIMIT,
                                     TLSF_DPA_DEADLINE};
    for (unsigned i = 0; i < 4; ++i)
      if (std::strcmp (decline, names[i]) == 0) {
        *out = nullptr;
        return {TLSF_DPA_DECLINED, reasons[i]};
      }
  }
  auto capped = *budget;
  if (std::getenv ("ACACIA_TEST_DPA_STATE_LIMIT"))
    capped.max_states = 2;
  return __real_tlsf_dpa_create (root, inputs, ni, outputs, no, &capped, out);
}
extern "C" int __real_bdd_ite (int, int, int);
extern "C" int __wrap_bdd_ite (int f, int g, int h) {
  if (std::getenv ("ACACIA_TEST_DPA_IMPORT_ALLOCATION"))
    throw std::bad_alloc ();
  return __real_bdd_ite (f, g, h);
}
#endif

#if defined(ACACIA_PORTFOLIO_TEST_HOOKS) && !defined(NDEBUG)
# include <cerrno>
# include <cstdio>
# include <cstdlib>
# include <fstream>
# include <poll.h>
# include <sys/mman.h>
# include <sys/resource.h>
# include <sys/wait.h>
# include <unistd.h>

namespace acacia::synthesis {
  namespace {
    bool obligation_worker = false, parity_worker = false;
    volatile unsigned char* latent_memory = nullptr;
    size_t latent_size = 0;
    volatile unsigned char* cow_memory = nullptr;
    size_t cow_size = 0;
    void allocate (const char* setting) {
      if (const char* bytes = std::getenv (setting)) {
        const auto size = std::strtoull (bytes, nullptr, 10);
        auto* memory = static_cast<volatile unsigned char*> (std::malloc (size));
        if (!memory) {
          std::fprintf (stderr, "{\"stage\":\"synthesis_allocation_failed\",\"pid\":%ld}\n",
                        long (getpid ()));
          throw std::bad_alloc ();
        }
        for (size_t i = 0; i < size; i += 4096)
          memory[i] = 1;
      }
    }
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
  void test_before_verdict (int& verdict) {
    const char* cow = !parity_worker && !obligation_worker
                          ? std::getenv ("ACACIA_TEST_SYNTHESIS_LEGACY_COW_BYTES")
                          : nullptr;
    if (cow || (parity_worker && std::getenv ("ACACIA_TEST_SYNTHESIS_PARITY_DESCENDANT"))) {
      if (cow) {
        cow_size = std::strtoull (cow, nullptr, 10);
        void* mapping =
            mmap (nullptr, cow_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED)
          throw std::bad_alloc ();
        cow_memory = static_cast<volatile unsigned char*> (mapping);
        for (size_t i = 0; i < cow_size; i += 4096)
          cow_memory[i] = 1;
      }
      const pid_t child = fork ();
      if (child < 0)
        throw std::runtime_error ("test descendant fork failed");
      if (child == 0)
        while (true)
          pause ();
      auto* record = active_worker_record ();
      record->descendant_pid = child;
      descendant_record (*record, child, nullptr);
      std::fprintf (stderr, "{\"stage\":\"synthesis_descendant\",\"pid\":%ld}\n", long (child));
    }
    if (!parity_worker && !obligation_worker)
      allocate ("ACACIA_TEST_SYNTHESIS_LEGACY_RSS_BYTES");
    if (parity_worker) {
      if (std::getenv ("ACACIA_TEST_SYNTHESIS_CONFLICT"))
        verdict = EXIT_CODE_UNREAL;
      allocate ("ACACIA_TEST_SYNTHESIS_PARITY_RSS_BYTES");
      if (const char* bytes = std::getenv ("ACACIA_TEST_SYNTHESIS_PARITY_LATENT_BYTES")) {
        latent_size = std::strtoull (bytes, nullptr, 10);
        void* mapping = mmap (nullptr, latent_size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED)
          throw std::bad_alloc ();
        latent_memory = static_cast<volatile unsigned char*> (mapping);
      }
      if (std::getenv ("ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD")) {
        gate ("synthesis_ready", "ACACIA_TEST_SYNTHESIS_PARITY_VERDICT_FD");
        for (size_t i = 0; i < latent_size; i += 4096)
          latent_memory[i] = 1;
        return;
      }
    }
    const bool helper =
        obligation_worker && std::getenv ("ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD");
    gate ("synthesis_ready", helper ? "ACACIA_TEST_SYNTHESIS_OBLIGATION_VERDICT_FD"
                                    : "ACACIA_TEST_SYNTHESIS_VERDICT_FD");
  }
  void test_after_selection () {
    gate ("synthesis_selected",
          parity_worker && std::getenv ("ACACIA_TEST_SYNTHESIS_PARITY_EXPORT_FD")
              ? "ACACIA_TEST_SYNTHESIS_PARITY_EXPORT_FD"
              : "ACACIA_TEST_SYNTHESIS_EXPORT_FD");
    for (size_t i = 0; i < cow_size; i += 4096)
      cow_memory[i] = 2;
    if (!parity_worker && !obligation_worker &&
        std::getenv ("ACACIA_TEST_SYNTHESIS_LEGACY_EXPORT_RSS_BYTES")) {
      allocate ("ACACIA_TEST_SYNTHESIS_LEGACY_EXPORT_RSS_BYTES");
      gate ("synthesis_grown", "ACACIA_TEST_SYNTHESIS_EXPORT_FD");
    }
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
  void test_before_selection () {
    gate ("synthesis_selecting", "ACACIA_TEST_SYNTHESIS_SELECTION_FD");
  }
  void test_verdict_published () {
    if (std::getenv ("ACACIA_TEST_SYNTHESIS_SELECTION_FD"))
      std::fprintf (stderr, "{\"stage\":\"synthesis_proved\",\"pid\":%ld}\n", long (getpid ()));
  }
  void test_after_extraction_cleanup () {
    gate ("synthesis_reclaimed", "ACACIA_TEST_SYNTHESIS_RECLAIM_FD");
  }
  void test_extraction_turn (int worker_pid, uint64_t quantum, uint64_t started_ns,
                             uint64_t deadline_ns) {
    if (std::getenv ("ACACIA_TEST_SYNTHESIS_GRACE_FD"))
      std::fprintf (stderr,
                    "{\"stage\":\"synthesis_turn\",\"worker_pid\":%d,\"quantum_ns\":%llu,"
                    "\"started_ns\":%llu,\"deadline_ns\":%llu}\n",
                    worker_pid, (unsigned long long) quantum, (unsigned long long) started_ns,
                    (unsigned long long) deadline_ns);
  }
  bool test_extraction_grace (bool expired, uint64_t& deadline_mono_ns, int worker_pid) {
    const char* descriptor = std::getenv ("ACACIA_TEST_SYNTHESIS_GRACE_FD");
    if (!descriptor)
      return expired;
    pollfd fd {std::atoi (descriptor), POLLIN, 0};
    char release;
    if (poll (&fd, 1, 0) != 1 || !(fd.revents & POLLIN) || read (fd.fd, &release, 1) != 1)
      return false;
    if (release == 't') {
      deadline_mono_ns = 1;
      return false;
    }
    if (release == 'e') {
      std::fprintf (stderr, "{\"stage\":\"synthesis_exit_boundary\"}\n");
      siginfo_t finished {};
      while (waitid (P_PID, worker_pid, &finished, WEXITED | WNOWAIT) < 0)
        if (errno != EINTR)
          throw std::runtime_error ("test wait at grace boundary failed");
    }
    return true;
  }
  void test_before_reap (int worker_pid) {
    if (!std::getenv ("ACACIA_TEST_SYNTHESIS_REAP_FD"))
      return;
    gate ("synthesis_reaping", "ACACIA_TEST_SYNTHESIS_REAP_FD");
    // The test interrupts the parent while gated. Keep the child waitable so
    // the ordinary loop must reap the handler's SIGKILL before checking its flag.
    siginfo_t finished {};
    while (waitid (P_PID, worker_pid, &finished, WEXITED | WNOWAIT) < 0)
      if (errno != EINTR)
        throw std::runtime_error ("test wait for interrupted worker failed");
  }
}

#ifdef __linux__
extern "C" int __real_waitid (idtype_t, id_t, siginfo_t*, int);
extern "C" pid_t __real_wait4 (pid_t, int*, int, rusage*);
extern "C" pid_t __wrap_wait4 (pid_t pid, int* status, int flags, rusage* usage) {
  if (const char* path = std::getenv ("ACACIA_TEST_SYNTHESIS_DISCARD_PID")) {
    std::ifstream pending (path);
    pid_t discarded = -1;
    pending >> discarded;
    if (pid == discarded) {
      static bool reported = false;
      if (!reported) {
        std::fprintf (stderr, "{\"stage\":\"synthesis_discard_pending\"}\n");
        reported = true;
      }
      pollfd fd {std::atoi (std::getenv ("ACACIA_TEST_SYNTHESIS_DISCARD_FD")), POLLIN, 0};
      if (!(flags & WNOHANG)) {
        char byte;
        while (read (fd.fd, &byte, 1) < 0 && errno == EINTR) {
        }
      }
      else if (poll (&fd, 1, 0) == 0)
        return 0;
    }
  }
  return __real_wait4 (pid, status, flags, usage);
}
extern "C" int __wrap_waitid (idtype_t type, id_t pid, siginfo_t* info, int flags) {
  if ((flags & WSTOPPED) && std::getenv ("ACACIA_TEST_SYNTHESIS_STOP_FD")) {
    static bool reported = false;
    if (!reported) {
      std::fprintf (stderr, "{\"stage\":\"synthesis_stop_pending\"}\n");
      reported = true;
    }
    if (!(flags & WNOHANG)) {
      char byte;
      (void) read (std::atoi (std::getenv ("ACACIA_TEST_SYNTHESIS_STOP_FD")), &byte, 1);
    }
    *info = {};
    return 0;
  }
  return __real_waitid (type, pid, info, flags);
}
# endif
#endif
