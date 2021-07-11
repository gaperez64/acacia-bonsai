#pragma once

#define DEFAULT_K 11

#define VECTOR_ELT_T char
#define K_BOUNDED_SAFETY_AUT_IMPL k_bounded_safety_aut
#ifdef NDEBUG
# define STATIC_ARRAY_MAX 300
# define STATIC_MAX_BITSETS 3ul
#else
# define STATIC_ARRAY_MAX 30
# define STATIC_MAX_BITSETS 1ul
#endif
#ifdef NO_SIMD
# define ARRAY_IMPL array_backed_sum
# define VECTOR_IMPL vector_backed
#else
# define ARRAY_IMPL simd_array_backed_sum
# define VECTOR_IMPL simd_vector_backed
#endif

constexpr auto STATIC_ARRAY_CAP_MAX = vectors::traits<vectors::ARRAY_IMPL, VECTOR_ELT_T>::capacity_for (STATIC_ARRAY_MAX);

#define ARRAY_AND_BITSET_DOWNSET_IMPL vector_backed_bin
#define VECTOR_AND_BITSET_DOWNSET_IMPL vector_backed_bin
