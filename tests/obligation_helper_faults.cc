// Test-only ELF interposition. Never linked into or installed with a solver.
#include <string_view>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <dlfcn.h>
#include <new>
#include <ostream>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/mtdswa.hh>
#include <spot/twaalgos/synthesis.hh>
#include <stdexcept>
#include <time.h>
#include <unistd.h>

namespace {
  bool obligation_entered = false;
  unsigned obligation_calls = 0;
  void delay () {
    if (const char* value = std::getenv ("ACACIA_TEST_OBLIGATION_LEGACY_DELAY_MS")) {
      const long ms = std::strtol (value, nullptr, 10);
      timespec wait {ms / 1000, (ms % 1000) * 1000000};
      nanosleep (&wait, nullptr);
    }
  }
}
namespace spot {
  mtdswa_ptr obligation_synthesis (formula f, const bdd_dict_ptr& dict,
                                   const std::vector<std::string>& outvars, bool realizability,
                                   bool simplify_terms, int debug) {
    obligation_entered = true;
    ++obligation_calls;
    const char* mode = std::getenv ("ACACIA_TEST_OBLIGATION_FAULT");
    const std::string_view fault = mode ? mode : "";
    if (fault == "decision-contract" && (!realizability || obligation_calls != 1))
      throw std::runtime_error ("decision contract violated");
    if (fault == "slow-decision") {
      timespec wait {5, 200000000};
      nanosleep (&wait, nullptr);
    }
    if (fault == "null")
      return nullptr;
    if (fault == "empty") {
      auto answer = std::make_shared<mtdswa> (dict);
      answer->acc = acc_cond::acc_code::f ();
      return answer;
    }
    if (fault == "exception")
      throw std::runtime_error ("test exception");
    if (fault == "allocation")
      throw std::bad_alloc ();
    if (fault == "unreal-exit")
      _exit (1);
    if (fault == "real-exit")
      _exit (0);
    if (fault == "signal")
      _exit (2);
    if (fault == "memory") {
      auto* memory = new unsigned char[96 * 1024 * 1024];
      for (size_t at = 0; at < 96 * 1024 * 1024; at += 4096)
        static_cast<volatile unsigned char*> (memory)[at] = 1;
      while (true)
        pause ();
    }
    if (fault == "stall")
      while (true)
        pause ();
    using function = decltype (&obligation_synthesis);
    auto original = reinterpret_cast<function> (dlsym (
        RTLD_NEXT,
        "_ZN4spot20obligation_synthesisENS_7formulaERKSt10shared_ptrINS_8bdd_"
        "dictEERKSt6vectorINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESaISC_EEbbi"));
    if (!original)
      throw std::runtime_error ("test interposition lookup failed");
    return original (fault == "wrong-strategy" ? formula::Not (f) : f, dict, outvars,
                     realizability, simplify_terms, debug);
  }

  std::ostream& print_aiger (std::ostream& os, const_aig_ptr circuit) {
    if (!obligation_entered)
      delay ();
    const char* mode = std::getenv ("ACACIA_TEST_OBLIGATION_FAULT");
    const std::string_view fault = mode ? mode : "";
    if (obligation_entered && fault == "export-exception")
      throw std::runtime_error ("test export failure");
    if (obligation_entered && fault == "corrupt-controller") {
      auto mutable_circuit = std::const_pointer_cast<aig> (circuit);
      for (unsigned i = 0; i < circuit->num_outputs (); ++i)
        mutable_circuit->set_output (i, circuit->output (i) ^ 1U);
    }
    using function = std::ostream& (*) (std::ostream&, const_aig_ptr);
    auto original = reinterpret_cast<function> (
        dlsym (RTLD_NEXT, "_ZN4spot11print_aigerERSoSt10shared_ptrIKNS_3aigEE"));
    if (!original)
      throw std::runtime_error ("test export lookup failed");
    return original (os, circuit);
  }

  twa_graph_ptr aig::as_automaton (bool keepsplit) const {
    const char* mode = std::getenv ("ACACIA_TEST_OBLIGATION_FAULT");
    const std::string_view fault = mode ? mode : "";
    if (obligation_entered && fault == "check-stall")
      while (true)
        pause ();
    if (obligation_entered && fault == "check-exception")
      throw std::runtime_error ("test check failure");
    using function = twa_graph_ptr (*) (const aig*, bool);
    auto original =
        reinterpret_cast<function> (dlsym (RTLD_NEXT, "_ZNK4spot3aig12as_automatonEb"));
    if (!original)
      throw std::runtime_error ("test check lookup failed");
    return original (this, keepsplit);
  }

