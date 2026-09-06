// P6 only. One invocation, one capped worker, one TAA factory, one arm.
// --formula is the captured bad-language WORKER formula (no extra negation).
// The sparse specialization of P3/P4 lives here; no production backend dispatch.
#include "solver/k_schedule.hh"
#include "solver/sparse_forward_rank.hh"
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
  namespace rows = acacia::spot_rows;
  namespace letters = acacia::spot_letters;
  namespace lazy = acacia::spot_lazy;
  namespace guarded = acacia::spot_guarded;
  namespace solver_detail = acacia::solver_detail;
  using Rank = rows::SparseForwardRank<>;
  size_t rank_bytes (const Rank& r) {
    return sizeof (Rank) + r.entries ().capacity () * sizeof (Rank::Entry);
  }
  using guarded::detail::require;
  using guarded::detail::row_ok;
  using letters::Unknown;
  using letters::Variables;
  using rows::StateId;
  using Clock = std::chrono::steady_clock;
  using Fields = std::map<std::string, std::string>;
  struct InvalidInput : std::runtime_error {
      using std::runtime_error::runtime_error;
  };
  double elapsed (Clock::time_point start) { return guarded::detail::elapsed (start); }
  double clock_ms () {
    return std::chrono::duration<double, std::milli> (Clock::now ().time_since_epoch ()).count ();
  }

  [[noreturn]] void fail (const std::string& s) {
    throw std::runtime_error ("acacia-spot-provider-replay: " + s);
  }
  std::string cell (std::string s) {
    for (char& c : s)
      if (c == '\t' || c == '\n' || c == '\r')
        c = ' ';
    return s;
  }
  std::string decimal (double n) {
    std::ostringstream s;
    s << std::fixed << std::setprecision (6) << n;
    return s.str ();
  }
  size_t peak_bytes () {
    rusage r {};
    getrusage (RUSAGE_SELF, &r);
    return size_t (r.ru_maxrss) * 1024;
  }
  std::string rss_bytes () {
    std::ifstream f {"/proc/self/statm"};
    size_t total, resident;
    if (!(f >> total >> resident))
      return "NA";
    return std::to_string (resident * size_t (sysconf (_SC_PAGESIZE)));
  }
  // Incremental measurements survive bad_alloc, BuDDy's _Exit and alarm death.
  // The parent has never initialized Spot and never constructs a reference.
  struct Reporter {
      int fd;
      void put (const std::string& k, const std::string& v) const {
        const auto s = k + '\t' + cell (v) + '\n';
        size_t offset = 0;
        while (offset < s.size ()) {
          const auto n = write (fd, s.data () + offset, s.size () - offset);
          if (n < 0 && errno == EINTR)
            continue;
          if (n <= 0)
            std::_Exit (2);
          offset += size_t (n);
        }
      }
      void count (const std::string& k, size_t v) const { put (k, std::to_string (v)); }
      void ms (const std::string& k, double v) const { put (k, decimal (v)); }
  };

  enum class Phase { eager, search, verify };
  // Instrument the public underlying iterator boundary, including a request
  // that fails inside TAA before returning its indivisibly constructed row.
  class ObservedProvider final : public spot::twa {
      spot::const_twa_ptr provider_;
      Reporter report_;
      mutable size_t requests_ = 0;
      class Iterator final : public spot::twa_succ_iterator {
          spot::const_twa_ptr provider_;
          spot::twa_succ_iterator* inner_;

        public:
          Iterator (spot::const_twa_ptr p, spot::twa_succ_iterator* i)
            : provider_ (std::move (p)),
              inner_ (i) {}
          ~Iterator () override {
            if (inner_)
              provider_->release_iter (inner_);
          }
          bool first () override { return inner_->first (); }
          bool next () override { return inner_->next (); }
          bool done () const override { return inner_->done (); }
          bdd cond () const override { return inner_->cond (); }
          spot::acc_cond::mark_t acc () const override { return inner_->acc (); }
          const spot::state* dst () const override { return inner_->dst (); }
      };

    public:
      ObservedProvider (spot::const_twa_ptr p, Reporter r)
        : twa (p->get_dict ()),
          provider_ (std::move (p)),
          report_ (r) {
        set_acceptance (provider_->num_sets (), provider_->get_acceptance ());
        copy_ap_of (provider_);
        prop_state_acc (false);
      }
      size_t requests () const { return requests_; }
      const spot::state* get_init_state () const override { return provider_->get_init_state (); }
      spot::twa_succ_iterator* succ_iter (const spot::state* s) const override {
        ++requests_;
        report_.count ("underlying_rows_requested", requests_);
        // Keep ownership if allocating the forwarding iterator itself fails.
        struct Release {
            const spot::twa* p;
            void operator() (spot::twa_succ_iterator* i) const { p->release_iter (i); }
        };
        std::unique_ptr<spot::twa_succ_iterator, Release> i {provider_->succ_iter (s),
                                                             {provider_.get ()}};
        if (!i)
          fail ("TAA returned a null iterator");
        auto result = std::make_unique<Iterator> (provider_, i.get ());
        i.release ();
        return result.release ();
      }
      std::string format_state (const spot::state*) const override {
        fail ("provider display is disabled");
      }
  };
  // Exact immutable graph storage is P2's complete row cache, NOT an optimized
  // twa_graph copy. Freeze checks closure and makes any subsequent miss fatal.
  // Both arms have the identical storage; only C4 calls enumerate_and_freeze().
  class RowStore {
    public:
      std::shared_ptr<lazy::LazyBuchiView> view;
      std::shared_ptr<rows::SpotRows> cache;
      Reporter report;
      Phase phase = Phase::eager;
      std::set<StateId> search_sources, verifier_sources;
      size_t requests = 0, generated_edges = 0, verifier_rebuilt = 0;
      size_t eager_generated = 0, search_generated = 0, verify_generated = 0;
      double generation_ms = 0;
      bool frozen = false;

      RowStore (std::shared_ptr<lazy::LazyBuchiView> v, rows::RowLimits l, Reporter r)
        : view (std::move (v)),
          cache (std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {view}, l)),
          report (r) {
        discover ();
      }

      const rows::CompleteRankRow& get (StateId source) {
        view->check_contract ();
        if (phase == Phase::search)
          search_sources.insert (source);
        if (phase == Phase::verify)
          verifier_sources.insert (source);
        const bool missing = cache->state (source) != rows::RowState::complete;
        if (missing && frozen)
          fail ("frozen graph row miss");
        const auto started = Clock::now ();
        if (missing) {
          ++requests;
          snapshot ();
          report.ms ("row_started_clock_ms", clock_ms ());
          report.put ("row_generation_active", "true");
        }
        const auto row = cache->row (source);
        if (missing)
          generation_ms += elapsed (started);
        if (missing)
          report.put ("row_generation_active", "false");
        discover ();
        if (missing && row.row) {
          generated_edges += row.row->edges.size ();
          ++(phase == Phase::eager    ? eager_generated
             : phase == Phase::search ? search_generated
                                      : verify_generated);
        }
        if (missing)
          snapshot ();
        if (row.status != rows::Status::complete || !row.row)
          throw letters::detail::Failure {row.status == rows::Status::resource_limit
                                              ? Unknown::resource_limit
                                              : Unknown::row_failure,
                                          row.error};
        return *row.row;
      }
      void enumerate_and_freeze () {
        for (size_t q = 0; q < cache->state_count (); ++q)
          get (StateId (q));
        require (cache->complete_rows () == cache->state_count ());
        frozen = true;
      }
      void snapshot () const {
        // O(1) arena counters, never an automaton traversal or display API.
        const auto* init = dynamic_cast<const lazy::detail::CursorState*> (
            cache->canonical_state (cache->initial_id ()));
        require (init != nullptr);
        report.count ("underlying_rows_generated", view->underlying_rows ());
        const auto* observed =
            dynamic_cast<const ObservedProvider*> (init->context->provider.get ());
        report.count ("underlying_rows_requested", observed ? observed->requests () : 0);
        report.count ("wrapper_rows_requested", requests);
        report.count ("wrapper_rows_generated", cache->complete_rows ());
        report.count ("underlying_states_discovered", init->context->ids.size ());
        report.count ("wrapper_states_discovered", cache->state_count ());
        report.count ("wrapper_edges_generated", generated_edges);
        report.count ("search_rows_requested", search_sources.size ());
        size_t additional = 0, search_only = 0;
        for (auto q : verifier_sources)
          additional += !search_sources.contains (q);
        for (auto q : search_sources)
          search_only += !verifier_sources.contains (q);
        report.count ("search_only_rows_requested", search_only);
        report.count ("verification_additional_rows", additional);
        report.count ("verification_rows_requested", verifier_sources.size ());
        report.count ("verification_rows_rebuilt", verifier_rebuilt);
        report.count ("eager_rows_generated", eager_generated);
        report.count ("search_rows_generated_cumulative", search_generated);
        report.count ("verification_rows_generated_cumulative", verify_generated);
        report.ms ("row_generation_ms", generation_ms);
      }
      StateId id (const spot::state* state) const {
        const auto found = ids_.find (state);
        if (found == ids_.end ())
          fail ("missing construction correspondence");
        return found->second;
      }

    private:
      spot::state_map<StateId> ids_;  // borrowed canonical states outlive this map
      void discover () {
        for (size_t q = ids_.size (); q < cache->state_count (); ++q)
          ids_.emplace (cache->canonical_state (StateId (q)), StateId (q));
      }
  };

  // Rebuild verifier P2 rows from the immutable provider data. Each root is an
  // active certificate source; this never walks paths to seed integer IDs.
  // Returned states retain P5 construction identity, so local P2 numbering is
  // remapped with Spot hash/compare through RowStore::id(), not display text.
  class CertificateSource final : public spot::twa {
      RowStore& store_;
      StateId root_;
      class Iterator final : public spot::twa_succ_iterator {
          RowStore& store_;
          const rows::CompleteRankRow& row_;
          size_t i_ = 0;

        public:
          Iterator (RowStore& s, const rows::CompleteRankRow& r) : store_ (s), row_ (r) {}
          bool first () override {
            i_ = 0;
            return !done ();
          }
          bool next () override {
            ++i_;
            return !done ();
          }
          bool done () const override { return i_ == row_.spot_edges.size (); }
          bdd cond () const override { return row_.spot_edges.at (i_).condition; }
          spot::acc_cond::mark_t acc () const override {
            return row_.spot_edges.at (i_).acceptance;
          }
          const spot::state* dst () const override {
            return store_.cache->canonical_state (row_.spot_edges.at (i_).destination)->clone ();
          }
      };

    public:
      CertificateSource (RowStore& s, StateId root)
        : twa (s.view->get_dict ()),
          store_ (s),
          root_ (root) {
        set_buchi ();
        prop_state_acc (false);
        copy_ap_of (s.view);
      }
      const spot::state* get_init_state () const override {
        return store_.cache->canonical_state (root_)->clone ();
      }
      spot::twa_succ_iterator* succ_iter (const spot::state* s) const override {
        return new Iterator (store_, store_.get (store_.id (s)));
      }
      std::string format_state (const spot::state*) const override {
        fail ("provider display is disabled in measured replay");
      }
  };

  class Reader {
      RowStore& store_;
      bool verifier_;
      rows::RowLimits limits_;
      std::map<StateId, rows::CompleteRankRow> rebuilt_;

    public:
      Reader (RowStore& s, bool v, rows::RowLimits l) : store_ (s), verifier_ (v), limits_ (l) {}
      Rank initial_rank () const { return Rank {{{store_.cache->initial_id (), 0}}, 1}; }
      bool is_safe (const Rank& r, int K) const { return r.is_safe (K); }
      const rows::CompleteRankRow& row (StateId q) {
        if (!verifier_)
          return store_.get (q);
        if (auto it = rebuilt_.find (q); it != rebuilt_.end ())
          return it->second;
        if (rebuilt_.size () >= limits_.max_rows)
          throw letters::detail::Failure {Unknown::resource_limit};
        auto provider = std::make_shared<CertificateSource> (store_, q);
        rows::SpotRows fresh {rows::GenericTransitionBuchi {provider}, limits_};
        const auto result = fresh.row (fresh.initial_id ());
        row_ok (result.status);
        require (result.row != nullptr);
        rows::CompleteRankRow normalized;
        for (const auto& e : result.row->spot_edges) {
          const auto dst = store_.id (fresh.canonical_state (e.destination));
          const bool inc = e.acceptance.has (0);
          normalized.spot_edges.push_back ({dst, e.condition, e.acceptance});
          normalized.edges.push_back ({dst, e.condition, inc});
          for (auto v : {rows::RowDigest (dst), rows::RowDigest (e.condition.id ()),
                         rows::RowDigest (inc)}) {
            normalized.digest ^= v;
            normalized.digest *= 1099511628211ULL;
          }
        }
        ++store_.verifier_rebuilt;
        return rebuilt_.emplace (q, std::move (normalized)).first->second;
      }
  };

  // P3 predicates specialized to P5's sparse keys. The BDD boundary, budgets,
  // deterministic valuation picker and quantifiers are the existing P3 ones.
  // D(r) union support(target), never the discovered arena, defines a query.
  class Oracle {
      struct Prepared {
          std::map<StateId, std::vector<std::pair<int64_t, bdd>>> destinations;
          std::map<std::pair<StateId, int64_t>, bdd> thresholds;
          std::map<std::pair<int, std::vector<Rank::Entry>>, bdd> preimages;
      };
      Reader& rows_;
      letters::Oracle boundary_;
      int K_;
      std::unordered_map<Rank, Prepared> prepared_;
      size_t queries_ = 0, threshold_hits_ = 0, preimage_hits_ = 0;
      Prepared& prepare (const Rank& r, letters::detail::Letters& b) {
        (void) r.is_safe (K_);
        if (auto it = prepared_.find (r); it != prepared_.end ())
          return it->second;
        std::vector<std::pair<int, const rows::CompleteRankRow*>> active;
        for (auto [q, value] : r.entries ()) {
          b.step ();
          active.emplace_back (value, &rows_.row (q));
        }
        Prepared p;
        for (auto [value, row] : active)
          for (const auto& e : row->edges) {
            b.step ();
            b.require_support (e.condition, boundary_.alphabet ().ap_vars);
            p.destinations[e.destination].emplace_back (int64_t (value) + int (e.increment),
                                                        e.condition);
          }
        return prepared_.emplace (r, std::move (p)).first->second;
      }
      bdd threshold (Prepared& p, StateId q, int64_t h, letters::detail::Letters& b) {
        b.step ();
        if (h <= -1)
          return bddtrue;
        if (h > K_)
          return bddfalse;
        if (auto it = p.thresholds.find ({q, h}); it != p.thresholds.end ()) {
          ++threshold_hits_;
          return it->second;
        }
        bdd result = bddfalse;
        if (auto it = p.destinations.find (q); it != p.destinations.end ())
          for (const auto& [level, guard] : it->second) {
            b.step ();
            if (level >= h)
              result = b.lor (result, guard);
          }
        b.nodes (result);
        p.thresholds.emplace (std::make_pair (q, h), result);
        return result;
      }
      // kind: 0 upward, 1 downward, 2 equality (P3's exact same boundaries).
      bdd preimage (Prepared& p, const Rank& target, int kind, letters::detail::Letters& b) {
        (void) target.is_safe (K_);
        const auto key = std::make_pair (kind, target.entries ());
        if (auto it = p.preimages.find (key); it != p.preimages.end ()) {
          ++preimage_hits_;
          return it->second;
        }
        std::set<StateId> coordinates;
        for (auto [q, v] : target.entries ()) {
          (void) v;
          b.step ();
          coordinates.insert (q);
        }
        if (kind != 0)
          for (const auto& [q, edges] : p.destinations) {
            (void) edges;
            b.step ();
            coordinates.insert (q);
          }
        bdd result = bddtrue;
        for (auto q : coordinates) {
          const int64_t v = target.at (q);
          bdd term;
          if (kind == 0)
            term = threshold (p, q, v, b);
          else if (kind == 1 || v == -1)
            term = b.negate (threshold (p, q, v + 1, b));
          else if (v == K_)
            term = threshold (p, q, K_, b);
          else
            term = b.land (threshold (p, q, v, b), b.negate (threshold (p, q, v + 1, b)));
          result = b.land (result, term);
        }
        b.nodes (result);
        p.preimages.emplace (key, result);
        return result;
      }
      bdd aggregate (const Rank& r, const std::vector<Rank>& targets, bool bad,
                     letters::detail::Letters& b) {
        auto& p = prepare (r, b);
        bdd result = bddfalse;
        if (bad)
          for (const auto& [q, edges] : p.destinations) {
            (void) edges;
            result = b.lor (result, threshold (p, q, K_, b));
          }
        for (const auto& target : targets) {
          b.step ();
          result = b.lor (result, preimage (p, target, bad ? 0 : 1, b));
        }
        b.nodes (result);
        return result;
      }

    public:
      Oracle (Reader& r, RowStore& store, letters::WorkerAlphabet a, int K)
        : rows_ (r),
          boundary_ (store.cache, std::move (a), K),
          K_ (K) {}
      void set_limits (letters::QueryLimits l) { boundary_.set_limits (l); }
      template <typename T, typename F>
      letters::Result<T> query (F&& f) {
        ++queries_;
        auto result = boundary_.query<T> (std::forward<F> (f));
        if (!result.value)
          prepared_.clear ();
        return result;
      }
      letters::Result<bdd> eq (const Rank& r, const Rank& s) {
        return query<bdd> ([&] (auto& b) { return preimage (prepare (r, b), s, 2, b); });
      }
      letters::Result<bdd> bad (const Rank& r, const std::vector<Rank>& L, uint64_t) {
        return query<bdd> ([&] (auto& b) { return aggregate (r, L, true, b); });
      }
      letters::Result<letters::Invariant> invariant (const Rank& initial,
                                                     const std::vector<Rank>& G, uint64_t) {
        return query<letters::Invariant> ([&] (auto& b) {
          bool covered = false;
          for (const auto& g : G) {
            b.step ();
            if (!g.is_safe (K_))
              return letters::Invariant::rejected;
            covered |= initial.leq (g);
          }
          if (!covered)
            return letters::Invariant::rejected;
          for (const auto& g : G)
            if (!b.tautology (
                    b.forall (b.exists (aggregate (g, G, false, b), boundary_.alphabet ().outputs),
                              boundary_.alphabet ().inputs)))
              return letters::Invariant::rejected;
          return letters::Invariant::verified;
        });
      }
      letters::Result<Rank> evaluate (const Rank& r, bdd valuation) {
        return query<Rank> ([&] (auto& b) {
          (void) b.restrict_total (bddtrue, valuation, Variables::all);
          auto& p = prepare (r, b);
          std::vector<Rank::Entry> result;
          for (const auto& [q, edges] : p.destinations)
            for (const auto& [level, guard] : edges)
              if (b.land (valuation, guard) != bddfalse)
                result.emplace_back (q, int32_t (std::min (int64_t (K_), level)));
          return Rank {std::move (result), K_};
        });
      }
      letters::Result<bdd> restrict_total (bdd f, bdd v, Variables vars) {
        return query<bdd> ([&] (auto& b) { return b.restrict_total (f, v, vars); });
      }
      letters::Result<std::optional<bdd>> model (bdd f, Variables vars) {
        return query<std::optional<bdd>> ([&] (auto& b) { return b.model (f, vars); });
      }
      static bool leq (const Rank& a, const Rank& b) { return a.leq (b); }
      void report (Reporter out, const std::string& prefix) const {
        const auto& m = boundary_.metrics ();
        out.count (prefix + "queries", queries_);
        out.count (prefix + "steps", m.steps);
        out.count (prefix + "bdd_operations", m.bdd_operations);
        out.count (prefix + "peak_live_nodes", m.peak_live_nodes);
        out.count (prefix + "peak_result_nodes", m.peak_result_nodes);
        out.count (prefix + "threshold_hits", threshold_hits_);
        out.count (prefix + "preimage_hits", preimage_hits_);
        size_t bytes = 0;
        for (const auto& [rank, p] : prepared_) {
          bytes += rank_bytes (rank);
          for (const auto& [key, predicate] : p.preimages) {
            (void) predicate;
            bytes += sizeof (key.second) + key.second.capacity () * sizeof (Rank::Entry);
          }
        }
        out.count (prefix + "cache_rank_bytes", bytes);
      }
  };
}  // namespace replay

