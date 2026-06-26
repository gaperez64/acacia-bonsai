#pragma once

#include "configuration.hh"

#if !defined(NDEBUG) || ACACIA_ENABLE_INPUT_PICKER_CRITICAL
#include "input_pickers/critical.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_INPUT_PICKER_CRITICAL_FULLRND
#include "input_pickers/critical_fullrnd.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_INPUT_PICKER_CRITICAL_PQ
#include "input_pickers/critical_pq.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_INPUT_PICKER_CRITICAL_RND
#include "input_pickers/critical_rnd.hh"
#endif
