#pragma once

// BuDDy adds the increment to the old size as signed int. With 16-byte nodes,
// an 8 GiB table plus this increment stays well below INT_MAX.
#if ACACIA_BDD_MAX_INCREASE < 0 || ACACIA_BDD_MAX_INCREASE > 16777216
# error "ACACIA_BDD_MAX_INCREASE must be between 0 and 16777216"
#endif

#if (ACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING + ACACIA_ENABLE_AUT_PREPROCESSOR_STANDARD) != 1
# error "Exactly one aut preprocessor must be selected"
#endif

#if (ACACIA_ENABLE_IOS_PRECOMPUTER_STANDARD + ACACIA_ENABLE_IOS_PRECOMPUTER_MONA + ACACIA_ENABLE_IOS_PRECOMPUTER_SEMANTIC_MONA) != 1
# error "Exactly one ios precomputer must be selected"
#endif

#if (ACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ + ACACIA_ENABLE_INPUT_PICKER_CRITICAL) != 1
# error "Exactly one input picker must be selected"
#endif
