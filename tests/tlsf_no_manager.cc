#include <oxidd/capi.h>

#include <cstdio>
#include <cstdlib>

// Linker interception of the real FFI boundary: conversion in the Acacia
// parent must not construct a manager, even for a precheck-friendly formula.
extern "C" oxidd::capi::oxidd_bdd_manager_t
__wrap_oxidd_bdd_manager_new (size_t, size_t, uint32_t) {
  std::fputs ("TLSF conversion unexpectedly constructed an OxiDD manager\n", stderr);
  std::abort ();
}
