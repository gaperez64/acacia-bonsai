#pragma once

#include "configuration.hh"

#if !defined(NDEBUG) || ACACIA_ENABLE_AUT_PREPROCESSOR_NO_PREPROCESSING
#include "aut_preprocessors/no_preprocessing.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_AUT_PREPROCESSOR_STANDARD
#include "aut_preprocessors/standard.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_AUT_PREPROCESSOR_SURELY_LOSING
#include "aut_preprocessors/surely_losing.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_AUT_PREPROCESSOR_ELEVATOR
#include "aut_preprocessors/elevator.hh"
#endif
