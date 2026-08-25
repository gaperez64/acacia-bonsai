#include "config/component_checks.hh"
#include "configuration.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>
#include <posets/vectors/traits.hh>

int main () {
  using Vector = posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>;
  using Downset =
      posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<Vector, 0>>;
  acacia::config::checks::check_solver_components<Downset> ();
  return 0;
}
