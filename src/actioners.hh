#pragma once

#include "actioners/direction.hh"
#include "configuration.hh"

#if ACACIA_COMPILE_ACTIONER_NO_IOS_PRECOMPUTATION
#include "actioners/no_ios_precomputation.hh"
#endif

#if ACACIA_COMPILE_ACTIONER_STANDARD
#include "actioners/standard.hh"
#endif
