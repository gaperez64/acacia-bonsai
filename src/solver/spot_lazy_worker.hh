#pragma once

#include "solver/game_backend.hh"
#include "solver/closure_buchi_provider.hh"
#include "solver/spot_worker_record.hh"
#include "solver/k_schedule.hh"
#include "solver/spot_candidate_limits.hh"
#include "solver/spot_lazy_game.hh"
#include "utils/verbose.hh"
#include <spot/tl/print.hh>
#include <sys/resource.h>
#include <iomanip>

namespace acacia::spot_lazy_worker {
  enum class Outcome { win, kmax, unknown };

  inline const char* closure_failure_name (closure_buchi::FailureKind kind) {
    using F = closure_buchi::FailureKind;
    switch (kind) {
      case F::unsupported_operator: return "unsupported_operator";
      case F::normalization_limit: return "normalization_limit";
      case F::branch_limit: return "branch_limit";
      case F::guard_limit: return "guard_limit";
      case F::state_limit: return "state_limit";
      case F::row_limit: return "row_limit";
      case F::memory_limit: return "memory_limit";
      case F::cancelled: return "cancelled";
      case F::injected: return "injected";
      case F::invalid_state: return "invalid_state";
      case F::unexpected: return "unexpected";
    }
    return "unexpected";
  }
  inline std::string failure_reason (std::exception_ptr error, const char* otherwise) {
    if (error) try { std::rethrow_exception (error); }
    catch (const closure_buchi::AdapterFailure& e) {
      return std::string ("closure-buchi:") + closure_failure_name (e.failure ().kind);
    }
    catch (...) {}
    return otherwise;
  }

  // Exact, length-delimited worker identity; FNV-1a-64 is a reproducible identity
  // checksum, not a security hash. Include interface APs absent from the formula.
  inline void capture_boundary (spot::formula f, const std::vector<std::string>& inputs,
                                 const std::vector<std::string>& outputs,
                                 const std::string& target, spot_lazy_game::Reporter report) {
    std::string identity;
    auto append = [&] (const std::string& value) {
      identity += std::to_string (value.size ()) + ':' + value;
    };
    append (spot::str_psl (f));
    append (target);
    append ("infinite-word;forall-input-exists-output;avoid-transition-buchi;cursor=0;rank=0");
    for (const auto* aps : {&inputs, &outputs}) {
      append (std::to_string (aps->size ()));
      for (const auto& ap : *aps) append (ap);
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : identity) { hash ^= c; hash *= 1099511628211ULL; }
    std::ostringstream hex;
    hex << std::hex << std::setfill ('0') << std::setw (16) << hash;
    report.put ("worker_boundary", identity);
    report.put ("worker_boundary_hash", "fnv1a64:" + hex.str ());
    report.put ("worker_target_semantics", target);
    report.put ("initial_convention", "cursor=0,rank=0");
  }

  // One factory for workers and replay; eager selection changes only RowStore's
  // exploration policy. No second cursor, translator or graph preprocessing.
  inline std::unique_ptr<spot_lazy_game::RowStore> make_closure_store (
      spot::formula f, const spot::bdd_dict_ptr& dict, spot_rows::RowLimits rows,
      spot_lazy_game::Reporter report = {}, closure_buchi::Options options = {}) {
    namespace game = spot_lazy_game;
    const auto started = game::Clock::now ();
    options.max_rows = std::min (options.max_rows, rows.max_rows);
    options.max_edges_per_row = std::min (options.max_edges_per_row, rows.max_edges_per_row);
    auto built = closure_buchi::Provider::create (f, dict, options);
    report.ms ("factory_return_ms", game::elapsed (started));
    if (auto* failure = std::get_if<closure_buchi::Failure> (&built))
      throw closure_buchi::AdapterFailure (*failure);
    auto provider = std::get<std::shared_ptr<closure_buchi::Provider>> (std::move (built));
    auto snapshot = [provider, mode = options.row_expansion] (const game::Reporter& r) {
      const auto& c = provider->counters ();
      r.put ("row_expansion_mode", closure_buchi::row_expansion_name (mode));
      r.ms ("closure_factory_ms", c.factory_ns / 1e6);
      r.ms ("closure_normalization_ms", c.normalization_ns / 1e6);
      r.ms ("closure_raw_row_ms", c.raw_row_ns / 1e6);
      r.ms ("closure_cursor_row_ms", c.cursor_row_ns / 1e6);
      if (c.complete_rows) r.ms ("first_row_ms", c.first_row_ns / 1e6);
      r.count ("closure_branches_considered", c.branches_considered);
      r.count ("closure_branches_pruned", c.branches_pruned);
      r.count ("closure_guards_generated", c.guards_generated);
      r.count ("closure_states_discovered", c.states_discovered);
      r.count ("closure_complete_rows", c.complete_rows);
      r.count ("closure_raw_rows", c.raw_rows);
      r.count ("closure_edges", c.edges);
      r.count ("closure_retained_bytes", c.retained_bytes);
      // A1 Boolean-guard folding; all 0 under RowExpansion::enumerative.
      r.ms ("boolean_conversion_ms", c.boolean_conversion_ns / 1e6);
      r.count ("boolean_cache_hits", c.boolean_cache_hits);
      r.count ("boolean_cache_misses", c.boolean_cache_misses);
      r.count ("boolean_conversion_calls", c.boolean_conversion_calls);
      r.count ("boolean_complete_entries", c.boolean_complete_entries);
    };
    auto store = std::make_unique<game::RowStore> (
        game::FixedBuchi {{provider}, snapshot, {}}, rows, report);
    store->factory_started = started;
    game::require (provider->complete_rows () == 0 && provider->discovered_states () == 1);
    store->snapshot ();
    return store;
  }

