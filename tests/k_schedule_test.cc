#include "configuration.hh"
#include "solver/k_schedule.hh"

#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

  std::ostream& print_vector (std::ostream& out, const std::vector<int>& value) {
    out << '[';
    for (std::size_t i = 0; i < value.size (); ++i) {
      if (i != 0)
        out << ',';
      out << value[i];
    }
    return out << ']';
  }

  std::vector<int> lift (const std::vector<int>& value,
                         std::size_t bool_threshold, int delta) {
    std::vector<int> out (value.size (), 0);
    for (std::size_t i = 0; i < bool_threshold; ++i)
      out[i] = value[i] + delta;
    return out;
  }

  bool check_overflow_guard (const std::vector<int>& value,
                             std::size_t bool_threshold) {
    // A jump with kmax <= 99 has delta <= 99.  Use that conservative maximum,
    // even though the configured kmin makes the actual largest jump smaller.
    constexpr long long largest_schedule_delta = 99;
    constexpr long long value_min =
        static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::lowest ());
    constexpr long long value_max =
        static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::max ());

    for (std::size_t i = 0; i < bool_threshold; ++i) {
      const long long widened = static_cast<long long> (value[i])
                                + largest_schedule_delta;
      if (widened < value_min || widened > value_max) {
        std::cerr << "OVERFLOW GUARD FAILED: state=";
        print_vector (std::cerr, value)
            << ", bool_threshold=" << bool_threshold << ", coordinate=" << i
            << ", delta=" << largest_schedule_delta << ", result=" << widened
            << ", VECTOR_ELT_T range=[" << value_min << ',' << value_max << "]\n";
        return false;
      }
    }
    return true;
  }

  bool check_lift_algebra () {
    std::size_t checked = 0;

    for (const int K : {2, 3}) {
      const std::size_t levels = static_cast<std::size_t> (K + 2);
      for (std::size_t size = 1; size <= 4; ++size) {
        std::size_t vector_count = 1;
        for (std::size_t i = 0; i < size; ++i)
          vector_count *= levels;

        for (std::size_t encoded = 0; encoded < vector_count; ++encoded) {
          std::vector<int> state (size);
          std::size_t rest = encoded;
          for (int& coordinate : state) {
            coordinate = static_cast<int> (rest % levels) - 1;
            rest /= levels;
          }

          for (std::size_t bool_threshold = 0; bool_threshold <= size;
               ++bool_threshold) {
            if (not check_overflow_guard (state, bool_threshold))
              return false;

            // Enumerating every value in -1..K at every threshold includes
            // numeric -1, active numeric values, boolean -1, and boolean 0.
            for (int a = 1; a <= 4; ++a) {
              for (int b = 1; b <= 4; ++b) {
                const auto repeated = lift (lift (state, bool_threshold, a),
                                            bool_threshold, b);
                const auto direct = lift (state, bool_threshold, a + b);
                ++checked;
                if (repeated == direct)
                  continue;

                std::cerr << "ALGEBRA EQUALITY FAILS: K=" << K
                          << ", size=" << size
                          << ", bool_threshold=" << bool_threshold << ", state=";
                print_vector (std::cerr, state) << ", a=" << a << ", b=" << b
                                                << ", repeated=";
                print_vector (std::cerr, repeated) << ", direct=";
                print_vector (std::cerr, direct) << '\n';
                return false;
              }
            }
          }
        }
      }
    }

    std::cout << "Algebra equality HOLDS: " << checked
              << " exhaustive cases checked\n";
    std::cout << "Overflow guard HOLDS for schedule deltas through 99; "
                 "VECTOR_ELT_T range is ["
              << static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::lowest ())
              << ','
              << static_cast<long long> (std::numeric_limits<VECTOR_ELT_T>::max ())
              << "]\n";
    return true;
  }

  bool expect (std::string_view label, bool condition) {
    if (condition)
      return true;
    std::cerr << label << ": FAILED\n";
    return false;
  }

  bool check_schedule () {
    using acacia::k_schedule::next;
    bool ok = true;
    long long k = 2;
    for (int i = 0; i < 33; ++i) {
      const auto n = next (k, 2, 99, 3);
      ok &= expect ("linear sequence", n == std::min (99LL, k + 3));
      if (not n)
        return false;
      k = *n;
    }
    ok &= expect ("reaches maximum", k == 99 && not next (k, 2, 99, 3));
    ok &= expect ("invalid increment", not next (2, 2, 99, 0) && not next (2, 2, 99, -1));
    ok &= expect ("below minimum", not next (1, 2, 99, 3));
    ok &= expect ("single bound", not next (2, 2, 2, 3));
    constexpr auto max = std::numeric_limits<long long>::max ();
    ok &= expect ("overflow clamps", next (max - 1, 2, max, 3) == max);
    return ok;
  }
}  // namespace

int main () {
  return check_lift_algebra () && check_schedule () ? 0 : 1;
}
