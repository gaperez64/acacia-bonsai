#pragma once

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

namespace acacia {
  // Only this process's cgroup; no_limit distinguishes max/absent from unreadable state.
  inline std::optional<uint64_t> scope_memory (const char* file, const char* directory = nullptr,
                                               bool* no_limit = nullptr) noexcept {
    if (no_limit)
      *no_limit = false;
    char path[4608] {};
    if (directory)
      snprintf (path, sizeof path, "%s/%s", directory, file);
    else if (FILE* membership = std::fopen ("/proc/self/cgroup", "r")) {
      char line[4096] {};
      if (std::fgets (line, sizeof line, membership)) {
        if (const char* group = std::strstr (line, "::/"))
          snprintf (path, sizeof path, "/sys/fs/cgroup%.*s/%s",
                    int (std::strcspn (group + 2, "\n")), group + 2, file);
        else if (std::strchr (line, ':') && no_limit)
          *no_limit = true;
      }
      else if (std::feof (membership) && no_limit)
        *no_limit = true;
      std::fclose (membership);
    }
    else if (errno == ENOENT && no_limit)
      *no_limit = true;
    if (FILE* f = std::fopen (path, "r")) {
      char token[64] {}, extra;
      const bool read = std::fscanf (f, "%63s %c", token, &extra) == 1 && !std::ferror (f);
      std::fclose (f);
      if (read) {
        uint64_t value = 0;
        const auto end = token + std::strlen (token);
        const auto parsed = std::from_chars (token, end, value);
        if (parsed.ec == std::errc {} && parsed.ptr == end)
          return value;
        if (no_limit && std::strcmp (file, "memory.max") == 0 && std::strcmp (token, "max") == 0)
          *no_limit = true;
      }
    }
    else if (*path && errno == ENOENT && no_limit)
      *no_limit = true;
    return std::nullopt;
  }
}