namespace replay {
  // Sparse upward antichain with P4's exact order; no dense coordinate-sum
  // prefilter (its implicit -1 tail would depend on the discovered arena).
  class LossSet {
      std::vector<Rank> generators_;

    public:
      bool subsumes (const Rank& r) const {
        for (const auto& g : generators_)
          if (g.leq (r))
            return true;
        return false;
      }
      bool insert (const Rank& r) {
        if (subsumes (r))
          return false;
        std::erase_if (generators_, [&] (const Rank& g) { return r.leq (g); });
        generators_.push_back (r);
        return true;
      }
      size_t bytes () const {
        size_t result = 0;
        for (const auto& r : generators_)
          result += rank_bytes (r);
        return result;
      }
  };
  struct MetricReport {
      Oracle& oracle;
      Reporter out;
      std::string prefix;
      ~MetricReport () { oracle.report (out, prefix); }
  };
  using solver_detail::forward_result_status;
  using solver_detail::losing_reason;
  // P4 OTFUR and verifier specialization: same queue order, loss propagation,
  // exact guarded choices, proof chronology and inductive certificate checks.
  using guarded::ChoiceRef;
  using guarded::GuardedChoice;
  using guarded::RankNodeId;
  using guarded::RowIdentity;
  struct GuardedRankNode {
      Rank rank;
      bool active_rows_complete = false;
      bool losing = false;
      std::vector<GuardedChoice> choices;
      bdd covered_inputs = bddfalse;
      std::vector<ChoiceRef> incoming;
      bool queued = false;
  };
  struct GuardedLosingProof {
      solver_detail::losing_proof record;
      Rank rank;
      std::optional<bdd> input;
      // Row-independent UNSAFE/SUBSUMPTION rules have no row obligations.
      std::vector<RowIdentity> rows;
  };
  using Limits = guarded::Limits;
  struct SolveResult {
      // Certificates contain BDD guards; retain their AP registrations until
      // after every guard is destroyed (members are destroyed in reverse).
      spot::const_twa_ptr provider;
      forward_result_status status = forward_result_status::unknown;
      Unknown failure = Unknown::none;
      RankNodeId initial = 0;
      std::vector<GuardedRankNode> nodes;
      std::vector<GuardedLosingProof> proofs;
      std::optional<std::size_t> initial_proof;
      bool pending_loss = false;
      bool pending_expansion = false;
      std::vector<Rank> generators;
      std::size_t expansions = 0, choices_created = 0, reopened_sources = 0;
      double prep_ms = 0, solve_ms = 0, verify_ms = 0;
  };

