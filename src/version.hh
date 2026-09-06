#pragma once

#include "configuration.hh"

#include <iosfwd>

const char* acacia_version ();
void print_version (std::ostream& output);

// An absent or empty label denotes a hand-assembled configuration.
inline constexpr const char* acacia_preset () {
#ifdef ACACIA_PRESET
  return ACACIA_PRESET;
#else
  return "";
#endif
}
