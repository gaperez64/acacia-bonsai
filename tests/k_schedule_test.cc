#include "configuration.hh"
#include "solver/k_schedule.hh"

#include <cstddef>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
  using acacia::k_schedule::kind;
  using acacia::k_schedule::loss_evidence;

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

  std::vector<long long> collect (kind schedule, long long kmin, long long kmax,
                                  long long kinc,
                                  const loss_evidence& evidence) {
    std::vector<long long> result;
    long long k = kmin;
    for (std::size_t step = 0; step < 128; ++step) {
      const auto next =
          acacia::k_schedule::next (schedule, k, kmin, kmax, kinc, evidence);
      if (not next)
        return result;
      if (*next <= k || *next > kmax) {
        std::cerr << acacia::k_schedule::name (schedule)
                  << ": invalid next bound " << *next << " after " << k
                  << " with kmax=" << kmax << '\n';
        return {};
      }
      result.push_back (*next);
      k = *next;
    }
    std::cerr << acacia::k_schedule::name (schedule)
              << ": did not terminate within 128 steps\n";
    return {};
  }

  bool reaches_kmax (kind schedule, const loss_evidence& evidence) {
    constexpr long long kmin = 2;
    constexpr long long kmax = 99;
    constexpr long long kinc = 3;
    long long k = kmin;

    for (std::size_t step = 0; step < 128; ++step) {
      const auto next =
          acacia::k_schedule::next (schedule, k, kmin, kmax, kinc, evidence);
      if (not next)
        return k == kmax;
      if (*next <= k || *next > kmax)
        return false;
      k = *next;
    }
    return false;
  }

  bool check_schedules () {
    const loss_evidence cheap {50, 1'000, 1'000, false};
    const loss_evidence cheap_by_shape {51, 64, 8, true};
    const loss_evidence expensive {51, 65, 9, true};

    const std::vector<long long> linear_expected {
        5,  8,  11, 14, 17, 20, 23, 26, 29, 32, 35,
        38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68,
        71, 74, 77, 80, 83, 86, 89, 92, 95, 98, 99};
    const std::vector<long long> geometric_expected {5, 11, 23, 47, 95, 99};

    bool ok = true;
    const auto linear = collect (kind::linear, 2, 99, 3, expensive);
    ok &= expect ("linear exact sequence", linear == linear_expected);

    auto geometric = collect (kind::geometric, 2, 99, 3, expensive);
    ok &= expect ("geometric exact sequence", geometric == geometric_expected);
    geometric.insert (geometric.begin (), 2);
    ok &= expect ("geometric sequence includes 2,5,11,23,47,95,99",
                  geometric
                      == std::vector<long long> {2, 5, 11, 23, 47, 95, 99});
    ok &= expect ("geometric exhausted at kmax",
                  not acacia::k_schedule::next (kind::geometric, 99, 2, 99, 3,
                                                expensive));

    ok &= expect ("direct-max jumps from kmin to kmax",
                  collect (kind::direct_max, 2, 99, 3, expensive)
                      == std::vector<long long> {99});
    ok &= expect ("adaptive cheap loss uses geometric",
                  collect (kind::cheap_loss_adaptive, 2, 99, 3, cheap)
                      == geometric_expected);
    ok &= expect ("adaptive cheap shape uses geometric",
                  collect (kind::cheap_loss_adaptive, 2, 99, 3,
                           cheap_by_shape)
                      == geometric_expected);
    ok &= expect ("adaptive expensive loss uses linear",
                  collect (kind::cheap_loss_adaptive, 2, 99, 3, expensive)
                      == linear_expected);

    ok &= expect ("linear terminates", reaches_kmax (kind::linear, expensive));
    ok &= expect ("geometric terminates",
                  reaches_kmax (kind::geometric, expensive));
    ok &= expect ("adaptive-cheap terminates",
                  reaches_kmax (kind::cheap_loss_adaptive, cheap));
    ok &= expect ("adaptive-expensive terminates",
                  reaches_kmax (kind::cheap_loss_adaptive, expensive));
    ok &= expect ("direct-max terminates",
                  reaches_kmax (kind::direct_max, expensive));

    ok &= expect ("linear diagnostic name",
                  std::string_view (acacia::k_schedule::name (kind::linear))
                      == "linear");
    ok &= expect ("geometric diagnostic name",
                  std::string_view (acacia::k_schedule::name (kind::geometric))
                      == "geometric");
    ok &= expect (
        "adaptive diagnostic name",
        std::string_view (
            acacia::k_schedule::name (kind::cheap_loss_adaptive))
            == "cheap_loss_adaptive");
    ok &= expect ("direct-max diagnostic name",
                  std::string_view (acacia::k_schedule::name (kind::direct_max))
                      == "direct_max");

    if (ok)
      std::cout << "K-schedule sequence and termination checks passed\n";
    return ok;
  }
}  // namespace

int main () {
  // The algebra check must run before schedule tests: it decides whether a
  // direct bound jump is semantically legal.
  if (not check_lift_algebra ())
    return 1;
  return check_schedules () ? 0 : 1;
}
