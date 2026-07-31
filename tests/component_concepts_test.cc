#include "config/component_checks.hh"
#include "configuration.hh"

#include <posets/downsets.hh>
#include <posets/vectors.hh>

int main () {
  using Downset = posets::downsets::VECTOR_AND_BITSET_DOWNSET_IMPL<posets::vectors::x_and_bitset<
      posets::vectors::VECTOR_IMPL<VECTOR_ELT_T>, 1>>;
  acacia::config::checks::check_solver_components<Downset> ();
  return 0;
}
