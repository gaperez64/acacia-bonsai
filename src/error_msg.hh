#pragma once

#include <cstdlib>
#include <stdarg.h>
#include <stdio.h>

// These are the possible return values for the solver
enum solver_res : int {
  EXIT_CODE_REAL = 0,
  EXIT_CODE_UNKNOWN = 1,
  EXIT_CODE_ERROR = 2,
  EXIT_CODE_UNREAL = 3
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
  fprintf (stderr, "%s:%u: ", filename, linenum);
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fputc ('\n', stderr);
  if (status)
    exit (status);
}
