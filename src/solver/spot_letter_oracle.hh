#pragma once

/// P3: exact predicates over worker APs, at a fixed rank and bound. No rank
/// variables, translator, game search, or formula/polarity transformations.
#include "solver/spot_rows.hh"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace acacia::spot_letters {

  using spot_rows::Rank;
  using spot_rows::SpotRows;
  using spot_rows::StateId;
  using Epoch = std::uint64_t;

  enum class Unknown { none, resource_limit, aborted, invalid_query, row_failure };
  inline const char* unknown_name (Unknown why) {
    switch (why) {
      case Unknown::none: return "none";
      case Unknown::resource_limit: return "resource_limit";
      case Unknown::aborted: return "aborted";
      case Unknown::invalid_query: return "invalid_query";
      case Unknown::row_failure: return "row_failure";
    }
    return "unknown";
  }

  template <typename T> struct Result {
      // No Boolean conversion: absence of a result is never Boolean false.
      std::optional<T> value;
      Unknown unknown = Unknown::none;
      std::exception_ptr error;
  };
  enum class Losing { proved_losing, unresolved };  // deliberately no WIN value
  enum class Invariant { verified, rejected };
  enum class Variables { all, inputs, outputs };

  struct WorkerAlphabet {
      // Pass aut->ap_vars() and the two cubes already computed by
      // solver_invoker's bdd_exist projections AFTER worker transformations.
      // These are variable cubes, not assumptions or legal-letter filters.
      bdd ap_vars, inputs, outputs;
      std::vector<int> order;  // recorded AP order, never BDD node/level order
  };

  struct QueryLimits {
      std::size_t max_steps = std::numeric_limits<std::size_t>::max ();
      std::size_t max_live_nodes = std::numeric_limits<std::size_t>::max ();
      // Called at bounded-work checkpoints, including between edge scans.
      // Must not run another query, change the provider, or clear BDD errors.
      bool (*aborted) (void*) = nullptr;
      void* abort_data = nullptr;
  };
  struct Metrics {
      std::size_t steps = 0, bdd_operations = 0;
      std::size_t peak_live_nodes = 0, peak_result_nodes = 0;
      std::size_t threshold_hits = 0, query_hits = 0;
  };

  /// Linked BuDDyX kernel.c calls an error hook and, on node exhaustion, may
  /// subsequently return false for ALL operations (bdd_clear_error's contract).
  /// Do not unwind through its C frames, return from the hook, or resume with
  /// possibly poisoned rows/guards. A fatal worker exits inconclusively, without
  /// printing a verdict. Its parent MUST treat exit 2 and signal death (including
  /// native stack exhaustion) as UNKNOWN. Install after manager initialization,
  /// also around worker setup. bdd_init itself installs BuDDy's default hook;
  /// a fatal initialization must likewise be an inconclusive worker death.
  /// Single-thread/process use only; never fork with an active BDD query.
  class BuddyErrors {
    public:
      static constexpr int inconclusive_exit = 2;
      BuddyErrors () : previous_ (bdd_error_hook (fatal)) {}
      ~BuddyErrors () { bdd_error_hook (previous_); }
      BuddyErrors (const BuddyErrors&) = delete;
      BuddyErrors& operator= (const BuddyErrors&) = delete;
    private:
      [[noreturn]] static void fatal (int) noexcept {
        std::fputs ("spot-letter-oracle: UNKNOWN (fatal BuDDy error)\n", stderr);
        std::_Exit (inconclusive_exit);
      }
      bddinthandler previous_;
  };

  namespace detail {
    struct Failure {
        Unknown why;
        std::exception_ptr error = {};
    };

    // The only BDD operation helper. Used within Oracle::query's error scope.
    class Letters {
      public:
        Letters (const WorkerAlphabet& alphabet, const QueryLimits& limits, Metrics& metrics)
          : alphabet_ (alphabet), limits_ (limits), metrics_ (metrics) {}

        void step () {
          if (limits_.aborted && limits_.aborted (limits_.abort_data))
            throw Failure {Unknown::aborted};
          if (steps_ >= limits_.max_steps)
            throw Failure {Unknown::resource_limit};
          ++steps_;
          ++metrics_.steps;
          const auto nodes = static_cast<std::size_t> (bdd_getnodenum ());
          metrics_.peak_live_nodes = std::max (metrics_.peak_live_nodes, nodes);
          if (nodes > limits_.max_live_nodes)
            throw Failure {Unknown::resource_limit};
        }
        template <typename F> auto call (F&& f) {
          step ();
          ++metrics_.bdd_operations;
          auto result = f ();
          step ();  // never publish an over-budget result
          return result;
        }
        bdd land (bdd a, bdd b) { return call ([&] { return a & b; }); }
        bdd lor (bdd a, bdd b) { return call ([&] { return a | b; }); }
        bdd negate (bdd a) { return call ([&] { return !a; }); }
        bdd restrict (bdd a, bdd cube) { return call ([&] { return bdd_restrict (a, cube); }); }
        bdd exists (bdd a, bdd vars) { return call ([&] { return bdd_exist (a, vars); }); }
        bdd forall (bdd a, bdd vars) { return call ([&] { return bdd_forall (a, vars); }); }
        bdd support (bdd a) { return call ([&] { return bdd_support (a); }); }
        bool satisfiable (bdd a) { step (); return a != bddfalse; }
        bool tautology (bdd a) { step (); return a == bddtrue; }
        int nodes (bdd a) {
          const int count = call ([&] { return bdd_nodecount (a); });
          metrics_.peak_result_nodes = std::max (metrics_.peak_result_nodes,
                                                static_cast<std::size_t> (count));
          return count;
        }
        bdd vars (Variables which) const {
          return which == Variables::all ? alphabet_.ap_vars
                 : which == Variables::inputs ? alphabet_.inputs : alphabet_.outputs;
        }
        bool supported (bdd a, bdd allowed) {
          return exists (support (a), allowed) == bddtrue;
        }
        void require_support (bdd a, bdd allowed) {
          if (not supported (a, allowed))
            throw Failure {Unknown::invalid_query};
        }
        void validate () {
          bdd all = bddtrue;
          std::set<int> seen;
          for (const int var : alphabet_.order) {
            step ();
            if (var < 0 || var >= bdd_varnum () || not seen.insert (var).second)
              throw Failure {Unknown::invalid_query};
            all = land (all, bdd_ithvar (var));
          }
          // Equality to a positive support cube rejects filters and negative
          // literals. Disjointness plus union rejects missing/duplicate APs.
          if (all != alphabet_.ap_vars || support (alphabet_.inputs) != alphabet_.inputs
              || support (alphabet_.outputs) != alphabet_.outputs
              || land (alphabet_.inputs, alphabet_.outputs) != all
              || exists (alphabet_.inputs, alphabet_.outputs) != alphabet_.inputs)
            throw Failure {Unknown::invalid_query};
        }
        bdd restrict_total (bdd a, bdd valuation, Variables which) {
          require_support (a, alphabet_.ap_vars);
          const bdd domain = vars (which);
          if (valuation == bddfalse || support (valuation) != domain
              || call ([&] { return bdd_satoneset (valuation, domain, bddfalse); }) != valuation)
            throw Failure {Unknown::invalid_query};
          return restrict (a, valuation);
        }
        // nullopt here means UNSAT, distinguished from Result's outer UNKNOWN.
        std::optional<bdd> model (bdd a, Variables which) {
          const bdd domain = vars (which);
          require_support (a, domain);
          if (not satisfiable (a)) return std::nullopt;
          bdd result = bddtrue;
          for (const int var : alphabet_.order) {
            const bdd positive = bdd_ithvar (var);
            if (exists (domain, positive) == domain) continue;
            const bdd negative = bdd_nithvar (var);
            const bdd low = restrict (a, negative);
            if (satisfiable (low)) {
              result = land (result, negative);
              a = low;
            }
            else {
              result = land (result, positive);
              a = restrict (a, positive);
            }
          }
          return result;
        }
      private:
        const WorkerAlphabet& alphabet_;
        const QueryLimits& limits_;
        Metrics& metrics_;
        std::size_t steps_ = 0;
    };
  }  // namespace detail

  class Oracle {
    public:
      // Ownership prevents provider/dictionary reuse while guards are cached.
      // Immutable context is the outer cache key: provider identity, K, mode.
      Oracle (std::shared_ptr<SpotRows> rows, WorkerAlphabet alphabet, std::int32_t K)
        : rows_ (std::move (rows)), alphabet_ (std::move (alphabet)), K_ (K) {}
      Oracle (const Oracle&) = delete;
      Oracle& operator= (const Oracle&) = delete;

      void reset (std::shared_ptr<SpotRows> rows, WorkerAlphabet alphabet, std::int32_t K) {
        clear_caches ();  // before releasing the old provider and AP inventory
        alphabet_ = std::move (alphabet);
        rows_ = std::move (rows);
        K_ = K;
      }
      void set_bound (std::int32_t K) { clear_caches (); K_ = K; }
      void set_limits (QueryLimits limits) { limits_ = limits; }
      const Metrics& metrics () const { return metrics_; }
      const WorkerAlphabet& alphabet () const { return alphabet_; }

      Result<bdd> threshold (const Rank& r, StateId q, std::int64_t h) {
        return query<bdd> ([&] (auto& b) { return threshold (prepare (r, b), q, h, b); });
      }
      Result<bdd> unsafe (const Rank& r) {
        return query<bdd> ([&] (auto& b) { return unsafe (prepare (r, b), b); });
      }
      Result<bdd> up_pre (const Rank& r, const Rank& ell) { return preimage (Kind::up, r, ell); }
      Result<bdd> down_pre (const Rank& r, const Rank& g) { return preimage (Kind::down, r, g); }
      Result<bdd> eq (const Rank& r, const Rank& s) { return preimage (Kind::eq, r, s); }
      Result<bdd> bad (const Rank& r, const std::vector<Rank>& L, Epoch epoch) {
        return query<bdd> ([&] (auto& b) {
          sync (losing_, L, epoch, b);
          return aggregate (r, losing_, true, b);
        });
      }
      Result<bdd> good (const Rank& r, const std::vector<Rank>& G, Epoch epoch) {
        return query<bdd> ([&] (auto& b) {
          sync (candidate_, G, epoch, b);
          return aggregate (r, candidate_, false, b);
        });
      }
      Result<Losing> losing (const Rank& r, const std::vector<Rank>& L, Epoch epoch) {
        return query<Losing> ([&] (auto& b) {
          sync (losing_, L, epoch, b);
          const Rank rank = key (r);
          const auto found = losing_.losses.find (rank);
          if (found != losing_.losses.end ()) { ++metrics_.query_hits; return found->second; }
          const bdd predicate = aggregate (rank, losing_, true, b);
          // Exactly exists u forall c Bad. A false result proves no win.
          const auto result = b.tautology (b.exists (b.forall (predicate, alphabet_.outputs), alphabet_.inputs))
                                  ? Losing::proved_losing : Losing::unresolved;
          losing_.losses.emplace (rank, result);
          return result;
        });
      }
      Result<Invariant> invariant (const Rank& initial, const std::vector<Rank>& G, Epoch epoch) {
        return query<Invariant> ([&] (auto& b) {
          const Rank init = key (initial);
          sync (candidate_, G, epoch, b);
          const auto found = candidate_.invariants.find (init);
          if (found != candidate_.invariants.end ()) { ++metrics_.query_hits; return found->second; }
          auto result = [&] (Invariant value) {
            candidate_.invariants.emplace (init, value);
            return value;
          };
          bool covered = false;
          for (const auto& g : candidate_.generators) {
            b.step ();
            if (not rows_->is_safe (g, K_)) return result (Invariant::rejected);
            covered |= leq (init, g);
          }
          if (not covered) return result (Invariant::rejected);
          for (const auto& g : candidate_.generators)
            if (not b.tautology (b.forall (b.exists (aggregate (g, candidate_, false, b),
                                                    alphabet_.outputs), alphabet_.inputs)))
              return result (Invariant::rejected);
          return result (Invariant::verified);
        });
      }

      // Checked helper entry point: callers may compose AP predicates while
      // retaining the same failure/budget boundary. Never invoke query recursively.
      template <typename T, typename F> Result<T> query (F&& f) {
        BuddyErrors errors;
        try {
          detail::Letters b {alphabet_, limits_, metrics_};
          b.step ();
          if (not rows_ || K_ < 1) throw detail::Failure {Unknown::invalid_query};
          if (not validated_) { b.validate (); validated_ = true; }
          T value = f (b);
          b.step ();
          return {std::move (value), Unknown::none, {}};
        }
        catch (const detail::Failure& failure) {
          clear_caches ();
          return {std::nullopt, failure.why, failure.error};
        }
        catch (const std::bad_alloc&) { return failed<T> (Unknown::resource_limit); }
        catch (const std::length_error&) { return failed<T> (Unknown::resource_limit); }
        catch (...) { return failed<T> (Unknown::invalid_query); }
      }
      Result<bdd> restrict_total (bdd f, bdd valuation, Variables which) {
        return query<bdd> ([&] (auto& b) { return b.restrict_total (f, valuation, which); });
      }
      Result<std::optional<bdd>> model (bdd f, Variables which = Variables::all) {
        return query<std::optional<bdd>> ([&] (auto& b) { return b.model (f, which); });
      }

      static std::int32_t at (const Rank& r, std::size_t q) { return q < r.size () ? r[q] : -1; }
      static bool leq (const Rank& a, const Rank& b) {
        for (std::size_t q = 0; q < a.size (); ++q)
          if (a[q] > at (b, q)) return false;
        return true;
      }

    private:
      enum class Kind { up, down, eq };
      struct Contribution { std::int64_t level; bdd guard; };
      struct Prepared {
          std::map<StateId, std::vector<Contribution>> destinations;
          std::map<std::pair<StateId, std::int64_t>, bdd> thresholds;
          std::map<std::pair<Kind, Rank>, bdd> preimages;
          std::optional<bdd> unsafe;
      };
      struct SetCache {
          std::optional<Epoch> epoch;
          std::vector<Rank> generators;
          std::map<Rank, bdd> preimages;
          std::map<Rank, Losing> losses;
          std::map<Rank, Invariant> invariants;
      };

      void clear_caches () {
        prepared_.clear ();
        losing_ = {};
        candidate_ = {};
        validated_ = false;
      }
      template <typename T> Result<T> failed (Unknown why) {
        const auto error = std::current_exception ();
        clear_caches ();
        return {std::nullopt, why, error};
      }
      Rank key (const Rank& rank) const {
        (void) rows_->is_safe (rank, K_);  // P2 validates dimensions and [-1,K]
        Rank result = rank;
        if (rows_->mode () == spot_rows::IncrementMode::generic_transition_buchi)
          while (not result.empty () && result.back () == -1) result.pop_back ();
        return result;
      }
      Prepared& prepare (const Rank& rank, detail::Letters& b) {
        const Rank r = key (rank);
        const auto found = prepared_.find (r);
        if (found != prepared_.end ()) return found->second;
        std::vector<std::pair<std::size_t, const spot_rows::CompleteRankRow*>> active;
        for (std::size_t p = 0; p < r.size (); ++p)
          if (r[p] != -1) {
            b.step ();
            const auto row = rows_->row (static_cast<StateId> (p));
            if (row.status != spot_rows::Status::complete || not row.row)
              throw detail::Failure {row.status == spot_rows::Status::resource_limit
                                         ? Unknown::resource_limit : Unknown::row_failure, row.error};
            active.emplace_back (p, row.row);
          }
        // No predicates from partially generated rows, including empty rows.
        Prepared result;
        for (const auto& [p, row] : active)
          for (const auto& edge : row->edges) {
            b.step ();
            b.require_support (edge.condition, alphabet_.ap_vars);
            result.destinations[edge.destination].push_back (
                {std::int64_t (r[p]) + int (edge.increment), edge.condition});
          }
        return prepared_.emplace (r, std::move (result)).first->second;
      }
      bdd threshold (Prepared& r, StateId q, std::int64_t h, detail::Letters& b) {
        b.step ();
        if (h <= -1) return bddtrue;
        if (h > K_) return bddfalse;
        const auto found = r.thresholds.find ({q, h});
        if (found != r.thresholds.end ()) { ++metrics_.threshold_hits; return found->second; }
        bdd result = bddfalse;
        const auto destination = r.destinations.find (q);
        if (destination != r.destinations.end ())
          for (const auto& edge : destination->second) {
            b.step ();
            if (edge.level >= h) result = b.lor (result, edge.guard);
          }
        b.nodes (result);
        r.thresholds.emplace (std::make_pair (q, h), result);
        return result;
      }
      bdd unsafe (Prepared& r, detail::Letters& b) {
        if (r.unsafe) { ++metrics_.query_hits; return *r.unsafe; }
        const auto caps = rows_->safe_caps (K_);
        bdd result = bddfalse;
        for (const auto& [q, edges] : r.destinations) {
          (void) edges;
          result = b.lor (result, threshold (r, q, std::int64_t (caps[q]) + 1, b));
        }
        b.nodes (result);
        r.unsafe = result;
        return result;
      }
      Result<bdd> preimage (Kind kind, const Rank& r, const Rank& target) {
        return query<bdd> ([&] (auto& b) {
          auto& source = prepare (r, b);
          return preimage (kind, source, key (target), b);
        });
      }
      bdd preimage (Kind kind, Prepared& r, const Rank& target, detail::Letters& b) {
        const auto cache_key = std::make_pair (kind, target);
        const auto found = r.preimages.find (cache_key);
        if (found != r.preimages.end ()) { ++metrics_.query_hits; return found->second; }
        std::set<StateId> coordinates;
        for (std::size_t q = 0; q < target.size (); ++q) {
          b.step ();
          if (target[q] != -1) coordinates.insert (static_cast<StateId> (q));
        }
        if (kind != Kind::up)
          for (const auto& [q, edges] : r.destinations) {
            (void) edges;
            b.step ();
            coordinates.insert (q);  // mandatory D(r) UNION supp(target)
          }
        bdd result = bddtrue;
        for (const StateId q : coordinates) {
          const auto value = at (target, q);
          bdd term;
          if (kind == Kind::up)
            term = threshold (r, q, value, b);
          else if (kind == Kind::down || value == -1)
            term = b.negate (threshold (r, q, std::int64_t (value) + 1, b));
          else if (value == K_)
            term = threshold (r, q, K_, b);
          else
            term = b.land (threshold (r, q, value, b),
                           b.negate (threshold (r, q, std::int64_t (value) + 1, b)));
          result = b.land (result, term);
        }
        b.nodes (result);
        r.preimages.emplace (cache_key, result);
        return result;
      }
      void sync (SetCache& cache, const std::vector<Rank>& generators, Epoch epoch,
                 detail::Letters& b) {
        std::vector<Rank> keys;
        for (const auto& rank : generators) { b.step (); keys.push_back (key (rank)); }
        // Check contents too: even accidental epoch reuse cannot serve stale truth.
        if (cache.epoch != epoch || cache.generators != keys) {
          cache.preimages.clear ();
          cache.losses.clear ();
          cache.invariants.clear ();
          cache.epoch = epoch;
          cache.generators = std::move (keys);
        }
      }
      bdd aggregate (const Rank& rank, SetCache& set, bool bad, detail::Letters& b) {
        const Rank r = key (rank);
        const auto found = set.preimages.find (r);
        if (found != set.preimages.end ()) { ++metrics_.query_hits; return found->second; }
        auto& source = prepare (r, b);
        bdd result = bad ? unsafe (source, b) : bdd (bddfalse);
        for (const auto& target : set.generators) {
          b.step ();
          result = b.lor (result, preimage (bad ? Kind::up : Kind::down, source, target, b));
        }
        b.nodes (result);
        set.preimages.emplace (r, result);
        return result;
      }

      // Reverse destruction keeps rows/provider/dictionary alive past guards.
      std::shared_ptr<SpotRows> rows_;
      WorkerAlphabet alphabet_;
      std::int32_t K_;
      QueryLimits limits_;
      Metrics metrics_;
      bool validated_ = false;
      std::map<Rank, Prepared> prepared_;
      SetCache losing_, candidate_;
  };
}  // namespace acacia::spot_letters
