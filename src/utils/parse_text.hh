#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace acacia {
  inline bool text_space (unsigned char c) { return std::isspace (c); }

  inline std::string trim_text (std::string_view value) {
    const auto first = std::find_if_not (value.begin (), value.end (), text_space);
    const auto last = std::find_if_not (value.rbegin (), value.rend (), text_space).base ();
    return first < last ? std::string (first, last) : std::string {};
  }

  // AP lists remove all whitespace, preserve case, and discard empty fields.
  inline void append_ap_list (const std::string& arg, std::vector<std::string>& result) {
    std::istringstream props (arg);
    std::string prop;
    while (std::getline (props, prop, ',')) {
      std::erase_if (prop, text_space);
      if (not prop.empty ())
        result.push_back (prop);
    }
  }
}
