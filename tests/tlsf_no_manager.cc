#include <oxidd/capi.h>
extern "C" {
#include <tlsf/cover.h>
}

#include <cstdio>
#include <cstdlib>

// Linker interception of the real FFI boundary: conversion in the Acacia
// parent must not construct a manager, even for a precheck-friendly formula.
extern "C" oxidd::capi::oxidd_bdd_manager_t __wrap_oxidd_bdd_manager_new (size_t, size_t,
                                                                          uint32_t) {
  std::fputs ("TLSF conversion unexpectedly constructed an OxiDD manager\n", stderr);
  std::abort ();
}

extern "C" ConstraintCover* __wrap_cover_build (TlsfSpec*, bool) {
  std::fputs ("TLSF conversion unexpectedly constructed a constraint cover\n", stderr);
  std::abort ();
}