  namespace detail {
    using guarded::detail::checked;
    using guarded::detail::elapsed;
    using guarded::detail::Failure;
    using guarded::detail::require;
    using guarded::detail::row_ok;
    using guarded::detail::take;
    inline std::vector<RowIdentity> complete_rows (Reader& rows, Oracle& oracle,
                                                   const Rank& rank) {
      return take (oracle.query<std::vector<RowIdentity>> ([&] (auto& b) {
        std::vector<RowIdentity> result;
        for (const auto& [p, value] : rank.entries ()) {
          (void) value;
          b.step ();
          const auto& row = rows.row (p);
          result.push_back ({p, row.digest});
        }
        return result;
      }));
    }
  }

  // P4 certificate rules, with fresh P2 normalization via construction/state
  // correspondence and a fresh sparse P3 oracle. Only immutable provider rows
  // are shared: search flags, predicates and strategy selections cannot certify
  // themselves. The verifier may generate additional active provider rows.
  inline letters::Result<std::vector<Rank>> verify_winning_certificate (
      RowStore& view, const letters::WorkerAlphabet& alphabet, std::int32_t K,
      const SolveResult& certificate, const Limits& limits = {}) {
    return detail::checked<std::vector<Rank>> ([&] {
      auto rows = std::make_shared<Reader> (view, true, limits.verifier_rows);
      Oracle oracle {*rows, view, alphabet, K};
      MetricReport metrics {oracle, view.report, "verify_"};
      oracle.set_limits (limits.verifier_queries);
      detail::require (certificate.failure == Unknown::none && not certificate.pending_loss &&
                       not certificate.pending_expansion &&
                       certificate.initial < certificate.nodes.size ());
      detail::require (certificate.nodes[certificate.initial].rank == rows->initial_rank ());
      std::vector<bool> seen (certificate.nodes.size (), false);
      solver_detail::forward_work_queue<RankNodeId> todo {certificate.initial};
      std::vector<Rank> reached;
      while (not todo.empty ()) {
        const auto id = todo.front ();
        todo.pop_front ();
        if (seen[id])
          continue;
        seen[id] = true;
        const auto& node = certificate.nodes[id];
        detail::require (not node.losing && not node.queued && node.active_rows_complete &&
                         rows->is_safe (node.rank, K));
        (void) detail::complete_rows (*rows, oracle, node.rank);
        bdd covered = bddfalse;
        for (const auto& choice : node.choices) {
          if (not choice.active)
            continue;
          detail::require (choice.successor < certificate.nodes.size ());
          const auto& target = certificate.nodes[choice.successor];
          detail::require (not target.losing && rows->is_safe (target.rank, K));
          const bdd eq = detail::take (oracle.eq (node.rank, target.rank));
          const bdd exact =
              detail::take (oracle.restrict_total (eq, choice.output, Variables::outputs));
          covered = detail::take (oracle.query<bdd> ([&] (auto& b) {
            b.require_support (choice.input_region, alphabet.inputs);
            detail::require (b.satisfiable (choice.input_region));
            detail::require (b.land (choice.input_region, b.negate (exact)) == bddfalse);
            return b.lor (covered, choice.input_region);
          }));
          todo.push_back (choice.successor);
        }
        detail::require (covered == bddtrue && covered == node.covered_inputs);
        reached.push_back (node.rank);
      }
      auto generators = detail::take (oracle.query<std::vector<Rank>> ([&] (auto& b) {
        std::vector<Rank> result;
        for (const auto& r : reached) {
          bool dominated = false;
          for (const auto& g : result) {
            b.step ();
            dominated |= Oracle::leq (r, g);
          }
          if (dominated)
            continue;
          std::erase_if (result, [&] (const auto& g) {
            b.step ();
            return Oracle::leq (g, r);
          });
          result.push_back (r);
        }
        return result;
      }));
      // A second obligation: forall u exists c Good at EVERY maximal generator.
      detail::require (detail::take (oracle.invariant (rows->initial_rank (), generators, 0)) ==
                       letters::Invariant::verified);
      return generators;
    });
  }

