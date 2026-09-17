#include "solver/closure_buchi_provider.hh"

#include "solver/spot_letter_oracle.hh"  // reuse the worker's fatal BuDDy boundary
#include <unordered_map>
#include <unordered_set>

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <set>

namespace acacia::closure_buchi {
  namespace {
    using Clock = std::chrono::steady_clock;
    using Formula = spot::formula;
    using Op = spot::op;
    using Set = std::vector<ClosureId>;
    std::uint64_t elapsed (Clock::time_point start) {
      return std::chrono::duration_cast<std::chrono::nanoseconds> (Clock::now () - start).count ();
    }
    struct Timer {
        std::uint64_t& ns;
        Clock::time_point start = Clock::now ();
        ~Timer () { ns += elapsed (start); }
    };
    [[noreturn]] void fail (FailureKind kind) { throw Failure {kind}; }
    Failure caught () noexcept {
      try {
        throw;
      } catch (const Failure& f) {
        return f;
      } catch (const AdapterFailure& f) {
        return f.failure ();
      } catch (const std::bad_alloc&) {
        return {FailureKind::memory_limit, std::current_exception ()};
      } catch (const std::length_error&) {
        return {FailureKind::memory_limit, std::current_exception ()};
      } catch (...) {
        return {FailureKind::unexpected, std::current_exception ()};
      }
    }
    void check (const Options& o) {
      if (o.hooks && o.hooks->cancelled && o.hooks->cancelled ())
        fail (FailureKind::cancelled);
      if (static_cast<std::size_t> (bdd_getnodenum ()) > o.max_live_bdd_nodes)
        fail (FailureKind::guard_limit);
    }
    void hit (const Options& o, FaultPoint point) {
      check (o);
      if (o.hooks && o.hooks->fail && o.hooks->fail (point))
        fail (FailureKind::injected);
    }
    void bytes (const Options& o, std::size_t retained, std::size_t temporary = 0) {
      if (retained > o.max_bytes || temporary > o.max_bytes - retained)
        fail (FailureKind::memory_limit);
    }
    void insert (Set& s, ClosureId id) {
      const auto p = std::lower_bound (s.begin (), s.end (), id);
      if (p == s.end () || *p != id)
        s.insert (p, id);
    }

    // Memoize by (input DAG node, polarity). Constructors hash-cons the result;
    // no string round-trip, automaton, implication test, or tree expansion.
    class Normalizer {
        const Options& options_;
        std::map<std::pair<Formula, bool>, Formula> memo_;
        std::unordered_set<Formula> generated_;
        std::set<Formula> aps_;
        std::size_t arcs_ = 0;
        Formula remember (Formula f) {
          std::vector<Formula> work {f};
          while (!work.empty ()) {
            check (options_);
            auto a = work.back ();
            work.pop_back ();
            if (!generated_.insert (a).second)
              continue;
            arcs_ += a.size ();
            if (generated_.size () > options_.max_normalization_nodes)
              fail (FailureKind::normalization_limit);
            bytes (options_, 0,
                   128 * (generated_.size () + memo_.size ()) + sizeof (Formula) * arcs_);
            for (auto c : a)
              work.push_back (c);
          }
          return f;
        }

