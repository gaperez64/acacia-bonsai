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

#ifndef VECTOR_ELT_T
# define VECTOR_ELT_T char
#endif

#ifndef K_BOUNDED_SAFETY_AUT_IMPL
# define K_BOUNDED_SAFETY_AUT_IMPL k_bounded_safety_aut
#endif
#ifdef NDEBUG
# pragma message ("Compiling with NDEBUG")
# ifndef STATIC_ARRAY_MAX
#  define STATIC_ARRAY_MAX 300
# endif

# ifndef STATIC_MAX_BITSETS
#  define STATIC_MAX_BITSETS 3ul
# endif

#else
# pragma message ("Compiling without NDEBUG")
# ifndef STATIC_ARRAY_MAX
#  define STATIC_ARRAY_MAX 30
# endif
# ifndef STATIC_MAX_BITSETS
#  define STATIC_MAX_BITSETS 1ul
# endif
#endif

#ifdef NO_SIMD
# pragma message ("Compiling without SIMD")
# ifndef ARRAY_IMPL
#  define ARRAY_IMPL array_backed_sum
# endif
# ifndef VECTOR_IMPL
#  define VECTOR_IMPL vector_backed
# endif
#else
# pragma message ("Compiling with SIMD")
# ifndef ARRAY_IMPL
#  define ARRAY_IMPL simd_array_backed_sum
# endif
# ifndef VECTOR_IMPL
#  define VECTOR_IMPL simd_vector_backed
# endif
#endif

#ifndef ARRAY_AND_BITSET_DOWNSET_IMPL
# define ARRAY_AND_BITSET_DOWNSET_IMPL vector_backed_bin
#endif

#ifndef VECTOR_AND_BITSET_DOWNSET_IMPL
# define VECTOR_AND_BITSET_DOWNSET_IMPL vector_backed_bin
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
# define ACTIONER actioners::standard<typename SetOfStates::value_type>
#endif

#ifndef INPUT_PICKER
# define INPUT_PICKER input_pickers::critical_pq
#endif
