#pragma once

#include "actioners/direction.hh"
#include "configuration.hh"

#if !defined(NDEBUG) || ACACIA_ENABLE_ACTIONER_NO_IOS_PRECOMPUTATION
#include "actioners/no_ios_precomputation.hh"
#endif

#if !defined(NDEBUG) || ACACIA_ENABLE_ACTIONER_STANDARD
#include "actioners/standard.hh"
#endif
