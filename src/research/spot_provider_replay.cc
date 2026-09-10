// P6 replay. One invocation, one capped worker, one TAA factory, one arm.
// --formula is the captured bad-language WORKER formula (no extra negation).
// Search and verification are shared with the P7 worker integration.
#include "solver/k_schedule.hh"
#include "solver/spot_lazy_game.hh"
#include "solver/spot_guarded_forward_safety.hh"
#include "solver/spot_lazy_buchi_view.hh"
#include "utils/verbose.hh"
#include <unordered_map>

#include <cerrno>
#include <charconv>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <spot/misc/version.hh>
#include <spot/tl/parse.hh>
#include <spot/tl/print.hh>
#include <sstream>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace replay {
  using namespace acacia::spot_lazy_game;
  using Fields = std::map<std::string, std::string>;
  struct InvalidInput : std::runtime_error {
      using std::runtime_error::runtime_error;
  };
  size_t peak_bytes () {
    rusage r {};
    getrusage (RUSAGE_SELF, &r);
    return size_t (r.ru_maxrss) * 1024;
  }
  std::string rss_bytes () {
    std::ifstream f {"/proc/self/statm"};
    size_t total, resident;
    if (!(f >> total >> resident)) return "NA";
    return std::to_string (resident * size_t (sysconf (_SC_PAGESIZE)));
  }
  Reporter pipe_reporter (int fd) {
    return {[fd] (const std::string& k, const std::string& v) {
      const auto s = k + '\t' + cell (v) + '\n';
      size_t offset = 0;
      while (offset < s.size ()) {
        const auto n = write (fd, s.data () + offset, s.size () - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) std::_Exit (2);
        offset += size_t (n);
      }
    }};
  }
} // namespace replay