  // Decision only: no graph/guard/constant-output certificate can escape to
  // controller synthesis. The caller supplies the existing transformed job.
  inline Outcome solve (spot::formula worker_formula, const spot::bdd_dict_ptr& dict,
                         bdd all_inputs, bdd all_outputs, int kmin, int kmax, int kinc,
                         spot_guarded::Limits limits = spot_taa_candidate_limits (),
                         automaton_provider selected = automaton_provider::spot_lazy) {
    namespace game = spot_lazy_game;
    const auto started = game::Clock::now ();
    game::Reporter report;
    const bool closure = is_closure_provider (selected);
    const bool eager = is_eager_provider (selected);
    const char* provider_name = automaton_provider_name (selected);
    const auto sink = [provider_name, closure] (const std::string& key, const std::string& value) {
      spot_records::put (key, value);
      if (closure && key == "reason" && value != "none")
        std::cerr << provider_name << " UNKNOWN: " << value << '\n';
      verb_do (1, utils::vout << provider_name << ' ' << key << '=' << value << std::endl);
    };
    if (closure || spot_records::active) report.sink = sink;
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
    report.put ("backend", closure ? "spot-guarded-sparse" : "spot-guarded");
    report.put ("construction", closure ? "closure-buchi,cursor=one-obligation,initial_rank=0"
                                        : "ltl_to_taa,refined_rules=false,cursor=P5,initial_rank=0");
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
      std::unique_ptr<game::RowStore> owned_store;
      if (closure) {
        closure_buchi::Options options;
        options.max_states = provider_limits.max_states;
        owned_store = make_closure_store (worker_formula, dict, limits.rows, report, options);
      }
      else {
        auto built = spot_lazy::make_view ([&] () -> spot::const_twa_ptr {
          if (not worker_formula.is_ltl_formula ())
            throw spot_lazy::Declined ("TAA route requires LTL");
          return std::make_shared<game::ObservedProvider> (
              spot::ltl_to_taa (worker_formula, dict, false), report);
        }, provider_limits);
        if (not built.value) {
          report.put ("status", "UNKNOWN");
          if (built.error) std::rethrow_exception (built.error);
          return Outcome::unknown;
        }
        owned_store = std::make_unique<game::RowStore> (*built.value, limits.rows, report);
      }
      report.ms ("factory_ms", game::elapsed (factory_started));
      auto& store = *owned_store;
      const auto& view = store.provider;
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
      game::require (store.cache->state_count () == 1 && store.cache->complete_rows () == 0);
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
        report.count ("k", k);
        // Immutable provider rows survive K; all rank, proof, oracle, strategy
        // and verification data belong to this attempt and are discarded.
        spot_records::phase ("search");
        game::Search search {store, alphabet, int32_t (k), limits};
        const auto result = search.solve ();
        store.check_contract ();
        store.snapshot ();
        report.ms ("search_ms", result.solve_ms);
        report.ms ("verification_ms", result.verify_ms);
        report.count ("game_states", result.nodes.size ());
        report.count ("guarded_choices", result.choices_created);
        report.put ("status", solver_detail::forward_result_name (result.status));
        report.put ("reason", failure_reason (store.row_error, spot_letters::unknown_name (result.failure)));
        spot_records::phase ("verified-attempt");
        if (result.status == solver_detail::forward_result_status::win_k)
          return Outcome::win;
        if (result.status != solver_detail::forward_result_status::lose_k)
          return Outcome::unknown;
        const auto next = k_schedule::next (
            ACACIA_K_SCHEDULE, k, kmin, kmax, kinc,
            {static_cast<long long> (result.solve_ms), result.proofs.size (), result.expansions, true});
        if (not next) return Outcome::kmax;
        k = *next;
      }
    } catch (const closure_buchi::AdapterFailure& e) {
      report.put ("reason", std::string ("closure-buchi:") + closure_failure_name (e.failure ().kind));
    } catch (const spot_letters::detail::Failure& e) {
      report.put ("reason", failure_reason (e.error, spot_letters::unknown_name (e.why)));
    } catch (const std::exception& e) {
      report.put ("reason", e.what ());
    } catch (...) {
      report.put ("reason", "provider-or-query-failure");
    }
    report.put ("status", "UNKNOWN");
    return Outcome::unknown;
  }
} // namespace acacia::spot_lazy_worker
