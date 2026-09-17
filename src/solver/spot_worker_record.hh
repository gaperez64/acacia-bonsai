#pragma once

// Opt-in research capture at the actual worker boundary. No graph traversal,
// formula transformation, or telemetry I/O occurs when the directory is unset.
#include <cstdlib>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace acacia::spot_records {
#ifdef ACACIA_RECORD_COST_TEST
  // Only the cost-regression test binary observes these counters. Count entry,
  // even without an active sink, so moving a guard into a callee fails the test.
  inline thread_local size_t record_calls = 0, timer_reads = 0, report_values = 0;
#endif
  inline void observe_record_call () {
#ifdef ACACIA_RECORD_COST_TEST
    ++record_calls;
#endif
  }
  inline auto now () {
#ifdef ACACIA_RECORD_COST_TEST
    ++timer_reads;
#endif
    return std::chrono::steady_clock::now ();
  }
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
      using Clock = std::chrono::steady_clock;
      struct Lifecycle {
        std::set<std::string> attempt_keys_;
        std::map<std::string, double> totals_;
        unsigned long attempt_id_ = 0, segment_id_ = 0;
        bool attempt_open_ = false, tracking_attempt_ = false;
        int exceptions_ = 0;
        Clock::time_point attempt_started_ {};
      };
      // No P0 container construction/destruction or exception query without a sink.
      std::optional<Lifecycle> lifecycle_;
      void clear_attempt () {
        lifecycle_->tracking_attempt_ = false;
        for (const auto& key : lifecycle_->attempt_keys_) values_.erase (key);
        lifecycle_->attempt_keys_.clear ();
      }
    public:
      Record () {
        if (const char* directory = std::getenv ("ACACIA_SPOT_CAPTURE_DIR"); directory && *directory) {
          lifecycle_.emplace ();
          lifecycle_->exceptions_ = std::uncaught_exceptions ();
          static unsigned long sequence = 0;
          path_ = std::filesystem::path (directory) /
                  (std::to_string (getpid ()) + "-" + std::to_string (++sequence) + ".json");
          const char* history = std::getenv ("ACACIA_SPOT_CAPTURE_HISTORY");
          history_ = history && std::string (history) == "1";
          put ("worker_pid", std::to_string (getpid ()));
          put ("record_version", "2");
          put ("worker_id", path_.stem ().string ());
          // The harness owns the invocation directory (label/cap/instance).
          put ("invocation_id", directory);
          put ("worker_end", "unobserved");
          put ("search_started", "false");
          if (const char* instance = std::getenv ("ACACIA_DIAG_INSTANCE")) put ("instance", instance);
          active = this;
        }
      }
      explicit operator bool () const { return !path_.empty (); }
      void put (const std::string& key, const std::string& value) {
        observe_record_call ();
        if (*this) {
          values_[key] = quote (value);
          if (lifecycle_->tracking_attempt_ && !key.starts_with ("cumulative_") &&
              key != "provider" && key != "backend") lifecycle_->attempt_keys_.insert (key);
        }
      }
      // Keep serialization and its unwind paths out of inlined solver loops.
      [[gnu::noinline]] void phase (std::string_view value) {
        observe_record_call ();
        if (!*this) return;
        put ("stage", std::string (value));
        put ("stage_started_clock_ms", std::to_string (
            std::chrono::duration<double, std::milli> (now ().time_since_epoch ()).count ()));
        if (value == "search") put ("search_started", "true");
        flush ();
      }
      [[gnu::noinline]] void begin_attempt (std::optional<long long> k = std::nullopt) {
        observe_record_call ();
        if (!*this) return;
        if (lifecycle_->attempt_open_) end_attempt ("UNKNOWN", "interrupted");
        clear_attempt ();
        lifecycle_->tracking_attempt_ = lifecycle_->attempt_open_ = true;
        lifecycle_->attempt_started_ = now ();
        put ("attempt_id", std::to_string (++lifecycle_->attempt_id_));
        put ("cumulative_attempts_started", std::to_string (lifecycle_->attempt_id_));
        // Direct Spot/language decisions have no K bound.
        if (k) put ("k", std::to_string (*k));
        put ("search_started", "false");
        put ("attempt_end", "false");
        put ("status", "UNKNOWN");
        put ("evidence", "none");
        phase ("attempt-start");
      }
      [[gnu::noinline]] void end_attempt (std::string_view status, std::string_view evidence) {
        observe_record_call ();
        if (!*this || !lifecycle_->attempt_open_) return;
        put ("status", std::string (status));
        put ("evidence", std::string (evidence));
        put ("attempt_end", "true");
        put ("attempt_elapsed_ms", std::to_string (
            std::chrono::duration<double, std::milli> (now () - lifecycle_->attempt_started_).count ()));
        // Only observed additive measurements of ended attempts are accumulated. Peaks
        // and provider-lifetime counters must never be summed as work counters.
        for (const auto* key : {"search_ms", "verification_ms", "loss_verification_ms",
                               "loss_verification_calls", "win_verification_calls",
                               "attempt_row_generation_ms", "attempt_rows_generated"}) {
          const auto found = values_.find (key);
          if (found != values_.end ()) {
            lifecycle_->totals_[key] += std::stod (found->second.substr (1));
            put (std::string ("cumulative_") + key, std::to_string (lifecycle_->totals_[key]));
          }
        }
        put ("cumulative_attempts_ended", std::to_string (++lifecycle_->totals_["attempts_ended"]));
        if (evidence != "exception" && evidence != "incomplete" && evidence != "interrupted")
          put ("cumulative_attempts_completed", std::to_string (++lifecycle_->totals_["attempts_completed"]));
        lifecycle_->attempt_open_ = false;
        phase ("attempt-end");
      }
      [[gnu::noinline]] void segment (std::string_view provider, std::string_view backend,
                                     bool fallback = false) {
        observe_record_call ();
        if (!*this) return;
        if (lifecycle_->attempt_open_) end_attempt ("UNKNOWN", "interrupted");
        // Preserve a failed factory's final status in optional history even
        // when it never reached a K attempt.
        if (lifecycle_->segment_id_ != 0) flush ();
        clear_attempt ();
        // Keep the transformed-job identity; factory/row metrics from a declined
        // provider must not describe the replacement even if no K was started.
        std::erase_if (values_, [] (const auto& entry) {
          const auto& key = entry.first;
          if (key.starts_with ("cumulative_") || key.starts_with ("worker_") ||
              key.starts_with ("requested_")) return false;
          for (const auto* keep : {"record_version", "invocation_id", "instance", "inputs",
                                  "outputs", "polarity", "transform", "translation_pref",
                                  "candidate_mode", "kmin", "kmax", "kinc"})
            if (key == keep) return false;
          return true;
        });
        put ("segment_id", std::to_string (++lifecycle_->segment_id_));
        put ("provider", std::string (provider));
        put ("backend", std::string (backend));
        put ("fallback", fallback ? "true" : "false");
        put ("search_started", "false");
        phase ("segment-start");
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
          // prefix and deduplicate attempt-end records by worker/attempt ID.
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
      ~Record () {
        try { if (*this) {
          const bool exception = std::uncaught_exceptions () > lifecycle_->exceptions_;
          if (lifecycle_->attempt_open_) end_attempt ("UNKNOWN", exception ? "exception" : "incomplete");
          put ("worker_end", exception ? "exception" : "returned");
          flush ();
        } } catch (...) { /* A failed capture must not replace the worker's exception. */ }
        active = previous_;
      }
  };
  inline void put (const std::string& key, const std::string& value) {
    observe_record_call ();
    if (active) active->put (key, value);
  }
  inline void phase (std::string_view value) {
    observe_record_call ();
    if (active) active->phase (value);
  }
  inline void begin_attempt (std::optional<long long> k = std::nullopt) {
    observe_record_call ();
    if (active) active->begin_attempt (k);
  }
  inline void end_attempt (std::string_view status, std::string_view evidence) {
    observe_record_call ();
    if (active) active->end_attempt (status, evidence);
  }
  inline void segment (std::string_view provider, std::string_view backend, bool fallback = false) {
    observe_record_call ();
    if (active) active->segment (provider, backend, fallback);
  }
  inline void worker_result (bool solved, std::string_view reason) {
    observe_record_call ();
    if (active) {
      active->put ("worker_result", solved ? "solved" : "unknown");
      active->put ("worker_reason", std::string (reason));
    }
  }
} // namespace acacia::spot_records
