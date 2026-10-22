#include "solver/bdd_config.hh"

#include <iostream>
#include <spot/twa/bdddict.hh>

int main () {
  auto dict = spot::make_bdd_dict ();
  const int original = bdd_setmaxincrease (12345);
  bdd_setmaxincrease (original);
  if constexpr (ACACIA_BDD_MAX_INCREASE > 0)
    acacia::configure_bdd ();
  const int expected = ACACIA_BDD_MAX_INCREASE ? ACACIA_BDD_MAX_INCREASE : original;
  // BuDDy has no getter; its setter returns the previous threshold.
  if (bdd_setmaxincrease (12345) != expected) {
    std::cerr << "BDD growth setting was not applied after Spot initialization\n";
    return 1;
  }
  if constexpr (ACACIA_BDD_MAX_INCREASE > 0)
    acacia::configure_bdd ();
  if (bdd_setmaxincrease (expected) != 12345) {
    std::cerr << "BDD growth setting was applied more than once\n";
    return 1;
  }
  const int old_size = bdd_getallocnum ();
  const int variables = old_size / 2 + 1;
  if (bdd_setvarnum (variables) < 0 or bdd_getallocnum () <= old_size) {
    std::cerr << "BDD table did not grow\n";
    return 1;
  }
  if ((bdd_ithvar (variables - 1) & bdd_nithvar (variables - 1)) != bddfalse) {
    std::cerr << "BDD operations failed after table growth\n";
    return 1;
  }
}
