#include "configuration.hh"
#include "native_support.hh"
#include "error_msg.hh"

#if ACACIA_NATIVE_ARMS
#include <cerrno>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

namespace acacia {
  void native_arm_diagnostic (std::string_view arm, std::string_view stage, int status,
                                     std::string_view message) {
    const auto quote = [] (std::string_view value) {
      std::string out = "\"";
      for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\')
          out += '\\';
        if (ch < 0x20) {
          constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out += hex[ch >> 4];
          out += hex[ch & 15];
        }
        else
          out += char (ch);
      }
      return out + '"';
    };
    const std::string line =
        std::string ("{\"arm\":") + quote (arm) + ",\"stage\":" + quote (stage) +
        ",\"status\":" + std::to_string (status) + ",\"message\":" + quote (message) + "}\n";
    // One write keeps concurrent children's JSON records intact on stderr.
    (void) ::write (STDERR_FILENO, line.data (), line.size ());
  }

  void native_diagnostic (bool unreal, std::string_view stage, int status,
                                 std::string_view message) {
    native_arm_diagnostic (unreal ? "unreal:gr1:oxidd" : "real:gr1:oxidd", stage, status, message);
  }

  bool native_limit_address_space (std::string_view arm) {
    rlimit address {};
    if (getrlimit (RLIMIT_AS, &address) != 0) {
      native_arm_diagnostic (arm, "memory", errno, "could not read address-space cap");
      return false;
    }
    constexpr rlim_t cap = rlim_t {8} * 1024 * 1024 * 1024;
    if (address.rlim_cur == RLIM_INFINITY || address.rlim_cur > cap) {
      address.rlim_cur = cap;
      if (setrlimit (RLIMIT_AS, &address) != 0) {
        native_arm_diagnostic (arm, "memory", errno, "could not set address-space cap");
        return false;
      }
    }
    return true;
  }
}
#endif
