#pragma once

#include "phase_records.hh"
#include "solver/diagnostics.hh"
#include <string_view>

#include <exception>
#include <optional>
#include <spot/tl/length.hh>
#include <spot/tl/print.hh>
#include <string>
#include <vector>

namespace acacia::unreal_witnesses {

  class records {
      worker_record* worker_ = nullptr;
      uint64_t source_ = 0, alphabet_ = 0;
      std::string alphabet_text_;
      phase_stamp entered_;

      // Invocation-local artifact binding only, following the existing Spot
      // boundary FNV convention. This digest is never an implication checker.
      static uint64_t digest (std::string_view text) noexcept {
        uint64_t result = 14695981039346656037ULL;
        for (unsigned char c : text) {
          result ^= c;
          result *= 1099511628211ULL;
        }
        return result;
      }
      static void append_partition (std::string& text, const std::vector<std::string>& aps) {
        text += std::to_string (aps.size ()) + ':';
        for (const auto& ap : aps)
          text += std::to_string (ap.size ()) + ':' + ap;
      }

    public:
      records () = default;
      void bind_source (std::string_view source, const std::vector<std::string>& inputs,
                        const std::vector<std::string>& outputs) noexcept {
        if (!phase_records_enabled ())
          return;
        worker_ = active_worker_record ();
        if (!worker_)
          return;
        try {
          source_ = digest (source);
          append_partition (alphabet_text_, inputs);
          append_partition (alphabet_text_, outputs);
          alphabet_ = digest (alphabet_text_);
        } catch (...) {
          phase_records_drop ();
          worker_ = nullptr;
        }
      }
      explicit operator bool () const noexcept { return worker_ != nullptr; }
      void event (const char* name, const char* fields) const noexcept {
        if (worker_)
          weakening_event (*worker_, name, fields);
      }
      void objective (const spot::formula& formula, const char* kind) const noexcept {
        if (!worker_)
          return;
        try {
          const auto hash = digest (spot::str_psl (formula));
          char fields[240];
          snprintf (fields, sizeof fields,
                    "\"kind\":\"%s\",\"objective_fnv1a64\":\"%016llx\","
                    "\"alphabet_fnv1a64\":\"%016llx\",\"structural_size\":%d",
                    kind, (unsigned long long) hash, (unsigned long long) alphabet_,
                    spot::length (formula));
          event ("weakening_binding", fields);
        } catch (...) {
          phase_records_drop ();
        }
      }
      void enter (const spot::formula& formula, uint64_t deadline,
                  const std::string& source_sha256, const char* format, const char* semantics,
                  const char* target, const char* effective, bool simplified, size_t inputs,
                  size_t outputs) noexcept {
        if (!worker_)
          return;
        ++worker_->prepass;
        worker_->run_id = 0;
        worker_->deadline_ns = deadline;
        entered_ = phase_start ();
        char fields[384];
        budget ("weakening_entry");
        event ("weakening_limits",
               "\"candidate_limit\":8,\"global_child_threshold\":64,\"local_budget\":null");
        // Metadata values come from the frontend's bounded semantics enums.
        snprintf (fields, sizeof fields,
                  "\"source_fnv1a64\":\"%016llx\",\"alphabet_fnv1a64\":\"%016llx\","
                  "\"inputs\":%zu,\"outputs\":%zu,\"format\":\"%s\",\"semantics\":\"%s\","
                  "\"target\":\"%s\",\"effective_target\":\"%s\",\"rsimp_changed\":%s",
                  (unsigned long long) source_, (unsigned long long) alphabet_, inputs, outputs,
                  format, semantics, target, effective, simplified ? "true" : "false");
        event ("weakening_source", fields);
        if (source_sha256.size () == 64 &&
            source_sha256.find_first_not_of ("0123456789abcdef") == std::string::npos) {
          snprintf (fields, sizeof fields, "\"source_sha256\":\"%s\"", source_sha256.c_str ());
          event ("weakening_source_digest", fields);
        }
        constexpr char hex[] = "0123456789abcdef";
        const auto chunks = (alphabet_text_.size () + 63) / 64;
        for (size_t i = 0; i < chunks; ++i) {
          char encoded[129] = {};
          const auto bytes = std::min (size_t {64}, alphabet_text_.size () - i * 64);
          for (size_t j = 0; j < bytes; ++j) {
            const auto c = static_cast<unsigned char> (alphabet_text_[i * 64 + j]);
            encoded[j * 2] = hex[c >> 4];
            encoded[j * 2 + 1] = hex[c & 15];
          }
          snprintf (fields, sizeof fields,
                    "\"encoding\":\"length_prefixed_partition_hex\",\"chunk\":%zu,\"chunks\":%zu,"
                    "\"data\":\"%s\"",
                    i, chunks, encoded);
          event ("weakening_alphabet", fields);
        }
        objective (formula, "simplified_original");
      }
      void eligibility (const char* reason, size_t oversized, size_t largest, size_t safety,
                        size_t obligations, size_t generated) const noexcept {
        if (!worker_)
          return;
        char fields[384];
        snprintf (fields, sizeof fields,
                  "\"eligible\":%s,\"reason\":\"%s\",\"oversized_globals\":%zu,"
                  "\"largest_global_conjunction_size\":%zu,"
                  "\"safety_conjuncts\":%zu,\"obligations\":%zu,\"generated\":%zu",
                  std::strcmp (reason, "eligible") == 0 ? "true" : "false", reason, oversized,
                  largest, safety, obligations, generated);
        event ("weakening_eligibility", fields);
      }
      void generated (const spot::formula& formula, unsigned index) const noexcept {
        if (!worker_)
          return;
        worker_->run_id = index + 1;
        objective (formula, "candidate");
        char fields[240];
        snprintf (fields, sizeof fields,
                  "\"candidate_index\":%u,\"derivation\":\"distribute_G_and_select_conjuncts\","
                  "\"premise\":\"simplified_original\",\"conclusion\":\"candidate\","
                  "\"obligation_index\":%u",
                  index, index);
        event ("weakening_candidate_generated", fields);
      }
      void reset_outcome () const noexcept {
        worker_record_text (worker_->reason, "none");
        worker_->stopped = false;
        worker_->verified = false;
      }
      void begin (unsigned index) const noexcept {
        if (!worker_)
          return;
        reset_outcome ();
        worker_->run_id = index + 1;
        worker_->run_start = phase_start ();
        worker_->run_weakening = true;
        worker_->run_active = true;
        budget ("weakening_attempt_start");
      }
      void budget (const char* name) const noexcept {
        if (!worker_)
          return;
        const auto now = phase_clock (CLOCK_MONOTONIC);
        const auto deadline = worker_->deadline_ns;
        char remaining[32] = "null", fields[160];
        if (deadline)
          snprintf (remaining, sizeof remaining, "%llu",
                    (unsigned long long) (deadline > now ? deadline - now : 0));
        snprintf (fields, sizeof fields, "\"deadline_ns\":%llu,\"remaining_ns\":%s",
                  (unsigned long long) deadline, remaining);
        event (name, fields);
      }
      void end (const char* outcome, const char* reason) const noexcept {
        if (!worker_)
          return;
        rusage usage {};
        char peak[32] = "null";
        if (getrusage (RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
          usage.ru_maxrss /= 1024;
#endif
          snprintf (peak, sizeof peak, "%ld", usage.ru_maxrss);
        }
        const auto now = phase_clock (CLOCK_MONOTONIC);
        const auto cpu = phase_clock (CLOCK_PROCESS_CPUTIME_ID);
        char fields[360];
        snprintf (fields, sizeof fields,
                  "\"outcome\":\"%s\",\"reason\":\"%s\",\"observer\":\"worker\","
                  "\"wall_ns\":%llu,\"cpu_ns\":%llu,\"peak_rss_kb\":%s,\"rss_kb\":null,"
                  "\"memory_reason\":\"process_peak_only\"",
                  outcome, reason, (unsigned long long) (now - worker_->run_start.wall),
                  (unsigned long long) (cpu - worker_->run_start.cpu), peak);
        budget ("weakening_attempt_budget");
        event ("weakening_attempt_end", fields);
        if (std::strcmp (outcome, "proof") == 0)
          event ("weakening_proof",
                 "\"claim\":\"UNREAL(original)\",\"checked_objective\":\"candidate\","
                 "\"proof_objective\":\"runner_objective\","
                 "\"derivation\":\"distribute_G_and_select_conjuncts\","
                 "\"proof_kind\":\"incumbent_UNREAL_runner\","
                 "\"source_transfer\":\"realizability_preserving_simplification\"");
        worker_->run_active = false;
      }
      void fallback (const spot::formula& original) const noexcept {
        if (!worker_)
          return;
        reset_outcome ();
        worker_->run_id = 0;
        worker_->run_weakening = false;
        worker_->run_active = true;
        worker_->run_start = phase_start ();
        objective (original, "incumbent_objective");
        budget ("weakening_fallback_start");
      }
      void finish (const char* outcome) const noexcept {
        if (!worker_)
          return;
        char fields[160];
        snprintf (fields, sizeof fields, "\"outcome\":\"%s\",\"wall_ns\":%llu,\"cpu_ns\":%llu",
                  outcome, (unsigned long long) (phase_clock (CLOCK_MONOTONIC) - entered_.wall),
                  (unsigned long long) (phase_clock (CLOCK_PROCESS_CPUTIME_ID) - entered_.cpu));
        event ("weakening_end", fields);
      }

      class attempt {
          const records& records_;
          unsigned long long declines_ = 0, stops_ = 0;
          bool ended_ = false;

        public:
          attempt (const records& record, unsigned index) noexcept : records_ (record) {
            if (record.worker_) {
              declines_ = record.worker_->declines;
              stops_ = record.worker_->stops;
            }
            records_.begin (index);
          }
          void finish (bool proof) noexcept {
            const auto* worker = records_.worker_;
            const bool stopped = worker && worker->stops != stops_;
            const bool declined = worker && worker->declines != declines_;
            const bool cancelled = stopped && std::strcmp (worker->reason, "cancelled") == 0;
            records_.end (proof                  ? "proof"
                          : cancelled            ? "cancellation"
                          : declined && !stopped ? "decline"
                                                 : "inconclusive",
                          stopped || declined ? worker->reason : "none");
            ended_ = true;
          }
          ~attempt () {
            if (!ended_)
              records_.end ("exception", "runner_exception");
          }
      };
  };

}  // namespace acacia::unreal_witnesses
