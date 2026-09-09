#pragma once

// Opt-in research capture at the actual worker boundary. No graph traversal,
// formula transformation, or telemetry I/O occurs when the directory is unset.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace acacia::spot_records {
  inline std::string quote (const std::string& s) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : s) {
      if (c == '"' || c == '\\') { out += '\\'; out += c; }
      else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
      else out += c;
    }
    return out + '"';
  }
  class Record;
  inline thread_local Record* active = nullptr;
  class Record {
      Record* previous_ = active;
      std::filesystem::path path_;
      bool history_ = false;
      std::map<std::string, std::string> values_;
    public:
      Record () {
        if (const char* directory = std::getenv ("ACACIA_SPOT_CAPTURE_DIR"); directory && *directory) {
          static unsigned long sequence = 0;
          path_ = std::filesystem::path (directory) /
                  (std::to_string (getpid ()) + "-" + std::to_string (++sequence) + ".json");
          const char* history = std::getenv ("ACACIA_SPOT_CAPTURE_HISTORY");
          history_ = history && std::string (history) == "1";
          put ("worker_pid", std::to_string (getpid ()));
          if (const char* instance = std::getenv ("ACACIA_DIAG_INSTANCE")) put ("instance", instance);
          active = this;
        }
      }
      explicit operator bool () const { return !path_.empty (); }
      void put (const std::string& key, const std::string& value) {
        if (*this) values_[key] = quote (value);
      }
      void list (const std::string& key, const std::vector<std::string>& values) {
        if (!*this) return;
        std::string encoded = "[";
        for (const auto& value : values) { if (encoded.size () > 1) encoded += ','; encoded += quote (value); }
        values_[key] = encoded + ']';
      }
      void flush () const noexcept {
        if (!*this) return;
        try {
          std::filesystem::create_directories (path_.parent_path ());
          const auto temporary = path_.string () + ".tmp";
          std::ostringstream snapshot;
          snapshot << "{";
          bool first = true;
          for (const auto& [key, value] : values_) {
            if (!first) snapshot << ',';
            first = false;
            snapshot << quote (key) << ':' << value;
          }
          snapshot << "}\n";
          const auto encoded = snapshot.str ();
          std::ofstream out (temporary);
          out << encoded;
          out.close ();
          if (!out) throw std::runtime_error ("cannot write capture");
          std::filesystem::rename (temporary, path_);
          // Optional milestone history retains completed K attempts. A killed
          // writer may leave a partial final line; consumers keep the complete
          // prefix and count only verified-attempt records, deduplicated by K.
          if (history_) {
            auto history_path = path_;
            history_path.replace_extension (".history.jsonl");
            std::ofstream history (history_path, std::ios::app);
            history << encoded;
            history.close ();
            if (!history) throw std::runtime_error ("cannot write capture history");
          }
        } catch (const std::exception& e) {
          std::cerr << "spot capture failed: " << e.what () << '\n';
        }
      }
      ~Record () { flush (); active = previous_; }
  };
  inline void put (const std::string& key, const std::string& value) {
    if (active) active->put (key, value);
  }
  inline void phase (const std::string& value) {
    if (active) { active->put ("stage", value); active->flush (); }
  }
} // namespace acacia::spot_records
