#include "solver/solver_invoker.hh"

#include <cstdlib>
#include <csignal>
#include <sys/wait.h>
#include <string>

namespace {
  void check (bool condition) { if (!condition) std::abort (); }
  std::string contents (const std::filesystem::path& directory) {
    for (const auto& entry : std::filesystem::directory_iterator (directory))
      if (entry.path ().extension () == ".json") {
        std::ifstream input (entry.path ());
        return {std::istreambuf_iterator<char> (input), std::istreambuf_iterator<char> ()};
      }
    return {};
  }
  void has (const std::string& text, const char* field, const char* value) {
    check (text.find (std::string ("\"") + field + "\":\"" + value + '"') != std::string::npos);
  }
  void worker_records () {
    namespace records = acacia::spot_records;
    char directory[] = "/tmp/acacia-record-test-XXXXXX";
    check (mkdtemp (directory));
    const std::filesystem::path root {directory};
    setenv ("ACACIA_SPOT_CAPTURE_HISTORY", "1", 1);
    const auto attempts = root / "attempts";
    setenv ("ACACIA_SPOT_CAPTURE_DIR", attempts.c_str (), 1);
    {
      records::Record record;
      records::segment ("frozen-graph", "spot-guarded-sparse");
      records::put ("factory_ms", "9");
      records::begin_attempt (1);
      records::phase ("search");
      records::put ("search_ms", "12");
      // Row time is included in search, never charged again in cumulative search.
      records::put ("attempt_row_generation_ms", "4");
      records::put ("game_states", "100");
      records::phase ("verification");
      records::put ("verification_ms", "3");
      records::end_attempt ("LOSE_K", "verified-loss");
      records::begin_attempt (2);
      auto text = contents (attempts);
      has (text, "attempt_id", "2");
      has (text, "k", "2");
      has (text, "search_started", "false");
      check (text.find ("\"game_states\"") == std::string::npos);
      check (text.find ("\"verification_ms\"") == std::string::npos);
      has (text, "cumulative_search_ms", "12.000000");
      records::phase ("search");
      records::put ("search_ms", "2");
      records::end_attempt ("RESOURCE_LIMIT", "none");
      records::segment ("frozen-graph", "backward", true);
      text = contents (attempts);
      has (text, "segment_id", "2");
      has (text, "fallback", "true");
      has (text, "backend", "backward");
      has (text, "search_started", "false");
      has (text, "cumulative_search_ms", "14.000000");
      has (text, "cumulative_attempt_row_generation_ms", "4.000000");
      check (text.find ("\"k\"") == std::string::npos);
      check (text.find ("\"factory_ms\"") == std::string::npos);
      records::begin_attempt (1);
      records::phase ("action-construction");
      records::phase ("search");
      records::end_attempt ("WIN_K", "fixedpoint");
      // A language/Spot decision is unbounded, even after a bounded attempt.
      records::begin_attempt ();
      text = contents (attempts);
      check (text.find ("\"k\"") == std::string::npos);
      has (text, "search_started", "false");
      has (text, "status", "UNKNOWN");
      records::end_attempt ("WIN", "spot-game");
    }
    has (contents (attempts), "worker_end", "returned");
    const auto hints = root / "hints";
    setenv ("ACACIA_SPOT_CAPTURE_DIR", hints.c_str (), 1);
    {
      records::Record record;
      for (int k = 1; k <= 3; ++k) {
        records::begin_attempt (k);
        auto text = contents (hints);
        check (text.find ("\"loss_hints\"") == std::string::npos);
        check (text.find ("\"win_verification_ms\"") == std::string::npos);
        records::phase ("search");
        records::put ("loss_hints", k < 3 ? "1" : "0");
        records::put ("loss_hint_ms", k < 3 ? "0.5" : "0");
        records::put ("loss_verification_calls", "0");
        records::put ("loss_verification_ms", "0");
        records::put ("win_verification_calls", k == 3 ? "1" : "0");
        records::put ("win_verification_ms", k == 3 ? "2" : "0");
        records::end_attempt (k < 3 ? "LOSS_HINT" : "WIN_K", k < 3 ? "loss-hint" : "verified-win");
        text = contents (hints);
        has (text, "evidence", k < 3 ? "loss-hint" : "verified-win");
        if (k < 3) check (text.find ("verified") == std::string::npos);
      }
    }
    const auto totals = contents (hints);
    has (totals, "cumulative_loss_hints", "2.000000");
    has (totals, "cumulative_loss_hint_ms", "1.000000");
    has (totals, "cumulative_loss_verification_calls", "0.000000");
    has (totals, "cumulative_loss_verification_ms", "0.000000");
    has (totals, "cumulative_win_verification_calls", "1.000000");
    has (totals, "cumulative_win_verification_ms", "2.000000");
    std::cout << "PASS hint records: distinct evidence, fresh counters, cumulative calls and times\n";
    for (bool unreal : {false, true}) {
      const auto empty = root / (unreal ? "empty-unreal" : "empty-real");
      setenv ("ACACIA_SPOT_CAPTURE_DIR", empty.c_str (), 1);
      {
        records::Record record;
        records::phase ("preprocessing");
        // Exercise the worker's zero-state return boundary directly: current
        // Spot normally represents the empty language with one dead state.
        check (acacia::solver_detail::finish_empty_translation (unreal) == !unreal);
      }
      const auto text = contents (empty);
      has (text, "stage", "attempt-end");
      has (text, "search_started", "false");
      has (text, "attempt_end", "true");
      has (text, "worker_end", "returned");
      has (text, "status", unreal ? "UNKNOWN" : "WIN");
      has (text, "evidence", "empty-language");
      has (text, "worker_result", unreal ? "unknown" : "solved");
      check (text.find ("\"k\"") == std::string::npos);
    }
    std::cout << "PASS empty-translation real/unreal lifecycle without search or K\n";
    const auto exception = root / "exception";
    setenv ("ACACIA_SPOT_CAPTURE_DIR", exception.c_str (), 1);
    try {
      records::Record record;
      records::begin_attempt (1);
      records::phase ("verification");
      throw std::runtime_error ("test exception");
    } catch (const std::runtime_error&) {}
    has (contents (exception), "worker_end", "exception");
    has (contents (exception), "evidence", "exception");
    has (contents (exception), "status", "UNKNOWN");

    const auto cancelled = root / "cancelled";
    setenv ("ACACIA_SPOT_CAPTURE_DIR", cancelled.c_str (), 1);
    int ready[2];
    check (pipe (ready) == 0);
    const pid_t child = fork ();
    check (child >= 0);
    if (child == 0) {
      close (ready[0]);
      records::Record record;
      records::begin_attempt (1);
      records::phase ("search");
      check (write (ready[1], "x", 1) == 1);
      for (;;) pause ();
    }
    close (ready[1]);
    char byte;
    check (read (ready[0], &byte, 1) == 1);
    close (ready[0]);
    check (kill (child, SIGKILL) == 0);
    int status;
    check (waitpid (child, &status, 0) == child && WIFSIGNALED (status));
    has (contents (cancelled), "worker_end", "unobserved");
    has (contents (cancelled), "attempt_end", "false");
    unsetenv ("ACACIA_SPOT_CAPTURE_DIR");
    unsetenv ("ACACIA_SPOT_CAPTURE_HISTORY");
    check (records::active == nullptr);
    records::Record disabled;
    check (!disabled && records::active == nullptr);
    std::filesystem::remove_all (root);
  }
}

int main () {
  worker_records ();
  std::cout << "PASS worker-record lifecycle: resets, unbounded decisions, fallback, exception, cancellation\n";
#if ACACIA_ENABLE_DIAGNOSTICS
  setenv ("ACACIA_DIAG", "1", 1);
  acacia::diagnostics::scoped_child child {"test"};
  auto* metrics = acacia::diagnostics::current ();
  if (metrics == nullptr)
    return 1;
  metrics->translation_ms = 7;
  metrics->final_reason = "unknown";

  {
    acacia::diagnostics::scoped_attempt attempt;
    metrics->translation_ms = 19;
    acacia::diagnostics::finish (false, "discarded-witness");
  }
  if (metrics->translation_ms != 7 or metrics->result != "unknown" or
      metrics->final_reason != "unknown")
    return 2;

  {
    acacia::diagnostics::scoped_attempt attempt;
    metrics->translation_ms = 23;
    acacia::diagnostics::finish (true, "accepted-witness");
    attempt.commit ();
  }
  if (metrics->translation_ms != 23 or metrics->result != "solved" or
      metrics->final_reason != "accepted-witness")
    return 3;
#endif
  return 0;
}
