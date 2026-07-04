// Unit tests for the output-side orbit counting used by the union_o spike.
// These are pure combinatorics: input type-counts define stabilizer buckets,
// and output representatives are distributions of output valuation types
// inside each bucket.

#include "solver/symmetric_k_bounded_safety_aut.hh"

#include <iostream>
#include <set>
#include <vector>

using acacia::solver_detail::symmetric::output_representative_count;
using acacia::solver_detail::symmetric::output_type_assignments_for_input_counts;

namespace {

  int failures = 0;

  void check_count (const std::vector<unsigned>& input_counts, unsigned output_types,
                    unsigned shared_outputs, size_t cap, size_t want) {
    const size_t got =
        output_representative_count (input_counts, output_types, shared_outputs, cap);
    if (got != want) {
      std::cerr << "count mismatch: got " << got << ", want " << want << "\n";
      ++failures;
    }
  }

}  // namespace

int main () {
  check_count ({5, 0}, 2, 0, 100, 6);       // all clients in one input bucket
  check_count ({3, 2}, 2, 0, 100, 12);      // (3+1) * (2+1)
  check_count ({3, 2}, 2, 1, 100, 24);      // one shared output bit doubles it
  check_count ({1, 2}, 4, 0, 100, 40);      // C(4,3) * C(5,3)
  check_count ({5, 0}, 2, 0, 5, 6);         // cap exceeded is reported as cap+1

  auto assignments = output_type_assignments_for_input_counts ({2, 1}, 2, 20);
  if (not assignments.has_value () or assignments->size () != 6) {
    std::cerr << "assignment count mismatch for {2,1} / 2 output types\n";
    ++failures;
  }
  else {
    std::set<std::vector<unsigned>> unique (assignments->begin (), assignments->end ());
    if (unique.size () != assignments->size ()) {
      std::cerr << "assignments are not unique\n";
      ++failures;
    }
    for (const auto& a : *assignments) {
      if (a.size () != 3) {
        std::cerr << "assignment has wrong slot count\n";
        ++failures;
        break;
      }
      for (unsigned t : a) {
        if (t >= 2) {
          std::cerr << "assignment uses an out-of-range output type\n";
          ++failures;
          break;
        }
      }
    }
  }

  auto capped = output_type_assignments_for_input_counts ({5, 0}, 2, 5);
  if (capped.has_value ()) {
    std::cerr << "expected capped assignment generation to return nullopt\n";
    ++failures;
  }

  if (failures == 0) {
    std::cout << "ALL symmetric_output_orbits tests PASSED\n";
    return 0;
  }
  std::cout << failures << " FAILURES\n";
  return 1;
}
