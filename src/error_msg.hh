#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// macOS does not have the "error.h" header, so we use
// the following functions
inline void error(int status, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  if (status != 0)
    exit(status);
}

inline void error_at_line(int status, const char *filename, unsigned int linenum, const char *format, ...) {
  va_list args;
  fprintf(stderr, "%s:%u: ", filename, linenum);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  if (status)
    exit(status);
}


