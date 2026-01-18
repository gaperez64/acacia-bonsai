#pragma once

#define VERSION 1.9.1

#include "utils/todo.hh"

#ifndef CPRE_AVOID_UNIONS
# define CPRE_AVOID_UNIONS 0
#endif

#ifndef DECOMPOSE_SPEC
# define DECOMPOSE_SPEC 1
#endif

#ifndef NO_ARRAY_CAP_MAX
// define it if you want STATIC_ARRAY_CAP_MAX to be 0
#endif

#ifndef USE_BOOLVEC_OVER_BITSET
// define it if you want to use boolean vectors when
// defaulting to VECTOR_AND_BITSET_DOWNSET_IMPL
#endif

// What follows are default values overriden by self-benchmark.sh
// or your meson setup.
//
// The defaults were copied from self-benchmark.sh 21/12/2025

// Overflow WARNING: Check VECTOR_ELT_T below before changing
#ifndef DEFAULT_K
# define DEFAULT_K 99
#endif
#ifndef DEFAULT_KMIN
# define DEFAULT_KMIN 2
#endif
#ifndef DEFAULT_KINC
# define DEFAULT_KINC 3
#endif
#ifndef DEFAULT_UNREAL_X
# define DEFAULT_UNREAL_X UNREAL_X_BOTH
#endif
#ifndef VECTOR_ELT_T
# define VECTOR_ELT_T signed char
#endif
// End of overflow WARNING

#ifndef STATIC_ARRAY_MAX
# define STATIC_ARRAY_MAX 300
#endif
#ifndef STATIC_MAX_BITSETS
# define STATIC_MAX_BITSETS 8ul
#endif
#ifndef SIMD_IS_MAX
# define SIMD_IS_MAX true
#endif
#ifndef AUT_PREPROCESSOR
# define AUT_PREPROCESSOR aut_preprocessors::surely_losing
#endif
#ifndef BOOLEAN_STATES
# define BOOLEAN_STATES boolean_states::forward_saturation
#endif
#ifndef IOS_PRECOMPUTER
# define IOS_PRECOMPUTER ios_precomputers::standard
#endif
#ifndef ACTIONER
# define ACTIONER actioners::standard
#endif
#ifndef INPUT_PICKER
# define INPUT_PICKER input_pickers::critical_pq
#endif

#ifdef NO_SIMD
# pragma message("Compiling without SIMD")
# ifndef ARRAY_IMPL
#  define ARRAY_IMPL array_backed_sum
# endif
# ifndef VECTOR_IMPL
#  define VECTOR_IMPL vector_backed
# endif
#else
# pragma message("Compiling with SIMD")
# ifndef ARRAY_IMPL
#  define ARRAY_IMPL simd_array_backed_sum
# endif
# ifndef VECTOR_IMPL
#  define VECTOR_IMPL simd_vector_backed
# endif
#endif

#ifndef ARRAY_AND_BITSET_DOWNSET_IMPL
# define ARRAY_AND_BITSET_DOWNSET_IMPL vector_backed
#endif
#ifndef VECTOR_AND_BITSET_DOWNSET_IMPL
# define VECTOR_AND_BITSET_DOWNSET_IMPL vector_backed
#endif
