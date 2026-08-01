#include "version.hh"

#include "acacia_version.hh"

#include <ostream>

const char* acacia_version () {
  return ACACIA_VERSION;
}

void print_version (std::ostream& output) {
  output << "Version: " << acacia_version () << '\n';
}
