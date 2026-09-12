#pragma once

// Shared P6 sparse guarded search and independent certificate replay. The
// eager replay and real worker use exactly the same construction and engine.
#include "solver/sparse_forward_rank.hh"
#include "solver/spot_guarded_forward_safety.hh"
#include "solver/spot_lazy_buchi_view.hh"
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace acacia::spot_lazy_game {
  namespace rows = acacia::spot_rows;
  namespace letters = acacia::spot_letters;
  namespace lazy = acacia::spot_lazy;
  namespace guarded = acacia::spot_guarded;
  namespace solver_detail = acacia::solver_detail;
  using Rank = rows::SparseForwardRank<>;
  inline size_t rank_bytes (const Rank& r) {
    return sizeof (Rank) + r.entries ().capacity () * sizeof (Rank::Entry);
  }
  using guarded::detail::require;
  using guarded::detail::row_ok;
  using letters::Unknown;
  using letters::Variables;
  using rows::StateId;
  using Clock = std::chrono::steady_clock;
  inline double elapsed (Clock::time_point start) { return guarded::detail::elapsed (start); }
  inline double clock_ms () {
    return std::chrono::duration<double, std::milli> (Clock::now ().time_since_epoch ()).count ();
  }

  [[noreturn]] inline void fail (const std::string& s) {
    throw std::runtime_error ("spot-lazy: " + s);
  }
  inline std::string cell (std::string s) {
    for (char& c : s)
      if (c == '\t' || c == '\n' || c == '\r')
        c = ' ';
    return s;
  }
  inline std::string decimal (double n) {
    std::ostringstream s;
    s << std::fixed << std::setprecision (6) << n;
    return s.str ();
  }
  // The caller owns persistence (the replay pipe or worker log). No process
  // or output stream is created by the shared search/verifier implementation.
  struct Reporter {
      std::function<void (const std::string&, const std::string&)> sink;
      void put (const std::string& k, const std::string& v) const {
        if (sink) sink (k, v);
      }
      void count (const std::string& k, size_t v) const { if (sink) put (k, std::to_string (v)); }
      void ms (const std::string& k, double v) const { if (sink) put (k, decimal (v)); }
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
      spot::const_twa_ptr provider;
      std::optional<rows::FrozenAcacia> frozen_view;
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
          provider (view),
          cache (std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {view}, l)),
          report (r) {
        discover ();
      }

      RowStore (rows::FrozenAcacia v, rows::RowLimits l, Reporter r = {})
        : provider (v.graph), frozen_view (v),
          cache (std::make_shared<rows::SpotRows> (v, l)), report (r) { discover (); }
      void check_contract () const { if (view) view->check_contract (); }
      int safe_cap (StateId q, int K) const {
        return frozen_view && q >= frozen_view->bool_threshold ? 0 : K - 1;
      }
      const rows::CompleteRankRow& get (StateId source) {
        check_contract ();
        if (report.sink && phase == Phase::search)
          search_sources.insert (source);
        if (report.sink && phase == Phase::verify)
          verifier_sources.insert (source);
        const bool missing = cache->state (source) != rows::RowState::complete;
        if (missing && frozen)
          fail ("frozen graph row miss");
        const auto started = missing && report.sink ? Clock::now () : Clock::time_point {};
        if (missing) {
          ++requests;
          snapshot ();
          report.ms ("row_started_clock_ms", clock_ms ());
          report.put ("row_generation_active", "true");
        }
        const auto row = cache->row (source);
        if (missing && report.sink)
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
        // Stored counters only; no provider traversal.
        if (!report.sink) return;
        if (frozen_view) {
          report.count ("wrapper_rows_generated", cache->complete_rows ());
          report.count ("wrapper_states_discovered", cache->state_count ());
          report.count ("wrapper_edges_generated", generated_edges);
          report.count ("verification_rows_rebuilt", verifier_rebuilt);
          report.ms ("row_generation_ms", generation_ms);
          return;
        }
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
        : twa (s.provider->get_dict ()),
          store_ (s),
          root_ (root) {
        set_buchi ();
        prop_state_acc (false);
        copy_ap_of (s.provider);
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
      std::shared_ptr<rows::SpotRows> frozen_verifier_;

    public:
      Reader (RowStore& s, bool v, rows::RowLimits l) : store_ (s), verifier_ (v), limits_ (l) {
        if (v && s.frozen_view)
          frozen_verifier_ = std::make_shared<rows::SpotRows> (*s.frozen_view, l);
      }
      Rank initial_rank () const { return Rank {{{store_.cache->initial_id (), 0}}, 1}; }
      bool is_safe (const Rank& r, int K) const {
        (void) r.is_safe (K);
        for (auto [q, value] : r.entries ()) {
          require (q < store_.cache->state_count ());
          if (value > store_.safe_cap (q, K)) return false;
        }
        return true;
      }
      int safe_cap (StateId q, int K) const { return store_.safe_cap (q, K); }
      const rows::CompleteRankRow& row (StateId q) {
        if (!verifier_)
          return store_.get (q);
        if (auto it = rebuilt_.find (q); it != rebuilt_.end ())
          return it->second;
        if (rebuilt_.size () >= limits_.max_rows)
          throw letters::detail::Failure {Unknown::resource_limit};
        if (store_.frozen_view) {
          (void) store_.get (q); // account verification demand independently
          const auto result = frozen_verifier_->row (q);
          row_ok (result.status);
          require (result.row != nullptr);
          ++store_.verifier_rebuilt;
          return rebuilt_.emplace (q, *result.row).first->second;
        }
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
            result = b.lor (result, threshold (p, q, rows_.safe_cap (q, K_) + 1, b));
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
      // The letters under which the successor of r is at or below s, rather
      // than exactly s.  This is the same downward preimage the inductive
      // invariant check already aggregates; it is exposed here so guarded
      // expansion and the certificate verifier can build a choice region from
      // it.  Per coordinate the term forbids the successor exceeding s there,
      // over supp(s) union the destinations of r; outside that set both sides
      // are absent, so the constraint is vacuous.
      letters::Result<bdd> down (const Rank& r, const Rank& s) {
        return query<bdd> ([&] (auto& b) { return preimage (prepare (r, b), s, 1, b); });
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
            if (!rows_.is_safe (g, K_))
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
        if (!out.sink) return;
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
}  // namespace acacia::spot_lazy_game

namespace acacia::spot_lazy_game {
  // Sparse upward antichain with P4's exact order; no dense coordinate-sum
  // prefilter (its implicit -1 tail would depend on the discovered arena).
  class LossSet {
      std::vector<Rank> generators_;
      std::vector<std::size_t> proofs_;  // parallel to generators_

    public:
      // Counter names and mutability mirror solver_detail::minimal_losing_antichain,
      // the dense twin of this structure, so the two solvers' logs read alike.
      // The query pair is mutable because subsumes() is const.
      mutable std::size_t queries = 0, hits = 0, prefilter_skips = 0;
      std::size_t insertions = 0, removals = 0, peak = 0;

      // The proof that witnesses a subsumption is found by the same scan that
      // decides it.  Recovering it with a second pass over a parallel list, as
      // this class and Search used to between them, walked every generator and
      // repeated every leq to learn something the first walk already knew.
      std::optional<std::size_t> subsumer (const Rank& r) const {
        ++queries;
        for (std::size_t i = 0; i < generators_.size (); ++i) {
          // Called explicitly rather than left to leq, which runs it again on
          // a candidate that passes: two integer comparisons against a merge
          // join, and it keeps leq self-contained for its many other callers.
          if (not generators_[i].prefilter_leq (r)) {
            ++prefilter_skips;
            continue;
          }
          if (generators_[i].leq (r)) {
            ++hits;
            return proofs_[i];
          }
        }
        return std::nullopt;
      }
      bool subsumes (const Rank& r) const { return subsumer (r).has_value (); }
      bool insert (const Rank& r, std::size_t proof) {
        if (subsumes (r))
          return false;
        // Compact both vectors together: they are parallel, and an erase_if on
        // one alone would silently misattribute every later witness.
        std::size_t write = 0;
        for (std::size_t read = 0; read < generators_.size (); ++read) {
          if (r.leq (generators_[read])) {
            ++removals;
            continue;
          }
          if (write != read) {
            generators_[write] = generators_[read];
            proofs_[write] = proofs_[read];
          }
          ++write;
        }
        generators_.erase (generators_.begin () + static_cast<std::ptrdiff_t> (write),
                           generators_.end ());
        proofs_.erase (proofs_.begin () + static_cast<std::ptrdiff_t> (write), proofs_.end ());
        generators_.push_back (r);
        proofs_.push_back (proof);
        ++insertions;
        peak = std::max (peak, generators_.size ());
        return true;
      }
      // Contiguous, so Oracle::bad's const std::vector<Rank>& binds with no copy.
      const std::vector<Rank>& ranks () const { return generators_; }
      const std::vector<std::size_t>& proof_ids () const { return proofs_; }
      std::size_t size () const { return generators_.size (); }
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
  // guarded choices, proof chronology and inductive certificate checks.
  using guarded::ChoiceRef;
  using guarded::RankNodeId;
  using guarded::RowIdentity;
  enum class SuccessorRelation { exact, downward };
  enum class OutputChoice { constant, existential };
  struct ChoiceSemantics {
      SuccessorRelation successor_relation;
      OutputChoice output_choice;
      bool operator== (const ChoiceSemantics&) const = default;
      bool valid () const {
        return (successor_relation == SuccessorRelation::exact ||
                successor_relation == SuccessorRelation::downward) &&
               (output_choice == OutputChoice::constant ||
                output_choice == OutputChoice::existential);
      }
  };
  // Preserve the existing build's default; either dimension can independently
  // be overridden by internal callers in the same binary.
  inline constexpr ChoiceSemantics default_choice_semantics {
#if ACACIA_SPOT_GUARDED_INEQUALITY_COVERING
      SuccessorRelation::downward,
#else
      SuccessorRelation::exact,
#endif
      OutputChoice::constant};
  struct SparseChoice {
      bdd input_region;
      // Present exactly in constant mode, where it must be a total output cube.
      // Existential choices store no output witness or cached projection.
      std::optional<bdd> constant_output;
      RankNodeId successor;
      bool active = true;

      static SparseChoice constant (bdd region, bdd output, RankNodeId target) {
        return SparseChoice (region, output, target);
      }
      static SparseChoice existential (bdd region, RankNodeId target) {
        return SparseChoice (region, std::nullopt, target);
      }

    private:
      // Named factories prevent old aggregate initializers changing meaning.
      SparseChoice (bdd region, std::optional<bdd> output, RankNodeId target)
        : input_region (region), constant_output (output), successor (target) {}
  };
  struct GuardedRankNode {
      Rank rank;
      bool active_rows_complete = false;
      bool losing = false;
      std::vector<SparseChoice> choices;
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
      ChoiceSemantics semantics = default_choice_semantics;
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
      // Losing-region work.  subsumption_scans counts broad passes over the
      // interned nodes; nodes_checked and nodes_invalidated are that pass's
      // numerator and denominator.  The antichain's own counters come from
      // LossSet.  Names follow the dense forward solver's child_metrics.
      std::size_t subsumption_scans = 0, reopen_enqueues = 0;
      std::size_t subsumption_nodes_checked = 0, subsumption_nodes_invalidated = 0;
      std::size_t subsumption_queries = 0, subsumption_hits = 0;
      std::size_t subsumption_prefilter_skips = 0;
      std::size_t losing_insertions = 0, losing_removals = 0;
      std::size_t losing_antichain_size = 0, losing_antichain_peak = 0;
      double prep_ms = 0, solve_ms = 0, verify_ms = 0;
  };

  namespace detail {
    using guarded::detail::checked;
    using guarded::detail::elapsed;
    using guarded::detail::Failure;
    using guarded::detail::require;
    using guarded::detail::row_ok;
    using guarded::detail::take;
    inline void validate_semantics (const SolveResult& certificate, ChoiceSemantics requested) {
      require (requested.valid () && certificate.semantics.valid () &&
               certificate.semantics == requested);
      for (const auto& node : certificate.nodes)
        for (const auto& choice : node.choices)
          require (choice.constant_output.has_value () ==
                   (certificate.semantics.output_choice == OutputChoice::constant));
    }
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
      const SolveResult& certificate, const Limits& limits = {},
      ChoiceSemantics requested = default_choice_semantics) {
    return detail::checked<std::vector<Rank>> ([&] {
      detail::validate_semantics (certificate, requested);
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
          // Rebuilt here from the fresh Reader and Oracle above, never taken
          // from the search: a choice that claims too much input space must
          // fail this. The validated certificate tag selects the obligation.
          const bdd reaches = detail::take (
              certificate.semantics.successor_relation == SuccessorRelation::downward
                  ? oracle.down (node.rank, target.rank) : oracle.eq (node.rank, target.rank));
          const bdd projection = detail::take (
              certificate.semantics.output_choice == OutputChoice::constant
                  ? oracle.restrict_total (reaches, *choice.constant_output, Variables::outputs)
                  : oracle.query<bdd> ([&] (auto& b) {
                      return b.exists (reaches, b.vars (Variables::outputs));
                    }));
          covered = detail::take (oracle.query<bdd> ([&] (auto& b) {
            b.require_support (choice.input_region, alphabet.inputs);
            detail::require (b.satisfiable (choice.input_region));
            detail::require (b.land (choice.input_region, b.negate (projection)) == bddfalse);
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

  inline letters::Result<bool> verify_losing_proof (
      RowStore& view, const letters::WorkerAlphabet& alphabet, std::int32_t K,
      const SolveResult& certificate, const Limits& limits = {},
      ChoiceSemantics requested = default_choice_semantics) {
    return detail::checked<bool> ([&] {
      detail::validate_semantics (certificate, requested);
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
            rows->is_safe (proof.rank, K);  // validates this provider's exact rank domain
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
      Search (RowStore& view, letters::WorkerAlphabet alphabet, std::int32_t K, Limits limits = {},
              ChoiceSemantics semantics = default_choice_semantics)
        : view_ (view),
          alphabet_ (std::move (alphabet)),
          K_ (K),
          limits_ (limits),
          semantics_ (semantics),
          rows_ (std::make_shared<Reader> (view_, false, limits.rows)),
          oracle_ (*rows_, view_, alphabet_, K) {
        result_.provider = view_.provider;
        result_.semantics = semantics_;
        oracle_.set_limits (limits_.queries);
      }
      ~Search () {
        if (!view_.report.sink) return;
        size_t bytes = 0;
        for (const auto& [rank, id] : interned_) {
          (void) id;
          bytes += rank_bytes (rank);
        }
        view_.report.count ("rank_interner_bytes", bytes);
        view_.report.count ("losing_antichain_rank_bytes", losing_.bytes ());
        // Emitted from the live counters, not from result_, which solve() has
        // moved from by the time this runs.  publish_counters() copies these
        // same members, so a string sink and a SolveResult reader agree.
        view_.report.count ("subsumption_scans", subsumption_scans_);
        view_.report.count ("subsumption_nodes_checked", nodes_checked_);
        view_.report.count ("subsumption_nodes_invalidated", nodes_invalidated_);
        view_.report.count ("reopen_enqueues", reopen_enqueues_);
        view_.report.count ("subsumption_queries", losing_.queries);
        view_.report.count ("subsumption_hits", losing_.hits);
        view_.report.count ("subsumption_prefilter_skips", losing_.prefilter_skips);
        view_.report.count ("losing_insertions", losing_.insertions);
        view_.report.count ("losing_removals", losing_.removals);
        view_.report.count ("losing_antichain_size", losing_.size ());
        view_.report.count ("losing_antichain_peak", losing_.peak);
      }
      // Copy the live counters into the result.  Called once, from solve(),
      // before either return; ~Search then emits the same values.
      void publish_counters () {
        result_.subsumption_scans = subsumption_scans_;
        result_.subsumption_nodes_checked = nodes_checked_;
        result_.subsumption_nodes_invalidated = nodes_invalidated_;
        result_.reopen_enqueues = reopen_enqueues_;
        result_.subsumption_queries = losing_.queries;
        result_.subsumption_hits = losing_.hits;
        result_.subsumption_prefilter_skips = losing_.prefilter_skips;
        result_.losing_insertions = losing_.insertions;
        result_.losing_removals = losing_.removals;
        result_.losing_antichain_size = losing_.size ();
        result_.losing_antichain_peak = losing_.peak;
      }
      SolveResult solve () {
        view_.phase = Phase::search;
        view_.report.ms ("stage_started_clock_ms", clock_ms ());
        MetricReport metrics {oracle_, view_.report, "search_"};
        const auto started = std::chrono::steady_clock::now ();
        const auto search = detail::checked<bool> ([&] {
          detail::require (semantics_.valid ());
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
        publish_counters ();  // before both returns below, and before verification
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
          const auto verified = verify_losing_proof (view_, alphabet_, K_, result_, limits_, semantics_);
          result_.status = verified.value && *verified.value ? forward_result_status::lose_k
                                                             : forward_result_status::unknown;
          result_.failure = verified.unknown;
        }
        else {
          auto verified = verify_winning_certificate (view_, alphabet_, K_, result_, limits_, semantics_);
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
      const ChoiceSemantics semantics_;
      std::shared_ptr<Reader> rows_;
      Oracle oracle_;
      SolveResult result_;
      std::unordered_map<Rank, RankNodeId> interned_;
      size_t before_search_ = view_.cache->complete_rows ();
      solver_detail::forward_work_queue<RankNodeId> open_, losses_;
      LossSet losing_;
      std::size_t subsumption_scans_ = 0;
      std::size_t nodes_checked_ = 0, nodes_invalidated_ = 0;
      std::size_t reopen_enqueues_ = 0;
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
      std::optional<std::size_t> subsumer (const Rank& r) const {
        return losing_.subsumer (r);
      }
      void enqueue_loss (RankNodeId id, losing_reason reason, std::vector<std::size_t> deps = {},
                         std::optional<bdd> input = {}, std::vector<RowIdentity> rows = {}) {
        auto& node = result_.nodes[id];
        if (node.losing)
          return;
        // Copy before the push_back below can reallocate result_.proofs, and
        // before node is used across it.
        const Rank rank = node.rank;
        const auto proof_id = result_.proofs.size ();
        for (const auto dep : deps)
          detail::require (dep < proof_id);
        result_.proofs.push_back (
            {{proof_id, reason, id, 0, std::move (deps)}, rank, input, std::move (rows)});
        node.losing = true;
        if (id == result_.initial)
          result_.initial_proof = proof_id;
        losses_.push_back (id);
        if (not losing_.insert (rank, proof_id))
          return;  // rank was already inside the region: nothing new is implied
        // The region grew, so broadcast the one generator that grew it.  Only
        // ranks above `rank` can be newly implied: everything above an older
        // generator was marked when that generator was inserted, and this scan
        // is the step that establishes it.  Testing `rank` alone is therefore
        // equivalent to re-testing every generator, at one comparison per node.
        //
        // enqueue_loss never interns, so result_.nodes is fixed for the whole
        // cascade, and a node marked here has a rank above `rank`, so its own
        // insert() returns false and it cannot start a further scan.
        ++subsumption_scans_;
        for (RankNodeId source = 0; source < result_.nodes.size (); ++source) {
          ++nodes_checked_;
          if (result_.nodes[source].losing)
            continue;
          if (Oracle::leq (rank, result_.nodes[source].rank)) {
            ++nodes_invalidated_;
            enqueue_loss (source, losing_reason::env_subsumed, {proof_id});
          }
        }
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
              // reopened_sources counts attempts, and an attempt on a node that
              // is already queued does nothing.  Counting the enqueues too is
              // what makes the pair readable: the attempts move when losses are
              // discovered earlier, the enqueues move only if the search does.
              if (not result_.nodes[source].queued)
                ++reopen_enqueues_;
              enqueue (source);  // target loss is NOT a proof of source loss
            }
          }
          // Subsumption invalidation is not here any more: it happens once per
          // generator, in enqueue_loss, where the region is what grows.  This
          // drain no longer appends to its own queue.
          losses_.pop_front ();
        }
      }
      void expand (RankNodeId id) {
        // OTF-AND-SPOT.md handoff 7.4, in order. Copy the rank: interning s may grow nodes.
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
        const bdd bad = detail::take (oracle_.bad (rank, losing_.ranks (), result_.proofs.size ()));
        const bdd bad_c = detail::take (oracle_.restrict_total (bad, *input, Variables::inputs));
        if (bad_c == bddtrue) {
          // deps is taken by value, so this copies the witness list before any
          // later insert can compact it.  Do not make the parameter a reference.
          enqueue_loss (id, losing_reason::env_losing_input, losing_.proof_ids (), input,
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
        detail::require (rows_->is_safe (successor, K_) && not losing_.subsumes (successor));
        // Widening this from "reaches exactly the successor" to "reaches at or
        // below it" lets one choice claim more of the missing input space. The
        // successor itself is unchanged, so the target is still a rank the
        // search reached and verified, not a synthesized upper bound.
        const bdd reaches = detail::take (semantics_.successor_relation == SuccessorRelation::downward
                                             ? oracle_.down (rank, successor)
                                             : oracle_.eq (rank, successor));
        // The sampled total output selected the target. Existential covering
        // may use a different output at each input while retaining that target;
        // quantify only the worker outputs, inside the checked query scope.
        const bdd projection = detail::take (
            semantics_.output_choice == OutputChoice::constant
                ? oracle_.restrict_total (reaches, *output, Variables::outputs)
                : oracle_.query<bdd> ([&] (auto& b) {
                    return b.exists (reaches, b.vars (Variables::outputs));
                  }));
        const bdd region = detail::take (oracle_.query<bdd> ([&] (auto& b) {
          const bdd C = b.land (missing, projection);
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
        source.choices.push_back (semantics_.output_choice == OutputChoice::constant
                                     ? SparseChoice::constant (region, *output, sid)
                                     : SparseChoice::existential (region, sid));
        result_.nodes[sid].incoming.push_back ({id, choice_id});
        ++result_.choices_created;
        source.covered_inputs = detail::take (
            oracle_.query<bdd> ([&] (auto& b) { return b.lor (source.covered_inputs, region); }));
        enqueue (sid);
        if (source.covered_inputs != bddtrue)
          enqueue (id);
      }
  };

}  // namespace acacia::spot_lazy_game
