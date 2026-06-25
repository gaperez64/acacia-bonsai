#pragma once

#include "aut_preprocessors/no_preprocessing.hh"
#include "aut_preprocessors/standard.hh"
#include "aut_preprocessors/surely_losing.hh"

#ifdef ENABLE_ELEVATOR_PREPROCESSOR
# include "aut_preprocessors/elevator.hh"
#endif
#include "configuration.hh"
