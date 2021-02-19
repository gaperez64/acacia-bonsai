#pragma once

namespace aut_preprocessing {
    struct no_preprocessing {
        static auto make (...) {
          return [] () {};
        }
    };
}
