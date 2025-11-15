#pragma once


#if defined(__APPLE__)

// macOS does not have the "error.h" header, so we use
// the following function
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

inline void error(int status, int errnum, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  if (status != 0)
    exit(status);
}

inline void error_at_line(int status, int errnum,
                                   const char *filename, unsigned int linenum,
                                   const char *format, ...) {
  va_list args;
  fprintf(stderr, "%s:%u: ", filename, linenum);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  if (errnum)
    fprintf(stderr, ": %s", strerror(errnum));
  fputc('\n', stderr);
  if (status)
    exit(status);
}


#elif defined(__linux__)
  #include <error.h>
#else
  #error "Unsupported platform"
#endif


