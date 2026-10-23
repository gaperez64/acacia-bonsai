#pragma once

#include "configuration.hh"

#include <bddx.h>

namespace acacia {
  // Call after Spot initializes BuDDy; dictionaries share one process-wide manager.
  inline void configure_bdd () {
#if ACACIA_BDD_MAX_INCREASE > 0
    [[maybe_unused]] static const int previous = bdd_setmaxincrease (ACACIA_BDD_MAX_INCREASE);
#endif
  }
}  // namespace acacia
