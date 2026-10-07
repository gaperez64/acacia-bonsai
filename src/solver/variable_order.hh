#pragma once

namespace acacia {
  enum class variable_order { incumbent, typed_interleaved, role_grouped };

  inline const char* variable_order_name (variable_order order) {
    switch (order) {
      case variable_order::incumbent: return "incumbent";
      case variable_order::typed_interleaved: return "typed-interleaved";
      case variable_order::role_grouped: return "role-grouped";
    }
    return "invalid";
  }
}
