#pragma once

#include "../src/phase_records.hh"
#include <dlfcn.h>
#include <poll.h>

// Record delivery is best effort in the solver. Tests that assert complete rows
// acknowledge each packet's append, so writer scheduling cannot drop their oracle.
extern "C" ssize_t write (int fd, const void* data, size_t size) {
  using function = ssize_t (*) (int, const void*, size_t);
  static const auto original = reinterpret_cast<function> (dlsym (RTLD_NEXT, "write"));
  const char* directory = std::getenv ("ACACIA_PHASE_RECORDS");
  constexpr size_t header = offsetof (acacia::phase_packet, line);
  pid_t pid = -1;
  uint16_t length = 0;
  if (size > header && size <= sizeof (acacia::phase_packet)) {
    std::memcpy (&pid, data, sizeof pid);
    std::memcpy (&length,
                 static_cast<const char*> (data) + offsetof (acacia::phase_packet, length),
                 sizeof length);
  }
  if (!directory || (!std::getenv ("ACACIA_TEST_RECORD_ACK") &&
      !std::getenv ("ACACIA_TEST_OBLIGATION_RECORD_ACK")) || pid != getpid () ||
      size != header + length || !length || static_cast<const char*> (data)[size - 1] != '\n')
    return original (fd, data, size);
  char path[4096];
  const int used = snprintf (path, sizeof path, "%s/%ld.jsonl", directory, long (pid));
  if (used <= 0 || size_t (used) >= sizeof path)
    return original (fd, data, size);
  struct stat before {};
  const off_t end = (stat (path, &before) == 0 ? before.st_size : 0) + length;
  ssize_t written;
  while ((written = original (fd, data, size)) < 0 && (errno == EAGAIN || errno == EINTR)) {
    pollfd writable {fd, POLLOUT, 0};
    poll (&writable, 1, -1);
  }
  if (written == ssize_t (size)) {
    const int saved_errno = errno;
    struct stat after {};
    while (stat (path, &after) != 0 || after.st_size < end)
      poll (nullptr, 0, 1);
    errno = saved_errno;
  }
  return written;
}