      public:
        explicit Normalizer (const Options& o) : options_ (o) {}
        std::vector<Formula> ap_inventory () const { return {aps_.begin (), aps_.end ()}; }
        Formula run (Formula f, bool neg = false, std::size_t depth = 0) {
          check (options_);
          if (depth > options_.max_normalization_depth)
            fail (FailureKind::normalization_limit);
          const auto key = std::make_pair (f, neg);
          if (auto p = memo_.find (key); p != memo_.end ())
            return p->second;
          if (memo_.size () >= options_.max_normalization_nodes)
            fail (FailureKind::normalization_limit);
          auto child = [&] (unsigned i, bool n) { return run (f[i], n, depth + 1); };
          auto make = [&] (Op op, Formula a, Formula b) {
            return remember (Formula::binop (op, a, b));
          };
          auto join = [&] (Op op, std::vector<Formula> v) {
            return remember (Formula::multop (op, std::move (v)));
          };
          Formula result;
          switch (f.kind ()) {
            case Op::tt: result = neg ? Formula::ff () : f; break;
            case Op::ff: result = neg ? Formula::tt () : f; break;
            case Op::ap:
              aps_.insert (f);
              result = neg ? Formula::Not (f) : f;
              break;
            case Op::Not: result = child (0, !neg); break;
            case Op::And:
            case Op::Or: {
              std::vector<Formula> v;
              for (unsigned i = 0; i < f.size (); ++i)
                v.push_back (child (i, neg));
              const bool conjunction = (f.kind () == Op::And) != neg;
              result = join (conjunction ? Op::And : Op::Or, std::move (v));
              break;
            }
            case Op::X: result = Formula::X (child (0, neg)); break;
            case Op::F:
              result = make (neg ? Op::R : Op::U, neg ? Formula::ff () : Formula::tt (),
                             child (0, neg));
              break;
            case Op::G:
              result = make (neg ? Op::U : Op::R, neg ? Formula::tt () : Formula::ff (),
                             child (0, neg));
              break;
            case Op::U:
            case Op::R: {
              const bool until = (f.kind () == Op::U) != neg;
              result = make (until ? Op::U : Op::R, child (0, neg), child (1, neg));
              break;
            }
            case Op::W:
            case Op::M: {
              // a W b = b R (a | b); a M b = b U (a & b).
              const bool weak = f.kind () == Op::W;
              auto a = child (0, neg), b = child (1, neg);
              result = make (weak != neg ? Op::R : Op::U, b,
                             join (weak != neg ? Op::Or : Op::And, {a, b}));
              break;
            }
            case Op::Implies:
              result = join (neg ? Op::And : Op::Or, {child (0, !neg), child (1, neg)});
              break;
            case Op::Equiv:
            case Op::Xor: {
              const bool equal = (f.kind () == Op::Equiv) != neg;
              auto a = child (0, false), na = child (0, true);
              auto b = child (1, false), nb = child (1, true);
              result = join (Op::Or, {join (Op::And, {a, equal ? b : nb}),
                                      join (Op::And, {na, equal ? nb : b})});
              break;
            }
            default: fail (FailureKind::unsupported_operator);
          }
          result = remember (result);
          memo_.emplace (key, result);
          if (memo_.size () > options_.max_normalization_nodes)
            fail (FailureKind::normalization_limit);
          bytes (options_, 0,
                 128 * (generated_.size () + memo_.size ()) + sizeof (Formula) * arcs_);
          return result;
        }
    };
  }

  struct Provider::Impl {
      struct RawEdge {
          bdd guard;
          Set next, postponed;
      };
      struct Underlying {
          Set pending;
          std::optional<std::vector<RawEdge>> row;
      };
      struct Cursor {
          std::size_t underlying, j;
          std::optional<std::vector<Edge>> row;
      };
      spot::bdd_dict_ptr dict;
      Options options;
      Counters stats;
      Clock::time_point started = Clock::now ();
      Formula normalized;
      std::vector<Formula> closure;
      std::unordered_map<Formula, ClosureId> ids;
      Set untils;
      std::vector<Formula> aps;
      std::vector<std::optional<bdd>> literals;  // generalized boolean_guard cache
      std::vector<unsigned char> is_boolean;
      std::deque<Underlying> underlying;
      std::map<Set, std::size_t> underlying_ids;  // exact lexicographic comparison
      std::deque<Cursor> states;
      std::map<std::pair<std::size_t, std::size_t>, StateId> state_ids;

      Impl (spot::bdd_dict_ptr d, Options o) : dict (std::move (d)), options (std::move (o)) {}
      ~Impl () { dict->unregister_all_my_variables (this); }

