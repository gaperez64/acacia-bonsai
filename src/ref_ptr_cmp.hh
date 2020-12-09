#pragma once

template <typename T>
struct ref_ptr_cmp {
    constexpr bool operator() (const T& lhs,
                               const T& rhs) const {
      return std::addressof (lhs.get ()) < std::addressof (rhs.get ());
    }
};
