#pragma once

#include "configuration.hh"

namespace actioners {
    enum class direction {
      forward,
      backward
    };
}

#include "actioners/standard.hh"
#include "actioners/no_ios_precomputation.hh"