      void initialize (Formula f) {
        {
          Timer timer {stats.normalization_ns};
          Normalizer normalizer {options};
          normalized = normalizer.run (f);
          // Preserve even APs removed by syntactic constant folding, so the
          // worker's dictionary/partition inventory is fixed at this boundary.
          aps = normalizer.ap_inventory ();
        }
        stats.retained_bytes = sizeof (Impl) + aps.capacity () * (sizeof (Formula) + 64);
        bytes (options, stats.retained_bytes);
        std::vector<Formula> work {normalized};
        while (!work.empty ()) {
          check (options);
          auto a = work.back ();
          work.pop_back ();
          if (ids.contains (a))
            continue;
          if (closure.size () >= options.max_normalization_nodes ||
              closure.size () >= std::numeric_limits<ClosureId>::max ())
            fail (FailureKind::normalization_limit);
          auto id = static_cast<ClosureId> (closure.size ());
          ids.emplace (a, id);
          closure.push_back (a);
          if (a.is (Op::U))
            untils.push_back (id);
          stats.retained_bytes += 160 + a.size () * sizeof (Formula);
          bytes (options, stats.retained_bytes);
          for (auto c : a)
            work.push_back (c);
        }
        // This registration owner also lives as long as cloned Spot states.
        for (auto ap : aps) {
          check (options);
          dict->register_proposition (ap, this);
        }
        literals.resize (closure.size ());
        for (std::size_t i = 0; i < closure.size (); ++i) {
          auto a = closure[i];
          if (a.is (Op::ap) || a.is (Op::Not)) {
            check (options);
            auto ap = a.is (Op::ap) ? a : a[0];
            literals[i] =
                a.is (Op::ap) ? bdd_ithvar (dict->varnum (ap)) : bdd_nithvar (dict->varnum (ap));
            ++stats.guards_generated;
            ++stats.boolean_complete_entries;
          } else if (a.is (Op::tt)) {
            literals[i] = bddtrue;
            ++stats.boolean_complete_entries;
          } else if (a.is (Op::ff)) {
            literals[i] = bddfalse;
            ++stats.boolean_complete_entries;
          }
        }
        // Every child of closure[i] was pushed onto `work` strictly after i's
        // own id was assigned above, so child ids are always > i: a single
        // reverse pass sees every child of And/Or before the node itself,
        // with no recursion and no dependency on insertion/discovery order.
        is_boolean.resize (closure.size ());
        for (std::size_t i = closure.size (); i-- > 0; ) {
          auto a = closure[i];
          switch (a.kind ()) {
            case Op::tt: case Op::ff: case Op::ap: case Op::Not:
              is_boolean[i] = 1;
              break;
            case Op::And: case Op::Or: {
              bool boolean = true;
              for (auto c : a)
                boolean = boolean && is_boolean[ids.at (c)];
              is_boolean[i] = boolean;
              break;
            }
            default:
              is_boolean[i] = 0;
              break;
          }
        }
        intern ({ids.at (normalized)}, 0);
        check (options);
      }

      StateId intern (const Set& pending, std::size_t j) {
        hit (options, FaultPoint::interning);
        auto found = underlying_ids.find (pending);
        std::size_t u;
        if (found != underlying_ids.end ())
          u = found->second;
        else {
          if (underlying.size () >= options.max_states)
            fail (FailureKind::state_limit);
          const auto added = sizeof (Underlying) + 96 + 2 * pending.size () * sizeof (ClosureId);
          bytes (options, stats.retained_bytes, added);
          u = underlying.size ();
          underlying.push_back ({pending, {}});
          try {
            underlying_ids.emplace (pending, u);
          } catch (...) {
            underlying.pop_back ();
            throw;
          }
          stats.retained_bytes += added;
        }
        const auto key = std::make_pair (u, j);
        if (auto s = state_ids.find (key); s != state_ids.end ())
          return s->second;
        if (states.size () >= options.max_states ||
            states.size () >= std::numeric_limits<StateId>::max ())
          fail (FailureKind::state_limit);
        const auto added = sizeof (Cursor) + 96;
        bytes (options, stats.retained_bytes, added);
        auto id = static_cast<StateId> (states.size ());
        states.push_back ({u, j, {}});
        try {
          state_ids.emplace (key, id);
        } catch (...) {
          states.pop_back ();
          throw;
        }
        stats.retained_bytes += added;
        stats.states_discovered = states.size ();
        return id;
      }