  inline letters::Result<bool> verify_losing_proof (RowStore& view,
                                                    const letters::WorkerAlphabet& alphabet,
                                                    std::int32_t K, const SolveResult& certificate,
                                                    const Limits& limits = {}) {
    return detail::checked<bool> ([&] {
      auto rows = std::make_shared<Reader> (view, true, limits.verifier_rows);
      Oracle oracle {*rows, view, alphabet, K};
      MetricReport metrics {oracle, view.report, "verify_"};
      oracle.set_limits (limits.verifier_queries);
      detail::require (certificate.failure == Unknown::none && not certificate.pending_loss &&
                       certificate.initial_proof &&
                       *certificate.initial_proof < certificate.proofs.size ());
      for (std::size_t id = 0; id < certificate.proofs.size (); ++id) {
        const auto& proof = certificate.proofs[id];
        const auto& record = proof.record;
        detail::take (oracle.query<bool> ([&] (auto& b) {
          detail::require (record.id == id && record.node < certificate.nodes.size () &&
                           proof.rank == certificate.nodes[record.node].rank);
          for (const auto dep : record.dependencies) {
            b.step ();
            detail::require (dep < id);  // chronological IDs prove acyclicity
          }
          return true;
        }));
        const bool safe =
            rows->is_safe (proof.rank, K);  // validates the all-numeric sparse domain
        switch (record.reason) {
          case losing_reason::env_unsafe:
            detail::require (not safe && record.dependencies.empty () && proof.rows.empty () &&
                             not proof.input);
            break;
          case losing_reason::env_subsumed:
            detail::require (record.dependencies.size () == 1 && proof.rows.empty () &&
                             not proof.input);
            detail::require (
                Oracle::leq (certificate.proofs[record.dependencies[0]].rank, proof.rank));
            break;
          case losing_reason::env_losing_input: {
            detail::require (safe && proof.input.has_value ());
            detail::require (detail::complete_rows (*rows, oracle, proof.rank) == proof.rows);
            std::vector<Rank> earlier;
            for (const auto dep : record.dependencies)
              earlier.push_back (certificate.proofs[dep].rank);
            const bdd bad = detail::take (oracle.bad (proof.rank, earlier, id));
            const bdd all_outputs =
                detail::take (oracle.restrict_total (bad, *proof.input, Variables::inputs));
            detail::require (all_outputs == bddtrue);
            break;
          }
          default: detail::require (false);
        }
      }
      const auto& root = certificate.proofs[*certificate.initial_proof];
      detail::require (root.record.node == certificate.initial &&
                       root.rank == rows->initial_rank ());
      return true;
    });
  }