namespace replay {
  struct Options {
      std::string arm, formula;
      std::optional<std::string> partition;
      int k = 0, kmax = 0, kinc = DEFAULT_KINC;
      unsigned timeout_seconds = 10;
      size_t max_memory_mib = 1024;
      lazy::Limits provider;
      Limits game;
  };
  std::string need_argument (int& i, int argc, char** argv) {
    if (++i >= argc)
      fail (std::string {argv[i - 1]} + " requires an argument");
    return argv[i];
  }
  template <typename T>
  T number (const std::string& s, const std::string& option) {
    T value {};
    const auto parsed = std::from_chars (s.data (), s.data () + s.size (), value);
    if (s.empty () || parsed.ec != std::errc {} || parsed.ptr != s.data () + s.size ())
      fail (option + " requires a non-negative integer in range");
    if constexpr (std::is_signed_v<T>)
      if (value < 0)
        fail (option + " requires a non-negative integer");
    return value;
  }
  void usage (std::ostream& out, const char* program) {
    out << "usage: " << program << " --arm c4|c5 --formula WORKER_LTL --k K\n"
        << "  [--partition uc...] [--kmax K --kinc N]\n"
        << "  [--timeout-seconds N] [--max-memory-mib N] [--max-states N]\n"
        << "  [--max-rows N] [--max-row-edges N] [--max-acceptance-sets N]\n"
        << "  [--max-rank-nodes N] [--max-choices N] [--max-expansions N]\n"
        << "  [--max-steps N] [--max-live-nodes N]\n"
        << "  [--verifier-max-rows N] [--verifier-max-steps N]\n"
        << "  Input is the already transformed bad-language formula supplied to\n"
        << "  create_automaton(), not a raw specification. No negation or TLSF adaptation.\n"
        << "  Partition is u=input/c=output in lexical AP-name order; default: all c.\n"
        << "  Exactly one formula/arm per process. TAA refined_rules=false in both.\n"
        << "  C4 freezes the exact P5 reachable row cache; C5 never enumerates it.\n"
        << "  Factory, acceptance setup and all query/row work count toward the caps.\n"
        << "  Defaults: 10 seconds, 1024 MiB address space, 200000 states/rows/ranks,\n"
        << "  2000000 edges per row/choices; query steps/live BDD nodes unlimited.\n"
        << "  --k defaults to one fixed-K attempt; --kmax uses the compiled worker\n"
        << "  schedule with fresh search/proofs/caches/strategy at each K.\n"
        << "  Counts ending in cumulative and provider row/state counts are job totals.\n"
        << "  search_rows_requested is the union across K; verification_additional_rows\n"
        << "  is certificate sources outside that union. Search-only is their set difference.\n"
        << "  Factory RSS/peak are process measurements, not allocation attribution.\n"
        << "  Rank bytes sum retained payload snapshots for nodes, proofs, interner,\n"
        << "  antichain, generators and query caches; allocator/map overhead is excluded.\n"
        << "  NA totals are censored/unknown; obtain C5's denominator from a separate C4 run.\n"
        << "  Exit 0: verified WIN_K; 2: inconclusive (including LOSE_K at Kmax);\n"
        << "  1: invalid invocation/input. LOSE_K never means LTL unrealizability.\n";
  }
  Options parse_options (int argc, char** argv) {
    Options o;
    std::set<std::string> seen;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--help") {
        usage (std::cout, argv[0]);
        std::exit (0);
      }
      if (!seen.insert (arg).second)
        fail ("duplicate option " + arg);
      auto next = [&] { return need_argument (i, argc, argv); };
      auto n = [&] { return number<size_t> (next (), arg); };
      if (arg == "--arm")
        o.arm = next ();
      else if (arg == "--formula")
        o.formula = next ();
      else if (arg == "--partition")
        o.partition = next ();
      else if (arg == "--k")
        o.k = number<int> (next (), arg);
      else if (arg == "--kmax")
        o.kmax = number<int> (next (), arg);
      else if (arg == "--kinc")
        o.kinc = number<int> (next (), arg);
      else if (arg == "--timeout-seconds")
        o.timeout_seconds = number<unsigned> (next (), arg);
      else if (arg == "--max-memory-mib")
        o.max_memory_mib = n ();
      else if (arg == "--max-states")
        o.provider.max_states = n ();
      else if (arg == "--max-rows")
        o.provider.rows.max_rows = n ();
      else if (arg == "--max-row-edges")
        o.provider.rows.max_edges_per_row = n ();
      else if (arg == "--max-acceptance-sets")
        o.provider.max_acceptance_sets = number<unsigned> (next (), arg);
      else if (arg == "--max-rank-nodes")
        o.game.max_rank_nodes = n ();
      else if (arg == "--max-choices")
        o.game.max_choices = n ();
      else if (arg == "--max-expansions")
        o.game.max_expansions = n ();
      else if (arg == "--max-steps")
        o.game.queries.max_steps = n ();
      else if (arg == "--max-live-nodes")
        o.provider.max_live_bdd_nodes = n ();
      else if (arg == "--verifier-max-rows")
        o.game.verifier_rows.max_rows = n ();
      else if (arg == "--verifier-max-steps")
        o.game.verifier_queries.max_steps = n ();
      else
        fail ("unknown option " + arg);
    }
    if (o.arm != "c4" && o.arm != "c5")
      fail ("--arm c4|c5 is required (one arm per invocation)");
    if (o.formula.empty () || o.k < 1)
      fail ("--formula and positive --k are required");
    if (!seen.contains ("--kmax"))
      o.kmax = o.k;
    if (o.kmax < o.k || o.kinc < 1 || o.kmax > std::numeric_limits<VECTOR_ELT_T>::max ())
      fail ("invalid K range/increment for the worker rank type");
    if (!o.timeout_seconds || !o.max_memory_mib ||
        o.max_memory_mib > std::numeric_limits<rlim_t>::max () / 1048576)
      fail ("time and memory caps must be positive and in range");
    o.game.rows = o.provider.rows;
    o.game.queries.max_live_nodes = o.provider.max_live_bdd_nodes;
    o.game.verifier_queries.max_live_nodes = o.provider.max_live_bdd_nodes;
    o.game.verifier_rows.max_edges_per_row = o.provider.rows.max_edges_per_row;
    if (!seen.contains ("--verifier-max-rows"))
      o.game.verifier_rows.max_rows = o.provider.rows.max_rows;
    if (!seen.contains ("--verifier-max-steps"))
      o.game.verifier_queries.max_steps = o.game.queries.max_steps;
    return o;
  }
  struct TimedStage {
      Reporter report;
      std::string name;
      Clock::time_point start = Clock::now ();
      TimedStage (Reporter r, std::string n, Clock::time_point job)
        : report (r),
          name (std::move (n)) {
        report.put ("stage", name);
        report.ms ("stage_started_ms", elapsed (job));
        report.ms ("stage_started_clock_ms", clock_ms ());
      }
      ~TimedStage () { report.ms (name + "_ms", elapsed (start)); }
  };
  letters::WorkerAlphabet alphabet (const std::shared_ptr<lazy::LazyBuchiView>& p,
                                    const Options& o, Reporter report) {
    std::vector<std::pair<std::string, int>> aps;
    for (auto ap : p->ap ())
      aps.emplace_back (ap.ap_name (), p->get_dict ()->varnum (ap));
    std::sort (aps.begin (), aps.end ());
    const std::string partition = o.partition.value_or (std::string (aps.size (), 'c'));
    if (partition.size () != aps.size ())
      throw InvalidInput ("--partition requires one u/c per lexical AP");
    letters::WorkerAlphabet a {p->ap_vars (), bddtrue, bddtrue, {}};
    std::string names;
    for (size_t i = 0; i < aps.size (); ++i) {
      auto [name, var] = aps[i];
      if (partition[i] != 'u' && partition[i] != 'c')
        throw InvalidInput ("partition accepts only u/c");
      (partition[i] == 'u' ? a.inputs : a.outputs) &= bdd_ithvar (var);
      a.order.push_back (var);
      // Length-prefix AP names: commas and other punctuation are unambiguous.
      names += std::to_string (name.size ()) + ':' + name;
    }
    report.put ("ap_order", names);
    report.put ("partition", partition);
    return a;
  }
  void rank_metrics (const SolveResult& r, Reporter report) {
    std::map<size_t, size_t> histogram;
    size_t entries = 0, bytes = 0, maximum = 0;
    for (const auto& n : r.nodes) {
      const auto count = n.rank.entries ().size ();
      ++histogram[count];
      entries += count;
      maximum = std::max (maximum, count);
      bytes += rank_bytes (n.rank);
    }
    std::string distribution;
    for (auto [size, count] : histogram)
      distribution += (distribution.empty () ? "" : ",") + std::to_string (size) + ':' +
                      std::to_string (count);
    report.put ("rank_support_distribution", distribution);
    report.count ("rank_support_sum", entries);
    report.count ("rank_support_max", maximum);
    report.count ("game_node_rank_bytes", bytes);
    size_t certificate_bytes = bytes;
    for (const auto& p : r.proofs)
      certificate_bytes += rank_bytes (p.rank);
    for (const auto& g : r.generators)
      certificate_bytes += rank_bytes (g);
    report.count ("certificate_rank_bytes", certificate_bytes);
    report.put ("rank_bytes_scope",
                "sum_of_retained_component_payload_snapshots_excludes_allocator_and_map_overhead");
    report.count ("game_states", r.nodes.size ());
    report.count ("guarded_choices", r.choices_created);
    report.count ("expansions", r.expansions);
    report.count ("losing_proofs", r.proofs.size ());
    report.count ("strategy_generators", r.generators.size ());
    report.count ("reopened_sources", r.reopened_sources);
    report.count ("reopen_enqueues", r.reopen_enqueues);
    report.count ("subsumption_scans", r.subsumption_scans);
    report.count ("subsumption_nodes_checked", r.subsumption_nodes_checked);
    report.count ("subsumption_nodes_invalidated", r.subsumption_nodes_invalidated);
    report.count ("subsumption_queries", r.subsumption_queries);
    report.count ("subsumption_hits", r.subsumption_hits);
    report.count ("losing_insertions", r.losing_insertions);
    report.count ("losing_removals", r.losing_removals);
    report.count ("losing_antichain_size", r.losing_antichain_size);
    report.count ("losing_antichain_peak", r.losing_antichain_peak);
  }
  int worker (const Options& o, Reporter report) {
    const auto job = Clock::now ();
    report.count ("worker_pid", size_t (getpid ()));
    report.put ("status", "UNKNOWN");
    report.put ("worker_result", "inconclusive");
    report.put ("total_status", o.arm == "c4" ? "censored" : "not_enumerated");
    report.put ("total_wrapper_rows", "NA");
    report.put ("total_underlying_rows", "NA");
    report.put ("total_wrapper_edges", "NA");
    report.put ("factory_measurement", "censored");
    report.count ("k", size_t (o.k));
    for (const auto* key :
         {"underlying_rows_requested", "underlying_rows_generated", "wrapper_rows_requested",
          "wrapper_rows_generated", "wrapper_edges_generated", "eager_rows_generated",
          "search_rows_requested", "search_only_rows_requested", "verification_rows_requested",
          "verification_additional_rows", "verification_rows_rebuilt",
          "search_rows_generated_cumulative", "verification_rows_generated_cumulative"})
      report.count (key, 0);
    report.ms ("row_generation_ms", 0);
    int exit_code = 2;
    try {
      spot::formula f;
      {
        TimedStage stage {report, "parse", job};
        auto parsed = spot::parse_infix_psl (o.formula);
        std::ostringstream error;
        if (parsed.format_errors (error))
          throw InvalidInput (error.str ());
        f = parsed.f;
        if (!f.is_ltl_formula ()) {
          report.put ("reason", "unsupported_non_ltl");
          report.put ("status", "DECLINED");
          return 2;
        }
        report.put ("worker_formula", spot::str_psl (f));
      }
      // This is the first Spot manager initialization in this process.
      const auto dict = spot::make_bdd_dict ();
      letters::BuddyErrors errors;
      spot::const_twa_ptr provider;
      report.count ("factory_baseline_peak_bytes", peak_bytes ());
      report.put ("factory_rss_before_bytes", rss_bytes ());
      {
        TimedStage stage {report, "factory", job};
        // Formula/TAA skeleton and any factory-internal factors are charged here.
        // There is deliberately NO translator/reference/preparation outside it.
        auto result = lazy::attempt ([&] () -> spot::const_twa_ptr {
          lazy::detail::bdd_budget (o.provider);
          auto p = spot::ltl_to_taa (f, dict, false);
          lazy::detail::bdd_budget (o.provider);
          return p;
        });
        report.count ("factory_peak_bytes", peak_bytes ());
        report.put ("factory_rss_after_bytes", rss_bytes ());
        report.put ("factory_measurement", result.value ? "complete" : "censored");
        if (!result.value) {
          if (result.error)
            std::rethrow_exception (result.error);
          throw lazy::ResourceLimit ("factory failed");
        }
        provider = *result.value;
      }
      std::shared_ptr<lazy::LazyBuchiView> view;
      {
        TimedStage stage {report, "acceptance_setup", job};
        view = std::make_shared<lazy::LazyBuchiView> (
            std::make_shared<ObservedProvider> (provider, report), o.provider);
        report.count ("acceptance_sets", provider->num_sets ());
        std::ostringstream acceptance;
        acceptance << provider->get_acceptance ();
        report.put ("provider_acceptance", acceptance.str ());
        report.count ("acceptance_setup_peak_bytes", peak_bytes ());
      }
      const auto a = alphabet (view, o, report);
      RowStore store {view, o.provider.rows, report};
      store.snapshot ();
      report.ms ("eager_ms", 0);
      if (o.arm == "c4") {
        TimedStage stage {report, "eager", job};
        store.enumerate_and_freeze ();
        report.put ("total_status", "complete");
        report.count ("total_wrapper_rows", store.cache->complete_rows ());
        report.count ("total_underlying_rows", view->underlying_rows ());
        report.count ("total_wrapper_edges", store.generated_edges);
      }
      // C5 reaches this point with precisely one discovered initial state and
      // zero generated rows. This assertion is live in release builds.
      else
        require (store.cache->state_count () == 1 && store.cache->complete_rows () == 0 &&
                 view->underlying_rows () == 0);
      for (long long k = o.k;;) {
        report.put ("stage", "starting_attempt");
        report.count ("k", size_t (k));
        report.put ("status", "UNKNOWN");
        report.put ("certificate", "unverified");
        report.put ("worker_result", "inconclusive");
        for (const auto* key :
             {"queries", "steps", "bdd_operations", "peak_live_nodes", "peak_result_nodes",
              "threshold_hits", "preimage_hits", "cache_rank_bytes"})
          for (const auto* prefix : {"search_", "verify_"})
            report.count (std::string (prefix) + key, 0);
        for (const auto* key :
             {"search_ms", "verification_ms", "attempt_ms", "game_states", "guarded_choices",
              "certificate_rank_bytes", "rank_interner_bytes", "losing_antichain_rank_bytes",
              "reopened_sources", "reopen_enqueues", "subsumption_scans",
              "subsumption_nodes_checked",
              "subsumption_nodes_invalidated", "subsumption_queries", "subsumption_hits",
              "losing_insertions", "losing_removals", "losing_antichain_size",
              "losing_antichain_peak"})
          report.put (key, "NA");
        const auto before_search = store.search_generated, before_verify = store.verify_generated;
        SolveResult result;
        {
          TimedStage stage {report, "attempt", job};
          report.put ("stage", "search");
          // Every K owns/destroys the rank interner, loss antichain/proofs,
          // threshold/preimage caches, work queues and guarded strategy.
          Search search {store, a, int32_t (k), o.game};
          result = search.solve ();
        }
        view->check_contract ();
        store.snapshot ();
        rank_metrics (result, report);
        report.count ("search_generated_rows", store.search_generated - before_search);
        report.count ("verification_generated_rows", store.verify_generated - before_verify);
        report.ms ("search_ms", result.solve_ms);
        report.ms ("verification_ms", result.verify_ms);
        report.ms ("end_to_end_ms", elapsed (job));
        report.count ("peak_rss_bytes", peak_bytes ());
        report.put ("status", solver_detail::forward_result_name (result.status));
        report.put ("reason", letters::unknown_name (result.failure));
        const bool win = result.status == forward_result_status::win_k;
        const bool loss = result.status == forward_result_status::lose_k;
        report.put ("certificate", win || loss ? "verified" : "unverified");
        const auto next =
            loss ? acacia::k_schedule::next (ACACIA_K_SCHEDULE, k, o.k, o.kmax, o.kinc,
                                             {static_cast<long long> (result.solve_ms),
                                              result.proofs.size (), result.expansions, true})
                 : std::nullopt;
        report.put ("worker_result", win ? "win" : next ? "retry" : "inconclusive");
        report.put ("stage", "attempt_complete");
        report.put ("emit", "1");
        if (!next)
          return win ? 0 : 2;
        k = *next;
      }
    } catch (const InvalidInput& e) {
      report.put ("reason", e.what ());
      exit_code = 1;
    } catch (const letters::detail::Failure& e) {
      report.put ("reason", letters::unknown_name (e.why));
    } catch (const lazy::Declined& e) {
      report.put ("status", "DECLINED");
      report.put ("reason", e.what ());
    } catch (const std::bad_alloc&) {
      report.put ("reason", "allocation_failure");
    } catch (const std::length_error& e) {
      report.put ("reason", e.what ());
    } catch (const std::exception& e) {
      report.put ("reason", e.what ());
    }
    report.ms ("end_to_end_ms", elapsed (job));
    report.count ("peak_rss_bytes", peak_bytes ());
    return exit_code;
  }

  const std::vector<std::string> columns {"arm",
                                          "worker_formula",
                                          "provider",
                                          "spot_version",
                                          "refined_rules",
                                          "wrapper",
                                          "initial_convention",
                                          "rank_domain",
                                          "partition",
                                          "ap_order",
                                          "k",
                                          "kmin",
                                          "kmax",
                                          "kinc",
                                          "k_schedule",
                                          "caps",
                                          "worker_pid",
                                          "status",
                                          "certificate",
                                          "worker_result",
                                          "reason",
                                          "stage",
                                          "factory_measurement",
                                          "factory_ms",
                                          "factory_baseline_peak_bytes",
                                          "factory_peak_bytes",
                                          "factory_rss_before_bytes",
                                          "factory_rss_after_bytes",
                                          "factory_scope",
                                          "acceptance_setup_ms",
                                          "acceptance_setup_peak_bytes",
                                          "acceptance_sets",
                                          "provider_acceptance",
                                          "underlying_rows_requested",
                                          "underlying_rows_generated",
                                          "wrapper_rows_requested",
                                          "wrapper_rows_generated",
                                          "underlying_states_discovered",
                                          "wrapper_states_discovered",
                                          "wrapper_edges_generated",
                                          "eager_rows_generated",
                                          "search_rows_requested",
                                          "search_only_rows_requested",
                                          "verification_rows_requested",
                                          "verification_additional_rows",
                                          "verification_rows_rebuilt",
                                          "search_generated_rows",
                                          "verification_generated_rows",
                                          "search_rows_generated_cumulative",
                                          "verification_rows_generated_cumulative",
                                          "row_generation_ms",
                                          "total_status",
                                          "total_underlying_rows",
                                          "total_wrapper_rows",
                                          "total_wrapper_edges",
                                          "rank_support_distribution",
                                          "rank_support_sum",
                                          "rank_support_max",
                                          "rank_bytes",
                                          "rank_bytes_scope",
                                          "game_node_rank_bytes",
                                          "certificate_rank_bytes",
                                          "rank_interner_bytes",
                                          "losing_antichain_rank_bytes",
                                          "search_cache_rank_bytes",
                                          "verify_cache_rank_bytes",
                                          "search_queries",
                                          "search_steps",
                                          "search_bdd_operations",
                                          "search_peak_live_nodes",
                                          "search_peak_result_nodes",
                                          "search_threshold_hits",
                                          "search_preimage_hits",
                                          "verify_queries",
                                          "verify_steps",
                                          "verify_bdd_operations",
                                          "verify_peak_live_nodes",
                                          "verify_peak_result_nodes",
                                          "verify_threshold_hits",
                                          "verify_preimage_hits",
                                          "game_states",
                                          "guarded_choices",
                                          "expansions",
                                          "losing_proofs",
                                          "strategy_generators",
                                          "reopened_sources",
                                          "reopen_enqueues",
                                          "subsumption_scans",
                                          "subsumption_nodes_checked",
                                          "subsumption_nodes_invalidated",
                                          "subsumption_queries",
                                          "subsumption_hits",
                                          "losing_insertions",
                                          "losing_removals",
                                          "losing_antichain_size",
                                          "losing_antichain_peak",
                                          "parse_ms",
                                          "eager_ms",
                                          "search_ms",
                                          "verification_ms",
                                          "attempt_ms",
                                          "end_to_end_ms",
                                          "process_wall_ms",
                                          "peak_rss_bytes",
                                          "measurement",
                                          "exit_code"};
  std::string value (const Fields& f, const std::string& k) {
    auto it = f.find (k);
    return it == f.end () ? "NA" : it->second;
  }
  void print (const Fields& f) {
    for (size_t i = 0; i < columns.size (); ++i)
      std::cout << (i ? "\t" : "") << value (f, columns[i]);
    std::cout << '\n';
  }
  std::pair<std::vector<Fields>, int> run (const Options& o) {
    // No formulas, providers or dictionaries exist in the parent before fork.
    int fds[2];
    if (pipe (fds) != 0)
      fail ("pipe failed");
    const auto started = Clock::now ();
    const pid_t pid = fork ();
    if (pid < 0) {
      close (fds[0]);
      close (fds[1]);
      fail ("fork failed");
    }
    if (!pid) {
      close (fds[0]);
      const rlim_t bytes = rlim_t (o.max_memory_mib) * 1048576;
      const rlimit memory {bytes, bytes}, core {0, 0};
      if (setrlimit (RLIMIT_AS, &memory) || setrlimit (RLIMIT_CORE, &core))
        std::_Exit (2);
      alarm (o.timeout_seconds);
      const int code = worker (o, pipe_reporter (fds[1]));
      close (fds[1]);
      std::_Exit (code);
    }
    close (fds[1]);
    Fields current;
    std::vector<Fields> attempts;
    std::string pending;
    char buffer[4096];
    for (;;) {
      const auto n = read (fds[0], buffer, sizeof buffer);
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        fail ("measurement pipe read failed");
      if (!n)
        break;
      pending.append (buffer, size_t (n));
      size_t newline;
      while ((newline = pending.find ('\n')) != std::string::npos) {
        const auto line = pending.substr (0, newline);
        pending.erase (0, newline + 1);
        const auto tab = line.find ('\t');
        if (tab == std::string::npos)
          fail ("malformed worker measurement");
        const auto key = line.substr (0, tab), val = line.substr (tab + 1);
        if (key == "emit")
          attempts.push_back (current);
        else
          current[key] = val;
      }
    }
    close (fds[0]);
    int status = 0;
    rusage usage {};
    while (wait4 (pid, &status, 0, &usage) < 0)
      if (errno != EINTR)
        fail ("wait4 failed");
    const int code = WIFEXITED (status) ? WEXITSTATUS (status) : 2;
    const bool died =
        !WIFEXITED (status) || (value (current, "worker_result") == "win" && code != 0);
    const bool partial = died || value (current, "stage") != "attempt_complete";
    if (partial) {
      if (died && value (current, "stage") == "attempt_complete" && !attempts.empty ())
        attempts.pop_back ();  // do not publish a verdict after fatal teardown
      current["status"] = value (current, "status") == "DECLINED" ? "DECLINED" : "UNKNOWN";
      current["certificate"] = "unverified";
      current["worker_result"] = "inconclusive";
      current["measurement"] = "censored";
      current["end_to_end_ms"] = decimal (elapsed (started));
      current["peak_rss_bytes"] = std::to_string (size_t (usage.ru_maxrss) * 1024);
      if (value (current, "row_generation_active") == "true") {
        // Charge an interrupted indivisible succ_iter too. Other counters in
        // a censored record are completed-work lower bounds, not final totals.
        const double previous = value (current, "row_generation_ms") == "NA"
                                    ? 0
                                    : std::stod (value (current, "row_generation_ms"));
        current["row_generation_ms"] =
            decimal (previous + clock_ms () - std::stod (value (current, "row_started_clock_ms")));
      }
      const auto stage = value (current, "stage");
      if (value (current, "stage_started_clock_ms") != "NA" &&
          (stage == "search" || stage == "verification" || stage == "eager"))
        current[stage + "_ms"] =
            decimal (clock_ms () - std::stod (value (current, "stage_started_clock_ms")));
      if (WIFSIGNALED (status))
        current["reason"] = "signal_" + std::to_string (WTERMSIG (status));
      else if (value (current, "reason") == "NA")
        current["reason"] = "worker_exit_" + std::to_string (code);
      if (value (current, "stage") == "factory") {
        current["factory_measurement"] = "censored";
        current["factory_peak_bytes"] = current["peak_rss_bytes"];
        if (value (current, "factory_ms") == "NA")
          current["factory_ms"] =
              decimal (elapsed (started) - std::stod (value (current, "stage_started_ms")));
      }
      // A partial eager traversal is a lower bound, never a denominator.
      if (value (current, "total_status") != "complete") {
        current["total_status"] = o.arm == "c4" ? "censored" : "not_enumerated";
        current["total_wrapper_rows"] = current["total_underlying_rows"] =
            current["total_wrapper_edges"] = "NA";
      }
      attempts.push_back (current);
    }
    if (!attempts.empty ()) {
      attempts.back ()["peak_rss_bytes"] = std::to_string (size_t (usage.ru_maxrss) * 1024);
      attempts.back ()["end_to_end_ms"] =
          decimal (elapsed (started));  // includes final provider teardown
    }
    for (auto& f : attempts) {
      f["arm"] = o.arm;
      f["provider"] = "ltl_to_taa";
      f["refined_rules"] = "false";
      f["spot_version"] = spot::version ();
      f["wrapper"] = "P5_single_cursor_one_obligation_per_edge_v1";
      f["initial_convention"] = "cursor=0,rank=0";
      f["rank_domain"] = "all_numeric_sparse";
      f["factory_scope"] =
          "TAA_formula_skeleton_including_all_internal_factor_setup_no_external_factors";
      f["kmin"] = std::to_string (o.k);
      f["kmax"] = std::to_string (o.kmax);
      f["kinc"] = std::to_string (o.kinc);
      f["k_schedule"] = acacia::k_schedule::name (ACACIA_K_SCHEDULE);
      std::ostringstream caps;
      caps << "time_s=" << o.timeout_seconds << ";memory_mib=" << o.max_memory_mib
           << ";states=" << o.provider.max_states << ";rows=" << o.provider.rows.max_rows
           << ";row_edges=" << o.provider.rows.max_edges_per_row
           << ";acceptance_sets=" << o.provider.max_acceptance_sets
           << ";live_bdd_nodes=" << o.provider.max_live_bdd_nodes
           << ";rank_nodes=" << o.game.max_rank_nodes << ";choices=" << o.game.max_choices
           << ";expansions=" << o.game.max_expansions
           << ";query_steps=" << o.game.queries.max_steps
           << ";verifier_rows=" << o.game.verifier_rows.max_rows
           << ";verifier_query_steps=" << o.game.verifier_queries.max_steps;
      f["caps"] = caps.str ();
      if (!f.contains ("measurement"))
        f["measurement"] = "complete";
      size_t bytes = 0;
      bool known = true;
      for (const auto* key :
           {"certificate_rank_bytes", "rank_interner_bytes", "losing_antichain_rank_bytes",
            "search_cache_rank_bytes", "verify_cache_rank_bytes"}) {
        if (value (f, key) == "NA")
          known = false;
        else
          bytes += number<size_t> (value (f, key), key);
      }
      f["rank_bytes"] = known ? std::to_string (bytes) : "NA";
      f["exit_code"] = std::to_string (code);
      f["process_wall_ms"] = decimal (elapsed (started));
    }
    return {std::move (attempts), code};
  }
}  // namespace replay

#ifndef ACACIA_PROVIDER_REPLAY_TESTING
int main (int argc, char** argv) {
  try {
    const auto options = replay::parse_options (argc, argv);
    const auto [rows, code] = replay::run (options);
    for (size_t i = 0; i < replay::columns.size (); ++i)
      std::cout << (i ? "\t" : "") << replay::columns[i];
    std::cout << '\n';
    for (const auto& row : rows)
      replay::print (row);
    return code;
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
#endif