      struct RowBudget {
          Impl& self;
          std::size_t branches = 0, steps = 0, guards = 0;
          bdd guard (bdd a, bdd b, bool disjoin = false) {
            hit (self.options, FaultPoint::guard);
            if (guards++ >= self.options.max_guards_per_row)
              fail (FailureKind::guard_limit);
            ++self.stats.guards_generated;
            bdd result = disjoin ? (a | b) : (a & b);
            check (self.options);
            return result;
          }
      };

      // Memoized postorder evaluator over Boolean closure IDs, reusing the
      // `literals` cache (RowBudget::guard is the checked AND/OR operation,
      // so this stays inside the same fatal-BuDDy/cancellation boundary as
      // ordinary branch expansion). Iterative: bounded by heap, not native
      // call-stack depth, so a long chain of nested Boolean And/Or cannot
      // overflow the stack. Only a completely computed result is cached;
      // RowBudget::guard/hit throw (uncached) on a limit or cancellation
      // before any partial value is stored, so a retried row after a
      // transient failure recomputes cleanly. Reused across rows, cursors
      // and K attempts within this provider: a closure id is evaluated at
      // most once for the provider's lifetime.
      bdd boolean_guard (ClosureId root, RowBudget& budget) {
        if (!literals[root]) {
          Timer timer {stats.boolean_conversion_ns};
          std::vector<ClosureId> work {root};
          while (!work.empty ()) {
            auto id = work.back ();
            if (literals[id]) { work.pop_back (); continue; }
            auto a = closure[id];
            bool ready = true;
            for (auto c : a) {
              auto child = ids.at (c);
              if (!literals[child]) { work.push_back (child); ready = false; }
            }
            if (!ready)
              continue;
            work.pop_back ();
            const bool conjunction = a.kind () == Op::And;
            bdd result = conjunction ? bdd (bddtrue) : bdd (bddfalse);
            for (auto c : a)
              result = budget.guard (result, *literals[ids.at (c)], !conjunction);
            literals[id] = result;
            ++stats.boolean_conversion_calls;
            ++stats.boolean_complete_entries;
          }
        }
        return *literals[root];
      }

      struct Branch {
          Set work, next, postponed;
          std::vector<unsigned char> done;
          bdd guard = bddtrue;
          std::size_t bytes () const {
            return sizeof (Branch) +
                   sizeof (ClosureId) *
                       (work.capacity () + next.capacity () + postponed.capacity ()) +
                   done.capacity ();
          }
      };