  class Search {
    public:
      Search (RowStore& view, letters::WorkerAlphabet alphabet, std::int32_t K, Limits limits = {})
        : view_ (view),
          alphabet_ (std::move (alphabet)),
          K_ (K),
          limits_ (limits),
          rows_ (std::make_shared<Reader> (view_, false, limits.rows)),
          oracle_ (*rows_, view_, alphabet_, K) {
        result_.provider = view_.view;
        oracle_.set_limits (limits_.queries);
      }
      ~Search () {
        size_t bytes = 0;
        for (const auto& [rank, id] : interned_) {
          (void) id;
          bytes += rank_bytes (rank);
        }
        view_.report.count ("rank_interner_bytes", bytes);
        view_.report.count ("losing_antichain_rank_bytes", losing_.bytes ());
      }
      SolveResult solve () {
        view_.phase = Phase::search;
        view_.report.ms ("stage_started_clock_ms", clock_ms ());
        MetricReport metrics {oracle_, view_.report, "search_"};
        const auto started = std::chrono::steady_clock::now ();
        const auto search = detail::checked<bool> ([&] {
          result_.initial = intern (rows_->initial_rank ());
          for (;;) {
            propagate_losses ();
            if (result_.nodes[result_.initial].losing || open_.empty ())
              return true;
            if (result_.expansions >= limits_.max_expansions)
              throw detail::Failure {Unknown::resource_limit};
            const auto id = open_.front ();
            open_.pop_front ();
            result_.nodes[id].queued = false;
            ++result_.expansions;
            expand (id);
          }
        });
        result_.solve_ms = detail::elapsed (started);
        result_.pending_loss = not losses_.empty ();
        result_.pending_expansion = not open_.empty ();
        if (not search.value) {
          result_.failure = search.unknown;
          result_.status = search.unknown == Unknown::resource_limit
                               ? forward_result_status::resource_limit
                               : forward_result_status::unknown;
          return std::move (result_);
        }
        view_.snapshot ();
        view_.report.count ("search_generated_rows",
                            view_.cache->complete_rows () - before_search_);
        oracle_.report (view_.report, "search_");
        view_.phase = Phase::verify;
        view_.report.put ("stage", "verification");
        view_.report.ms ("stage_started_clock_ms", clock_ms ());
        const auto verifying = std::chrono::steady_clock::now ();
        if (result_.nodes[result_.initial].losing) {
          const auto verified = verify_losing_proof (view_, alphabet_, K_, result_, limits_);
          result_.status = verified.value && *verified.value ? forward_result_status::lose_k
                                                             : forward_result_status::unknown;
          result_.failure = verified.unknown;
        }
        else {
          auto verified = verify_winning_certificate (view_, alphabet_, K_, result_, limits_);
          if (verified.value) {
            result_.generators = std::move (*verified.value);
            result_.status = forward_result_status::win_k;
          }
          else {
            result_.status =
                forward_result_status::unknown;  // verifier resource failure is UNKNOWN
            result_.failure = verified.unknown;
          }
        }
        result_.verify_ms = detail::elapsed (verifying);
        return std::move (result_);
      }

