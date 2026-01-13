#pragma once

#include <cstdlib>
#include <print>
#include <stdarg.h>
#include <stdio.h>

// These are the possible return values for the solver
enum SOLVER_RES : std::uint8_t {
  EXIT_CODE_REAL = 0,
  EXIT_CODE_UNKNOWN = 2,
  EXIT_CODE_ERROR = 3,
  EXIT_CODE_UNREAL = 1
};

// macOS does not have the "error.h" header, so we use
// the following functions
inline void error (int status, const char* format, ...) {
  va_list args;
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  if (status != 0)
    exit (status);
}

inline void error_at_line (int status, const char* filename, unsigned int linenum,
                           const char* format, ...) {
  va_list args;
  std::print (stderr, "{}:{}: ", filename, linenum);
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fputc ('\n', stderr);
  if (status)
    exit (status);
}