      std::vector<RawEdge> expand (const Set& pending, RowBudget& budget) {
        Timer timer {stats.raw_row_ns};
        std::vector<Branch> stack;
        stack.push_back ({pending, {}, {}, std::vector<unsigned char> (closure.size ()), bddtrue});
        std::size_t stack_bytes = stack.back ().bytes (), raw_bytes = 0;
        std::map<std::pair<Set, Set>, bdd> merged;
        while (!stack.empty ()) {
          hit (options, FaultPoint::branch);
          if (budget.branches++ >= options.max_branches_per_row)
            fail (FailureKind::branch_limit);
          ++stats.branches_considered;
          Branch b = std::move (stack.back ());
          stack.pop_back ();
          stack_bytes -= b.bytes ();
          auto fork = [&] (Branch other) {
            stack_bytes += other.bytes ();
            bytes (options, stats.retained_bytes, stack_bytes + b.bytes () + raw_bytes);
            stack.push_back (std::move (other));
          };
          while (!b.work.empty () && b.guard != bddfalse) {
            hit (options, FaultPoint::branch);
            if (budget.steps++ >= options.max_steps_per_row)
              fail (FailureKind::branch_limit);
            bytes (options, stats.retained_bytes, stack_bytes + b.bytes () + raw_bytes);
            auto id = b.work.back ();
            b.work.pop_back ();
            if (b.done[id])
              continue;
            b.done[id] = 1;  // only the CURRENT (root) obligation, never a descendant
            if (options.row_expansion == RowExpansion::symbolic_boolean && is_boolean[id]) {
              // Sound because expanding a Boolean obligation introduces no
              // future/postponed obligation: every satisfying alternative
              // shares the same temporal continuation, so their guards
              // combine as current_guard AND B(id) before continuing. No
              // witness branch, so no exponential blowup on e.g. a wide
              // conjunction of disjunctions. Never touches b.next/b.postponed.
              literals[id] ? ++stats.boolean_cache_hits : ++stats.boolean_cache_misses;
              b.guard = budget.guard (b.guard, boolean_guard (id, budget));
              continue;
            }
            auto f = closure[id];
            auto child = [&] (unsigned i) { return ids.at (f[i]); };
            switch (f.kind ()) {
              case Op::tt: break;
              case Op::ff: b.guard = bddfalse; break;
              case Op::ap:
              case Op::Not: b.guard = budget.guard (b.guard, *literals[id]); break;
              case Op::And:
                for (unsigned i = 0; i < f.size (); ++i)
                  b.work.push_back (child (i));
                break;
              case Op::Or:
                for (unsigned i = 1; i < f.size (); ++i) {
                  auto other = b;
                  other.work.push_back (child (i));
                  fork (std::move (other));
                }
                b.work.push_back (child (0));
                break;
              case Op::X: insert (b.next, child (0)); break;
              case Op::U: {
                auto other = b;
                other.work.push_back (child (0));
                insert (other.next, id);
                insert (other.postponed, id);
                fork (std::move (other));
                b.work.push_back (child (1));
                break;
              }
              case Op::R: {
                b.work.push_back (child (1));
                auto other = b;
                insert (other.next, id);
                fork (std::move (other));
                b.work.push_back (child (0));
                break;
              }
              default: fail (FailureKind::unexpected);
            }
          }
          if (b.guard == bddfalse) {
            ++stats.branches_pruned;
            continue;
          }
          auto key = std::make_pair (std::move (b.next), std::move (b.postponed));
          if (auto p = merged.find (key); p != merged.end ())
            p->second = budget.guard (p->second, b.guard, true);
          else {
            if (merged.size () >= options.max_edges_per_row)
              fail (FailureKind::row_limit);
            raw_bytes +=
                128 + sizeof (ClosureId) * (key.first.capacity () + key.second.capacity ());
            bytes (options, stats.retained_bytes, stack_bytes + b.bytes () + raw_bytes);
            merged.emplace (std::move (key), b.guard);
          }
        }
        std::vector<RawEdge> result;
        bytes (options, stats.retained_bytes, 2 * raw_bytes);
        result.reserve (merged.size ());
        for (const auto& [key, guard] : merged)
          result.push_back ({guard, key.first, key.second});
        return result;
      }

