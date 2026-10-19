#pragma once

#include "phase_records.hh"
#include <string_view>

#include <spot/tl/print.hh>
#include <stdexcept>
#include <string>
#include <tlsf/pipeline.h>
#include <vector>

namespace acacia {
  // Length prefixes preserve alphabet order and distinguish empty/missing APs.
  inline void accounting_identity (spot::formula formula, const std::vector<std::string>& inputs,
                                   const std::vector<std::string>& outputs, const char* provider,
                                   std::string_view translation,
                                   std::string_view preprocessing) noexcept {
    if (!phase_records_enabled ())
      return;
    try {
      const auto hash = [] (std::string_view text, char (&out)[65]) {
        if (!tlsf_pipeline_source_sha256 (text.data (), text.size (), out))
          throw std::runtime_error ("identity hash failed");
      };
      std::string alphabet;
      const auto append = [&] (const auto& names) {
        alphabet += std::to_string (names.size ()) + ":";
        for (const auto& name : names)
          alphabet += std::to_string (name.size ()) + ":" + name;
      };
      append (inputs);
      append (outputs);
      char formula_hash[65], alphabet_hash[65], translation_hash[65], preprocessing_hash[65];
      hash (spot::str_psl (formula), formula_hash);
      hash (alphabet, alphabet_hash);
      hash (translation, translation_hash);
      hash (preprocessing, preprocessing_hash);
      const auto* w = active_worker_record ();
      char line[768];
      const int n = snprintf (
          line, sizeof line,
          "{\"event\":\"work_identity\",\"worker_pid\":%ld,\"mono_ns\":%llu,\"subjob\":%u,\"run_"
          "id\":%u,"
          "\"formula_sha256\":\"%s\",\"alphabet_sha256\":\"%s\",\"provider\":\"%s\","
          "\"translation_options_sha256\":\"%s\",\"preprocessing_options_sha256\":\"%s\"}\n",
          long (getpid ()), (unsigned long long) phase_clock (CLOCK_MONOTONIC), w ? w->subjob : 0,
          w ? w->run_id : 0, formula_hash, alphabet_hash, provider, translation_hash,
          preprocessing_hash);
      if (n > 0 && size_t (n) < sizeof line)
        phase_records_send (line, size_t (n));
      else
        phase_records_drop ();
    } catch (...) {
      phase_records_drop ();
    }
  }

  // This observer holds references only; serialization failures lose telemetry,
  // and cannot change translation or the formula passed to Spot.
  struct translation_identity {
      const std::vector<std::string>& inputs;
      const std::vector<std::string>& outputs;
      const char* preference;
      const char* preprocessing;
      bool push_inputs;
      std::string_view normalization_schedule;
      void operator() (spot::formula actual) const noexcept {
        if (!phase_records_enabled ())
          return;
        try {
          std::string options = std::string (preprocessing) +
                                (push_inputs ? ":push-inputs" : ":no-input-push");
          if (!normalization_schedule.empty ())
            options += ":tlsf-normalization=" + std::string (normalization_schedule);
          accounting_identity (
              actual, inputs, outputs, "frozen-graph",
              std::string (preference) +
                  ":BA:SBAcc:simul=0:ba-simul=0:det-simul=0:tls-impl=1:wdba-minimize=2",
              options);
        } catch (...) {
          phase_records_drop ();
        }
      }
  };
}
