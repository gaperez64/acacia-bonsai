#pragma once

#if !__has_include("acacia_build_config.hh")
# error "Configure the build through Meson. scripts/acacia-config.py meson-args PRESET produces the option set for a named configuration."
#endif
#include "acacia_build_config.hh"

#include "utils/todo.hh"

// Every compile-time option, its default and its rationale live in
// config/acacia-options.json; this file deliberately states none of them, so
// there is no second answer to "what is the default".  The DEFAULT_K /
// VECTOR_ELT_T overflow constraint is recorded there under constants.

#include "config/derived_gates.hh"
#include "config/validate.hh"

#ifdef NO_SIMD
# pragma message("Compiling without SIMD")
#else
# pragma message("Compiling with SIMD")
#endif