    private:
      RowStore& view_;
      letters::WorkerAlphabet alphabet_;
      std::int32_t K_;
      Limits limits_;
      std::shared_ptr<Reader> rows_;
      Oracle oracle_;
      SolveResult result_;
      std::unordered_map<Rank, RankNodeId> interned_;
      size_t before_search_ = view_.cache->complete_rows ();
      solver_detail::forward_work_queue<RankNodeId> open_, losses_;
      LossSet losing_;
      // Only these generator IDs are reduced; immutable proofs are never erased.
      std::vector<std::size_t> generators_;

      void enqueue (RankNodeId id) {
        auto& node = result_.nodes[id];
        if (not node.losing && not node.queued) {
          open_.push_back (id);
          node.queued = true;
        }
      }
      RankNodeId intern (Rank rank) {
        const auto found = interned_.find (rank);
        if (found != interned_.end ())
          return found->second;
        if (result_.nodes.size () >= limits_.max_rank_nodes)
          throw detail::Failure {Unknown::resource_limit};
        const auto id = result_.nodes.size ();
        interned_.emplace (rank, id);
        result_.nodes.push_back (GuardedRankNode {std::move (rank)});
        enqueue (id);
        return id;
      }
      std::vector<Rank> losing_ranks () const {
        std::vector<Rank> ranks;
        for (const auto id : generators_)
          ranks.push_back (result_.proofs[id].rank);
        return ranks;
      }
      std::optional<std::size_t> subsumer (const Rank& r) const {
        if (not losing_.subsumes (r))
          return std::nullopt;
        for (const auto id : generators_)
          if (Oracle::leq (result_.proofs[id].rank, r))
            return id;
        detail::require (false);
        return std::nullopt;
      }
      void enqueue_loss (RankNodeId id, losing_reason reason, std::vector<std::size_t> deps = {},
                         std::optional<bdd> input = {}, std::vector<RowIdentity> rows = {}) {
        auto& node = result_.nodes[id];
        if (node.losing)
          return;
        const auto proof_id = result_.proofs.size ();
        for (const auto dep : deps)
          detail::require (dep < proof_id);
        result_.proofs.push_back (
            {{proof_id, reason, id, 0, std::move (deps)}, node.rank, input, std::move (rows)});
        node.losing = true;
        if (id == result_.initial)
          result_.initial_proof = proof_id;
        if (losing_.insert (node.rank)) {
          std::erase_if (generators_, [&] (auto old) {
            return Oracle::leq (node.rank, result_.proofs[old].rank);
          });
          generators_.push_back (proof_id);
        }
        losses_.push_back (id);
      }
      void recompute_coverage (RankNodeId id) {
        auto& node = result_.nodes[id];
        node.covered_inputs = detail::take (oracle_.query<bdd> ([&] (auto& b) {
          bdd covered = bddfalse;
          for (auto& choice : node.choices) {
            b.step ();
            if (result_.nodes[choice.successor].losing)
              choice.active = false;
            if (choice.active)
              covered = b.lor (covered, choice.input_region);
          }
          return covered;
        }));
      }
      void propagate_losses () {
        while (not losses_.empty ()) {
          const auto id = losses_.front ();
          // Keep the event pending until ALL incoming invalidations complete.
          std::set<RankNodeId> affected;
          for (const auto& ref : result_.nodes[id].incoming) {
            auto& choice = result_.nodes[ref.source].choices[ref.choice];
            if (choice.active && choice.successor == id) {
              choice.active = false;
              affected.insert (ref.source);
            }
          }
          for (const auto source : affected) {
            recompute_coverage (source);
            if (not result_.nodes[source].losing) {
              ++result_.reopened_sources;
              enqueue (source);  // target loss is NOT a proof of source loss
            }
          }
          // Preserve the existing solver's losing-subsumption invalidation,
          // including fully covered nodes that are no longer on the open queue.
          for (RankNodeId source = 0; source < result_.nodes.size (); ++source)
            if (not result_.nodes[source].losing)
              if (const auto proof = subsumer (result_.nodes[source].rank))
                enqueue_loss (source, losing_reason::env_subsumed, {*proof});
          losses_.pop_front ();
        }
      }
      void expand (RankNodeId id) {
        // otf.md 7.4, in order. Copy the rank: interning s may grow nodes.
        if (result_.nodes[id].losing)
          return;
        const Rank rank = result_.nodes[id].rank;
        if (not rows_->is_safe (rank, K_)) {
          enqueue_loss (id, losing_reason::env_unsafe);
          return;
        }
        if (const auto proof = subsumer (rank)) {
          enqueue_loss (id, losing_reason::env_subsumed, {*proof});
          return;
        }
        auto row_ids = detail::complete_rows (*rows_, oracle_, rank);
        result_.nodes[id].active_rows_complete = true;
        recompute_coverage (id);
        if (result_.nodes[id].covered_inputs == bddtrue)
          return;
        const bdd missing = detail::take (oracle_.query<bdd> (
            [&] (auto& b) { return b.negate (result_.nodes[id].covered_inputs); }));
        const auto input = detail::take (oracle_.model (missing, Variables::inputs));
        detail::require (input.has_value ());
        const bdd bad = detail::take (oracle_.bad (rank, losing_ranks (), result_.proofs.size ()));
        const bdd bad_c = detail::take (oracle_.restrict_total (bad, *input, Variables::inputs));
        if (bad_c == bddtrue) {
          enqueue_loss (id, losing_reason::env_losing_input, generators_, input,
                        std::move (row_ids));
          return;
        }
        const bdd available =
            detail::take (oracle_.query<bdd> ([&] (auto& b) { return b.negate (bad_c); }));
        const auto output = detail::take (oracle_.model (available, Variables::outputs));
        detail::require (output.has_value ());
        const bdd letter =
            detail::take (oracle_.query<bdd> ([&] (auto& b) { return b.land (*input, *output); }));
        auto successor = detail::take (oracle_.evaluate (rank, letter));
        detail::require (successor.is_safe (K_) && not losing_.subsumes (successor));
        const bdd eq = detail::take (oracle_.eq (rank, successor));
        const bdd exact = detail::take (oracle_.restrict_total (eq, *output, Variables::outputs));
        const bdd region = detail::take (oracle_.query<bdd> ([&] (auto& b) {
          const bdd C = b.land (missing, exact);
          b.require_support (C, alphabet_.inputs);
          detail::require (b.restrict_total (C, *input, Variables::inputs) == bddtrue);
          // Real runtime checks: every choice adds a previously uncovered input.
          detail::require (b.satisfiable (C) &&
                           b.land (C, result_.nodes[id].covered_inputs) == bddfalse);
          return C;
        }));
        if (result_.choices_created >= limits_.max_choices)
          throw detail::Failure {Unknown::resource_limit};
        const auto sid = intern (std::move (successor));
        detail::require (not result_.nodes[sid].losing);
        auto& source = result_.nodes[id];
        const auto choice_id = source.choices.size ();
        source.choices.push_back ({region, *output, sid, true});
        result_.nodes[sid].incoming.push_back ({id, choice_id});
        ++result_.choices_created;
        source.covered_inputs = detail::take (
            oracle_.query<bdd> ([&] (auto& b) { return b.lor (source.covered_inputs, region); }));
        enqueue (sid);
        if (source.covered_inputs != bddtrue)
          enqueue (id);
      }
  };

}  // namespace replay

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
              "certificate_rank_bytes", "rank_interner_bytes", "losing_antichain_rank_bytes"})
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
      const int code = worker (o, Reporter {fds[1]});
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
