#pragma once

#include "configuration.hh"

#if ACACIA_COMPILE_BOOLEAN_STATES_FORWARD_SATURATION
#include "boolean_states/forward_saturation.hh"
#endif

#if ACACIA_COMPILE_BOOLEAN_STATES_NO_BOOLEAN_STATES
#include "boolean_states/no_boolean_states.hh"
#endif
