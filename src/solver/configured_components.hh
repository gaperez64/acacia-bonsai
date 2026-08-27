#pragma once

#include "configuration.hh"

#if ACACIA_COMPILE_AUT_PREPROCESSOR_NO_PREPROCESSING
#include "aut_preprocessors/no_preprocessing.hh"
#endif

#if ACACIA_COMPILE_AUT_PREPROCESSOR_STANDARD
#include "aut_preprocessors/standard.hh"
#endif

#if ACACIA_COMPILE_AUT_PREPROCESSOR_SURELY_LOSING
#include "aut_preprocessors/surely_losing.hh"
#endif

#if ACACIA_COMPILE_AUT_PREPROCESSOR_ELEVATOR
#include "aut_preprocessors/elevator.hh"
#endif

#if ACACIA_COMPILE_BOOLEAN_STATES_FORWARD_SATURATION
#include "boolean_states/forward_saturation.hh"
#endif

#if ACACIA_COMPILE_BOOLEAN_STATES_NO_BOOLEAN_STATES
#include "boolean_states/no_boolean_states.hh"
#endif

#if ACACIA_COMPILE_BOOLEAN_STATES_TRANSITION_CORE
#include "boolean_states/transition_core.hh"
#endif

#include "actioners/direction.hh"

#if ACACIA_COMPILE_ACTIONER_NO_IOS_PRECOMPUTATION
#include "actioners/no_ios_precomputation.hh"
#endif

#if ACACIA_COMPILE_ACTIONER_STANDARD
#include "actioners/standard.hh"
#endif

#if ACACIA_COMPILE_IOS_PRECOMPUTER_DELEGATE
#include "ios_precomputers/delegate.hh"
#endif

#if ACACIA_COMPILE_IOS_PRECOMPUTER_FAKE_VARS
#include "ios_precomputers/fake_vars.hh"
#endif

#if ACACIA_COMPILE_IOS_PRECOMPUTER_POWSET
#include "ios_precomputers/powset.hh"
#endif

#if ACACIA_COMPILE_IOS_PRECOMPUTER_STANDARD
#include "ios_precomputers/standard.hh"
#endif

#if ACACIA_COMPILE_IOS_PRECOMPUTER_MONA
#include "ios_precomputers/mona.hh"
#endif

#if ACACIA_COMPILE_INPUT_PICKER_CRITICAL
#include "input_pickers/critical.hh"
#endif

#if ACACIA_COMPILE_INPUT_PICKER_CRITICAL_FULLRND
#include "input_pickers/critical_fullrnd.hh"
#endif

#if ACACIA_COMPILE_INPUT_PICKER_CRITICAL_PQ
#include "input_pickers/critical_pq.hh"
#endif

#if ACACIA_COMPILE_INPUT_PICKER_CRITICAL_RND
#include "input_pickers/critical_rnd.hh"
#endif