      Row row (StateId id) {
        check (options);
        if (id >= states.size ())
          fail (FailureKind::invalid_state);
        auto& state = states[id];
        if (state.row)
          return *state.row;
        if (stats.complete_rows >= options.max_rows)
          fail (FailureKind::row_limit);
        auto& base = underlying[state.underlying];
        RowBudget budget {*this};
        std::optional<std::vector<RawEdge>> raw;
        if (!base.row)
          raw = expand (base.pending, budget);
        const auto& source = base.row ? *base.row : *raw;
        std::size_t raw_bytes = 0;
        if (raw) {
          raw_bytes = raw->capacity () * sizeof (RawEdge);
          for (const auto& e : *raw)
            raw_bytes += (e.next.capacity () + e.postponed.capacity ()) * sizeof (ClosureId);
        }
        Timer timer {stats.cursor_row_ns};
        std::map<std::pair<StateId, bool>, bdd> merged;
        for (const auto& e : source) {
          check (options);
          const auto m = untils.size ();
          const bool satisfied =
              m == 0 ||
              !std::binary_search (e.postponed.begin (), e.postponed.end (), untils[state.j]);
          const auto next_j = m == 0 ? 0 : satisfied ? (state.j + 1) % m : state.j;
          const bool accepting = m == 0 || (satisfied && state.j == m - 1);
          auto dest = intern (e.next, next_j);  // discovery only
          const auto key = std::make_pair (dest, accepting);
          if (auto p = merged.find (key); p != merged.end ())
            p->second = budget.guard (p->second, e.guard, true);
          else
            merged.emplace (key, e.guard);
          bytes (options, stats.retained_bytes, raw_bytes + merged.size () * 128);
        }
        std::vector<Edge> complete;
        complete.reserve (merged.size ());
        for (const auto& [key, guard] : merged)
          complete.push_back ({key.first, guard, key.second});
        bytes (options, stats.retained_bytes,
               raw_bytes + merged.size () * 128 + complete.capacity () * sizeof (Edge));
        hit (options, FaultPoint::publication);
        // Nothing that allocates, calls a hook, or throws after this point.
        // Publish BOTH new caches together; a failed cursor build cannot leave
        // behind an apparently complete raw row from that failed request.
        if (raw) {
          base.row.emplace (std::move (*raw));
          ++stats.raw_rows;
        }
        stats.retained_bytes += raw_bytes + complete.capacity () * sizeof (Edge);
        stats.edges += complete.size ();
        state.row.emplace (std::move (complete));
        ++stats.complete_rows;
        if (stats.complete_rows == 1)
          stats.first_row_ns = elapsed (started);
        return *state.row;
      }
  };

  struct Provider::State final : spot::state {
      std::shared_ptr<Impl> context;
      StateId id;
      State (std::shared_ptr<Impl> c, StateId q) : context (std::move (c)), id (q) {}
      int compare (const spot::state* other) const override {
        const auto* rhs = dynamic_cast<const State*> (other);
        if (!rhs)
          throw AdapterFailure {{FailureKind::invalid_state}};
        if (context.get () != rhs->context.get ())
          return std::less<const Impl*> {}(context.get (), rhs->context.get ()) ? -1 : 1;
        return id < rhs->id ? -1 : id > rhs->id ? 1 : 0;
      }
      std::size_t hash () const override {
        auto h = std::hash<const Impl*> {}(context.get ());
        return h ^ (std::size_t (id) + 0x9e3779b9 + (h << 6) + (h >> 2));
      }
      State* clone () const override { return new State (*this); }
  };
  struct Provider::Iterator final : spot::twa_succ_iterator {
      std::shared_ptr<Impl> context;
      Row edges;
      std::size_t index = 0;
      Iterator (std::shared_ptr<Impl> c, Row r) : context (std::move (c)), edges (r) {}
      bool first () override {
        index = 0;
        return !done ();
      }
      bool next () override {
        ++index;
        return !done ();
      }
      bool done () const override { return index == edges.size (); }
      bdd cond () const override { return edges[index].condition; }
      spot::acc_cond::mark_t acc () const override {
        return edges[index].accepting ? spot::acc_cond::mark_t {0} : spot::acc_cond::mark_t {};
      }
      const spot::state* dst () const override {
        return new State (context, edges[index].destination);
      }
  };

