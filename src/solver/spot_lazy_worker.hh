#pragma once

#include "solver/game_backend.hh"
#include "solver/spot_worker_record.hh"
#include "solver/k_schedule.hh"
#include "solver/spot_candidate_limits.hh"
#include "solver/spot_lazy_game.hh"
#include "utils/verbose.hh"
#include <spot/tl/print.hh>
#include <sys/resource.h>

namespace acacia::spot_lazy_worker {
  enum class Outcome { win, kmax, unknown };

  // Decision only: no graph/guard/constant-output certificate can escape to
  // controller synthesis. The caller supplies the existing transformed job.
  inline Outcome solve (spot::formula worker_formula, const spot::bdd_dict_ptr& dict,
                         bdd all_inputs, bdd all_outputs, int kmin, int kmax, int kinc,
                         spot_guarded::Limits limits = spot_taa_candidate_limits (), bool eager = false,
                         LossCheckPolicy loss_check_policy = LossCheckPolicy::verify_all) {
    namespace game = spot_lazy_game;
    const auto started = game::Clock::now ();
    game::Reporter report;
    const char* provider_name = eager ? "spot-eager" : "spot-lazy";
    const auto sink = [provider_name] (const std::string& key, const std::string& value) {
      spot_records::put (key, value);
      verb_do (1, utils::vout << provider_name << ' ' << key << '=' << value << std::endl);
    };
    if (spot_records::active) report.sink = sink;
    verb_do (1, report.sink = sink);
    struct Accounting {
        game::Reporter report;
        game::Clock::time_point start;
        ~Accounting () {
          rusage usage {};
          getrusage (RUSAGE_SELF, &usage);
          report.ms ("end_to_end_ms", game::elapsed (start));
          report.count ("peak_rss_bytes", size_t (usage.ru_maxrss) * 1024);
        }
    } accounting {report, started}; // includes provider destruction
    if (report.sink) {
      std::ostringstream text;
      text << worker_formula;
      report.put ("worker_formula", text.str ());
    }
    report.put ("provider", provider_name);
    report.put ("backend", "spot-guarded");
    report.put ("construction", "ltl_to_taa,refined_rules=false,cursor=P5,initial_rank=0");
    report.count ("max_expansions", limits.max_expansions);
    report.count ("max_rows", limits.rows.max_rows);
    report.count ("max_rank_nodes", limits.max_rank_nodes);
    report.count ("max_query_steps", limits.queries.max_steps);
    try {
      spot_lazy::Limits provider_limits;
      provider_limits.rows = limits.rows;
      provider_limits.max_states = diagnostics::env_size (
          "ACACIA_SPOT_MAX_PROVIDER_STATES", provider_limits.max_states, true);
      report.count ("max_provider_states", provider_limits.max_states);
      const auto factory_started = game::Clock::now ();
      report.put ("stage", "factory");
      spot_records::phase ("factory");
      auto built = spot_lazy::make_view ([&] () -> spot::const_twa_ptr {
        if (not worker_formula.is_ltl_formula ())
          throw spot_lazy::Declined ("TAA route requires LTL");
        return std::make_shared<game::ObservedProvider> (
            spot::ltl_to_taa (worker_formula, dict, false), report);
      }, provider_limits);
      report.ms ("factory_ms", game::elapsed (factory_started));
      if (not built.value) {
        report.put ("status", "UNKNOWN");
        if (built.error) std::rethrow_exception (built.error);
        return Outcome::unknown;
      }
      auto view = *built.value;
      spot_letters::WorkerAlphabet alphabet {
          view->ap_vars (), bdd_exist (view->ap_vars (), all_outputs),
          bdd_exist (view->ap_vars (), all_inputs), {}};
      std::string partition, order;
      for (const auto& ap : view->ap ()) {
        const int var = dict->varnum (ap);
        alphabet.order.push_back (var);
        if (report.sink) {
          order += std::to_string (ap.ap_name ().size ()) + ':' + ap.ap_name ();
          partition += bdd_exist (bdd_ithvar (var), alphabet.inputs) == bddtrue ? 'u' : 'c';
        }
      }
      report.put ("ap_order", order);
      report.put ("partition", partition);
      game::RowStore store {view, provider_limits.rows, report};
      game::require (store.cache->state_count () == 1 && store.cache->complete_rows () == 0 &&
                     view->underlying_rows () == 0);
      store.snapshot ();
      if (eager) {
        const auto enumeration_started = game::Clock::now ();
        report.put ("stage", "enumeration");
        spot_records::phase ("enumeration");
        store.enumerate_and_freeze ();
        report.ms ("eager_ms", game::elapsed (enumeration_started));
        report.count ("total_wrapper_rows", store.cache->complete_rows ());
      }
      for (long long k = kmin;;) {
        spot_records::begin_attempt (k);
        report.count ("k", k);
        const auto rows_before = store.cache->complete_rows ();
        const auto generation_before = store.generation_ms;
        // Immutable provider rows survive K; all rank, proof, oracle, strategy
        // and verification data belong to this attempt and are discarded.
        spot_records::phase ("search");
        const auto result = [&] {
          game::Search search {store, alphabet, int32_t (k), limits};
          return search.solve_for_schedule (loss_check_policy);
        } ();
        view->check_contract ();
        store.snapshot ();
        const auto& metrics = game::attempt_metrics (result);
        report.ms ("search_ms", metrics.solve_ms);
        report.ms ("verification_ms", metrics.verify_ms);
        report.count ("game_states", metrics.nodes);
        report.count ("guarded_choices", metrics.choices);
        report.put ("status", game::attempt_status (result));
        report.put ("reason", spot_letters::unknown_name (game::attempt_failure (result)));
        report.ms ("attempt_row_generation_ms", store.generation_ms - generation_before);
        report.count ("attempt_rows_generated", store.cache->complete_rows () - rows_before);
        spot_records::end_attempt (game::attempt_status (result), game::attempt_evidence (result));
        const auto step = game::schedule_attempt (result, ACACIA_K_SCHEDULE, kmin, kmax, kinc);
        if (step.action == game::SchedulingAction::win) return Outcome::win;
        if (step.action == game::SchedulingAction::inconclusive) return Outcome::unknown;
        if (step.action == game::SchedulingAction::exhausted) return Outcome::kmax;
        k = step.next_k;
      }
    } catch (const std::exception& e) {
      report.put ("reason", e.what ());
    } catch (...) {
      report.put ("reason", "provider-or-query-failure");
    }
    report.put ("status", "UNKNOWN");
    spot_records::end_attempt ("UNKNOWN", "exception");
    return Outcome::unknown;
  }
} // namespace acacia::spot_lazy_worker
