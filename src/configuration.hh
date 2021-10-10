#pragma once

#include "utils/todo.hh"

#ifndef DEFAULT_K
# define DEFAULT_K 11
#endif
#ifndef DEFAULT_KMIN
# define DEFAULT_KMIN -1u
#endif
#ifndef DEFAULT_KINC
# define DEFAULT_KINC 0
#endif

#define DEFAULT_K 12
#define DEFAULT_KMIN 4
#define DEFAULT_KINC 2

#define VECTOR_ELT_T char
#define K_BOUNDED_SAFETY_AUT_IMPL k_bounded_safety_aut
#ifdef NDEBUG
# pragma message ("Compiling with NDEBUG")
# define STATIC_ARRAY_MAX 300
# define STATIC_MAX_BITSETS 3ul
#else
# pragma message ("Compiling without NDEBUG")
# define STATIC_ARRAY_MAX 30
# define STATIC_MAX_BITSETS 1ul
#endif
#ifdef NO_SIMD
# pragma message ("Compiling without SIMD")
# define ARRAY_IMPL array_backed_sum
# define VECTOR_IMPL vector_backed
#else
# pragma message ("Compiling with SIMD")
# define ARRAY_IMPL simd_array_backed_sum
# define VECTOR_IMPL simd_vector_backed
#endif

#define ARRAY_AND_BITSET_DOWNSET_IMPL vector_backed_bin
#define VECTOR_AND_BITSET_DOWNSET_IMPL vector_backed_bin

#define SIMD_IS_MAX true
