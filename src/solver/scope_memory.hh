#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

namespace acacia {
  // Only this process's cgroup, or the supplied directory; no value if unbounded/unreadable.
  inline std::optional<uint64_t> scope_memory (const char* file,
                                               const char* directory = nullptr) noexcept {
    char path[4608] {};
    if (directory)
      snprintf (path, sizeof path, "%s/%s", directory, file);
    else if (FILE* membership = std::fopen ("/proc/self/cgroup", "r")) {
      char line[4096] {};
      if (std::fgets (line, sizeof line, membership))
        if (const char* group = std::strstr (line, "::/"))
          snprintf (path, sizeof path, "/sys/fs/cgroup%.*s/%s",
                    int (std::strcspn (group + 2, "\n")), group + 2, file);
      std::fclose (membership);
    }
    if (FILE* f = std::fopen (path, "r")) {
      unsigned long long value = 0;
      const bool read = std::fscanf (f, "%llu", &value) == 1;
      std::fclose (f);
      if (read)
        return value;
    }
    return std::nullopt;
  }
}