  mealy_like try_create_direct_strategy (formula f, const std::vector<std::string>& outputs,
                                         synthesis_info& info, bool want_strategy) {
    delay ();
    using function = decltype (&try_create_direct_strategy);
    auto original = reinterpret_cast<function> (
        dlsym (RTLD_NEXT,
               "_ZN4spot26try_create_direct_strategyENS_7formulaERKSt6vectorINSt7__cxx1112basic_"
               "stringIcSt11char_traitsIcESaIcEEESaIS7_EERNS_14synthesis_infoEb"));
    if (!original)
      throw std::runtime_error ("test direct strategy lookup failed");
    return original (f, outputs, info, want_strategy);
  }
}

// Delay/fail original-objective normalization before obligation_synthesis is entered.
extern "C" void
preparation_fault (void*, spot::formula, const std::vector<std::string>&, unsigned, std::ostream*) __asm__ (
    "_ZN4spot24realizability_simplifierC1ENS_7formulaERKSt6vectorINSt7__cxx1112basic_"
    "stringIcSt11char_traitsIcESaIcEEESaIS8_EEjPSo");
extern "C" void preparation_fault (void* self, spot::formula formula,
                                   const std::vector<std::string>& inputs, unsigned options,
                                   std::ostream* verbose) {
  const char* mode = std::getenv ("ACACIA_TEST_OBLIGATION_FAULT");
  const std::string_view fault = mode ? mode : "";
  if (fault == "prepare-stall")
    while (true)
      pause ();
  if (fault == "prepare-allocation")
    throw std::bad_alloc ();
  if (fault == "prepare-exception")
    throw std::runtime_error ("test preparation exception");
  if (fault == "prepare-real-exit")
    _exit (0);
  if (fault == "prepare-unreal-exit")
    _exit (1);
  using function =
      void (*) (void*, spot::formula, const std::vector<std::string>&, unsigned, std::ostream*);
  auto original = reinterpret_cast<function> (
      dlsym (RTLD_NEXT,
             "_ZN4spot24realizability_simplifierC1ENS_7formulaERKSt6vectorINSt7__cxx1112basic_"
             "stringIcSt11char_traitsIcESaIcEEESaIS8_EEjPSo"));
  if (!original)
    throw std::runtime_error ("test preparation lookup failed");
  original (self, formula, inputs, options, verbose);
}

namespace {
  bool publication_entered = false;
  int publication_output = -1;

  bool publication_fault (std::string_view operation) {
    const char* mode = std::getenv ("ACACIA_TEST_PUBLICATION_FAULT");
    if (!publication_entered || !mode)
      return false;
    const std::string_view fault = mode;
    if (fault == operation) {
      errno = EIO;
      return true;
    }
    if (fault == std::string (operation) + "-delay") {
      timespec wait {1, 200000000};
      nanosleep (&wait, nullptr);
    }
    if (fault == std::string (operation) + "-stall")
      while (true)
        pause ();
    return false;
  }
}

extern "C" ssize_t pread (int fd, void* buffer, size_t size, off_t offset) {
  char descriptor[64], path[4096];
  snprintf (descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
  const auto count = readlink (descriptor, path, sizeof path - 1);
  if (count > 0) {
    path[count] = '\0';
    if (std::strstr (path, ".obligation-") && std::strstr (path, " (deleted)"))
      publication_entered = true;
  }
  if (offset && publication_fault ("pread-late"))
    return -1;
  if (publication_fault ("pread"))
    return -1;
  using function = decltype (&pread);
  auto original = reinterpret_cast<function> (dlsym (RTLD_NEXT, "pread"));
  return original (fd, buffer, size, offset);
}

extern "C" int open (const char* path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start (args, flags);
    mode = va_arg (args, mode_t);
    va_end (args);
  }
  if (publication_fault ("open"))
    return -1;
  using function = int (*) (const char*, int, ...);
  auto original = reinterpret_cast<function> (dlsym (RTLD_NEXT, "open"));
  const int fd = original (path, flags, mode);
  if (publication_entered && (flags & O_WRONLY))
    publication_output = fd;
  return fd;
}

extern "C" ssize_t write (int fd, const void* buffer, size_t size) {
  if (fd == publication_output && publication_fault ("write"))
    return -1;
  using function = decltype (&write);
  auto original = reinterpret_cast<function> (dlsym (RTLD_NEXT, "write"));
  const char* mode = std::getenv ("ACACIA_TEST_PUBLICATION_FAULT");
  if (fd == publication_output && mode && std::string_view (mode) == "write-partial") {
    static bool partial = false;
    if (partial) {
      errno = EIO;
      return -1;
    }
    partial = true;
    return original (fd, buffer, std::min<size_t> (size, 8));
  }
  return original (fd, buffer, size);
}

extern "C" int close (int fd) {
  const bool failed = fd == publication_output && publication_fault ("close");
  using function = decltype (&close);
  auto original = reinterpret_cast<function> (dlsym (RTLD_NEXT, "close"));
  const int result = original (fd);
  return failed ? -1 : result;
}
