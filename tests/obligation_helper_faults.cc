// Test-only ELF interposition. Never linked into or installed with a solver.
#include <string_view>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
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
  bool controller_opened = false;
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
    if (obligation_entered && fault == "export-stall")
      while (true)
        pause ();
    if (obligation_entered && fault == "export-stream") {
      os.setstate (std::ios::badbit);
      return os;
    }
    if (fault == "writer-contract" &&
        (!controller_opened || !dynamic_cast<std::ofstream*> (&os)))
      throw std::runtime_error ("controller must use the ordinary file writer");
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
  FILE* controller_fopen (const char* path, const char* mode, const char* symbol) {
    const char* destination = std::getenv ("ACACIA_TEST_OBLIGATION_OUTPUT");
    const bool controller = destination && std::strcmp (path, destination) == 0;
    const char* fault = std::getenv ("ACACIA_TEST_OBLIGATION_FAULT");
    if (controller && obligation_entered && fault && std::strcmp (fault, "export-open") == 0) {
      errno = EIO;
      return nullptr;
    }
    using function = FILE* (*) (const char*, const char*);
    auto original = reinterpret_cast<function> (dlsym (RTLD_NEXT, symbol));
    FILE* file = original (path, mode);
    if (controller && file)
      controller_opened = true;
    return file;
  }
}

extern "C" FILE* test_fopen (const char*, const char*) asm ("fopen");
extern "C" FILE* test_fopen (const char* path, const char* mode) {
  return controller_fopen (path, mode, "fopen");
}

extern "C" FILE* fopen64 (const char* path, const char* mode) {
  return controller_fopen (path, mode, "fopen64");
}
