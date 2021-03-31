#pragma once

namespace aut_preprocessors {
    struct no_preprocessing {
        static auto make (...) {
          return [] () {};
        }
    };
}
