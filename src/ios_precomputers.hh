#pragma once

#include "configuration.hh"

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
#if ACACIA_COMPILE_IOS_PRECOMPUTER_SEMANTIC_MONA
#include "ios_precomputers/semantic_mona.hh"
#endif
