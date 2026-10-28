#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

static int failure (const char* name) {
  const char* mode = getenv ("ACACIA_TEST_RECORD_SETUP_FAILURE");
  return mode && strcmp (mode, name) == 0;
}

int prctl (int option, ...) {
  if (option == PR_SET_CHILD_SUBREAPER && failure ("subreaper")) {
    errno = EPERM;
    return -1;
  }
  va_list args;
  va_start (args, option);
  unsigned long a = va_arg (args, unsigned long), b = va_arg (args, unsigned long),
                c = va_arg (args, unsigned long), d = va_arg (args, unsigned long);
  va_end (args);
  int (*original) (int, ...) = dlsym (RTLD_NEXT, "prctl");
  return original (option, a, b, c, d);
}

void* mmap (void* address, size_t length, int prot, int flags, int fd, off_t offset) {
  if ((flags & MAP_SHARED) && failure ("mmap")) {
    errno = ENOMEM;
    return MAP_FAILED;
  }
  void* (*original) (void*, size_t, int, int, int, off_t) = dlsym (RTLD_NEXT, "mmap");
  return original (address, length, prot, flags, fd, offset);
}

int pipe2 (int fds[2], int flags) {
  if (failure ("pipe")) {
    errno = EMFILE;
    return -1;
  }
  int (*original) (int*, int) = dlsym (RTLD_NEXT, "pipe2");
  return original (fds, flags);
}

pid_t fork (void) {
  static int injected = 0;
  if (!injected && failure ("fork")) {
    injected = 1;
    errno = EAGAIN;
    return -1;
  }
  pid_t (*original) (void) = dlsym (RTLD_NEXT, "fork");
  return original ();
}