  Provider::Provider (std::shared_ptr<Impl> impl) : twa (impl->dict), impl_ (std::move (impl)) {
    set_buchi ();
    prop_state_acc (false);
    for (auto ap : impl_->aps)
      register_ap (ap);
  }
  Provider::~Provider () {
    // twa deletes its iterator cache after derived members. Release it first
    // to avoid keeping a context/BDD registration alive past member teardown.
    delete iter_cache_;
    iter_cache_ = nullptr;
  }
  Result<std::shared_ptr<Provider>> Provider::create (Formula f, const spot::bdd_dict_ptr& dict,
                                                      Options options) noexcept {
    try {
      if (!dict || !f)
        fail (FailureKind::unsupported_operator);
      spot_letters::BuddyErrors errors;
      auto impl = std::make_shared<Impl> (dict, std::move (options));
      impl->initialize (f);
      auto result = std::shared_ptr<Provider> (new Provider (impl));
      impl->stats.factory_ns = elapsed (impl->started);
      return result;
    } catch (...) {
      return caught ();
    }
  }
  Result<Row> Provider::request_row (StateId id) const noexcept {
    try {
      spot_letters::BuddyErrors errors;
      return impl_->row (id);
    } catch (...) {
      return caught ();
    }
  }
  StateId Provider::initial_state () const noexcept { return 0; }
  std::size_t Provider::discovered_states () const noexcept { return impl_->states.size (); }
  std::size_t Provider::complete_rows () const noexcept { return impl_->stats.complete_rows; }
  bool Provider::is_complete (StateId id) const { return impl_->states.at (id).row.has_value (); }
  std::span<const ClosureId> Provider::obligations (StateId id) const {
    return impl_->underlying.at (impl_->states.at (id).underlying).pending;
  }
  std::size_t Provider::cursor (StateId id) const { return impl_->states.at (id).j; }
  std::span<const Formula> Provider::closure () const noexcept { return impl_->closure; }
  std::span<const ClosureId> Provider::untils () const noexcept { return impl_->untils; }
  Formula Provider::normalized_formula () const { return impl_->normalized; }
  const Counters& Provider::counters () const noexcept { return impl_->stats; }
  const spot::state* Provider::get_init_state () const { return state_from_id (0); }
  const spot::state* Provider::state_from_id (StateId id) const {
    if (id >= discovered_states ())
      throw AdapterFailure {{FailureKind::invalid_state}};
    return new State (impl_, id);
  }
  StateId Provider::state_id (const spot::state* state) const {
    auto s = dynamic_cast<const State*> (state);
    if (!s || s->context != impl_)
      throw AdapterFailure {{FailureKind::invalid_state}};
    return s->id;
  }
  spot::twa_succ_iterator* Provider::succ_iter (const spot::state* source) const {
    try {
      auto result = request_row (state_id (source));
      if (auto failure = std::get_if<Failure> (&result))
        throw AdapterFailure {*failure};
      return new Iterator (impl_, std::get<Row> (result));
    } catch (...) {
      throw AdapterFailure {caught ()};
    }
  }
  std::string Provider::format_state (const spot::state* state) const {
    const auto id = state_id (state);
    return std::to_string (id) + ":" + std::to_string (cursor (id));
  }
  Result<spot::twa_graph_ptr> Provider::materialize () const noexcept {
    try {
      spot_letters::BuddyErrors errors;
      auto graph = spot::make_twa_graph (get_dict ());
      graph->set_buchi ();
      graph->prop_state_acc (false);
      for (auto ap : ap ())
        graph->register_ap (ap);
      graph->new_state ();
      graph->set_init_state (0);
      for (std::size_t q = 0; q < discovered_states (); ++q) {
        auto result = request_row (static_cast<StateId> (q));
        if (auto failure = std::get_if<Failure> (&result))
          return *failure;
        while (graph->num_states () < discovered_states ())
          graph->new_state ();
        for (const auto& e : std::get<Row> (result)) {
          check (impl_->options);
          graph->new_edge (q, e.destination, e.condition,
                           e.accepting ? spot::acc_cond::mark_t {0} : spot::acc_cond::mark_t {});
        }
      }
      check (impl_->options);
      return graph;
    } catch (...) {
      return caught ();
    }
  }
}  // namespace acacia::closure_buchi
