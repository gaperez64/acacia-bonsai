#pragma once

#include "configuration.hh"

#if ACACIA_COMPILE_AUT_PREPROCESSOR_STANDARD
#include "aut_preprocessors/standard.hh"
#endif

#if ACACIA_COMPILE_AUT_PREPROCESSOR_SURELY_LOSING
#include "aut_preprocessors/surely_losing.hh"
#endif

#include "boolean_states/forward_saturation.hh"

#include "actioners/direction.hh"

#include "actioners/standard.hh"

#if ACACIA_COMPILE_IOS_PRECOMPUTER_STANDARD
#include "ios_precomputers/standard.hh"
#endif

#if ACACIA_COMPILE_IOS_PRECOMPUTER_MONA
#include "ios_precomputers/mona.hh"
#endif
#if ACACIA_COMPILE_IOS_PRECOMPUTER_SEMANTIC_MONA
#include "ios_precomputers/semantic_mona.hh"
#endif

#if ACACIA_COMPILE_INPUT_PICKER_CRITICAL
#include "input_pickers/critical.hh"
#endif

#if ACACIA_COMPILE_INPUT_PICKER_CRITICAL_PQ
#include "input_pickers/critical_pq.hh"
#endif
