#pragma once

#include "phase_records.hh"
#include "solver/diagnostics.hh"
#include "solver/unreal_weakening_budget.hh"
#include <string_view>

#include <exception>
#include <optional>
#include <spot/tl/length.hh>
#include <spot/tl/print.hh>
#include <string>
#include <vector>

namespace acacia::unreal_witnesses {

  inline uint64_t binding_hash (std::string_view text) noexcept {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : text) {
      h ^= c;
      h *= 1099511628211ULL;
    }
    return h;
  }

  // A receipt is created only AFTER the guarded backend's independent checker.
  // These invocation-local hashes bind artifacts; they do not prove implication
  // or replace certificate checking. No weakened certificate claims the original game.
  struct checked_game {
      uint64_t objective = 0, context = 0, runner_objective = 0, game = 0, proof = 0;
      uint64_t seal = 0;
      bool verified = false;
      uint64_t binding () const noexcept {
        uint64_t h = objective;
        for (auto v : {context, runner_objective, game, proof})
          h = (h ^ v) * 1099511628211ULL;
        return h;
      }
      bool valid () const noexcept {
        return verified && game && proof && runner_objective && seal == binding ();
      }
  };
  inline thread_local checked_game* checking_candidate = nullptr;

  class records {
      worker_record* worker_ = nullptr;
      uint64_t source_ = 0, alphabet_ = 0;
      std::string alphabet_text_;
      phase_stamp entered_;
      bool extended_ = false;

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
                  size_t outputs, const char* mode = "basic") noexcept {
        if (!worker_)
          return;
        extended_ = std::strcmp (mode, "extended") == 0;
        ++worker_->prepass;
        worker_->run_id = 0;
        worker_->deadline_ns = deadline;
        entered_ = phase_start ();
        char fields[384];
        budget ("weakening_entry");
        if (!extended_)
          event ("weakening_limits",
                 "\"candidate_limit\":8,\"global_child_threshold\":64,\"local_budget\":null");
        snprintf (fields, sizeof fields, "\"mode\":\"%s\"", mode);
        event ("weakening_mode", fields);
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
      void generated (const spot::formula& formula, unsigned candidate_index) const noexcept {
        if (!worker_)
          return;
        worker_->run_id = candidate_index + 1;
        objective (formula, "candidate");
        char fields[240];
        snprintf (fields, sizeof fields,
                  "\"candidate_index\":%u,\"derivation\":\"%s\","
                  "\"premise\":\"%s\",\"conclusion\":\"candidate\","
                  "\"obligation_index\":%u",
                  candidate_index,
                  extended_ ? "exact_AST_positive_conjunct_deletion"
                            : "distribute_G_and_select_conjuncts",
                  extended_ ? "exact_original" : "simplified_original", candidate_index);
        event ("weakening_candidate_generated", fields);
      }
      void extended_limits (uint64_t until, uint64_t per_attempt,
                            const allowances& options) const noexcept {
        event ("weakening_limits",
               "\"candidate_limit\":8,\"group_limit\":4,\"group_size_limit\":4,"
               "\"ast_node_limit\":16384,\"ast_edge_limit\":32768,"
               "\"support_membership_limit\":32768,\"guarantee_limit\":8192,"
               "\"support_visit_limit\":32768,\"global_child_threshold\":64,\"poll_ns\":1000000");
        char fields[384];
        snprintf (
            fields, sizeof fields,
            "\"attempt_fraction\":0.05,\"prepass_fraction\":0.20,\"cleanup_fraction\":0.5,"
            "\"attempt_ms\":%llu,\"total_ms\":%llu,"
            "\"attempt_override\":%s,\"total_override\":%s,"
            "\"unbounded_attempt_ns\":%llu,\"unbounded_total_ns\":%llu,"
            "\"memory_scope\":\"inherited_invocation\"",
            (unsigned long long) options.attempt_ms.value_or (default_attempt_ms),
            (unsigned long long) options.total_ms.value_or (default_total_ms),
            options.attempt_ms ? "true" : "false", options.total_ms ? "true" : "false",
            (unsigned long long) milliseconds_ns (
                options.attempt_ms.value_or (default_attempt_ms)),
            (unsigned long long) milliseconds_ns (options.total_ms.value_or (default_total_ms)));
        event ("weakening_budget_limits", fields);
        snprintf (fields, sizeof fields, "\"prepass_deadline_ns\":%llu,\"attempt_limit_ns\":%llu",
                  (unsigned long long) until, (unsigned long long) per_attempt);
        event ("weakening_local_budget", fields);
      }
      void attempt_limit (uint64_t deadline) const noexcept {
        char fields[96];
        snprintf (fields, sizeof fields, "\"attempt_deadline_ns\":%llu",
                  (unsigned long long) deadline);
        event ("weakening_attempt_limit", fields);
      }
      void selection (unsigned index, const std::vector<size_t>& kept,
                      const char* derivation) const noexcept {
        if (!worker_)
          return;
        worker_->run_id = index + 1;
        char fields[240];
        snprintf (fields, sizeof fields,
                  "\"derivation\":\"%s\",\"kept_count\":%zu,"
                  "\"antecedent_preserved\":true",
                  derivation, kept.size ());
        event ("weakening_transformation", fields);
        for (auto j : kept) {
          snprintf (fields, sizeof fields, "\"retained_guarantee_index\":%zu", j);
          event ("weakening_subset", fields);
        }
      }
      void path_step (size_t step, size_t child, const char* rule) const noexcept {
        char fields[160];
        snprintf (fields, sizeof fields, "\"step\":%zu,\"child\":%zu,\"rule\":\"%s\"", step, child,
                  rule);
        event ("weakening_path", fields);
      }
      void checked (const checked_game& proof) const noexcept {
        if (!worker_)
          return;
        char fields[260];
        snprintf (fields, sizeof fields,
                  "\"kind\":\"runner_objective\",\"objective_fnv1a64\":\"%016llx\"",
                  (unsigned long long) proof.runner_objective);
        event ("weakening_binding", fields);
        snprintf (fields, sizeof fields,
                  "\"game_fnv1a64\":\"%016llx\",\"proof_fnv1a64\":\"%016llx\","
                  "\"context_fnv1a64\":\"%016llx\","
                  "\"verifier\":\"guarded_independent_winning_certificate\"",
                  (unsigned long long) proof.game, (unsigned long long) proof.proof,
                  (unsigned long long) proof.context);
        event ("weakening_checked_game", fields);
      }
      void bounded_end (const char* outcome, const char* reason, uint64_t wall,
                        const rusage& usage) const noexcept {
        if (!worker_)
          return;
        char fields[360];
        auto peak = usage.ru_maxrss;
#ifdef __APPLE__
        peak /= 1024;
#endif
        const uint64_t cpu =
            uint64_t (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000000ULL +
            uint64_t (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1000;
        snprintf (fields, sizeof fields,
                  "\"outcome\":\"%s\",\"reason\":\"%s\",\"observer\":\"worker\","
                  "\"wall_ns\":%llu,\"cpu_ns\":%llu,\"peak_rss_kb\":%ld,"
                  "\"memory_reason\":\"reaped_attempt_process_peak\"",
                  outcome, reason, (unsigned long long) wall, (unsigned long long) cpu, peak);
        budget ("weakening_attempt_budget");
        event ("weakening_attempt_end", fields);
        if (std::strcmp (outcome, "proof") == 0)
          event ("weakening_proof",
                 "\"claim\":\"UNREAL(original)\",\"checked_objective\":\"candidate\","
                 "\"certificate_claim\":\"derived_game_only\","
                 "\"derivation\":\"exact_AST_positive_conjunct_deletion\","
                 "\"source_transfer\":\"exact_frontend_normalization_or_raw_AST\"");
        worker_->run_active = false;
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
                 "\"proof_kind\":\"UNREAL_runner\","
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
        objective (original, "original_objective");
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
