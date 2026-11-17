#pragma once

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
  namespace detail {
    using Clock = std::chrono::steady_clock;
    using Formula = spot::formula;
    using Op = spot::op;
    using Set = std::vector<ClosureId>;
    inline std::uint64_t elapsed (Clock::time_point start) {
      return std::chrono::duration_cast<std::chrono::nanoseconds> (Clock::now () - start).count ();
    }
    struct Timer {
        std::uint64_t& ns;
        Clock::time_point start = Clock::now ();
        ~Timer () { ns += elapsed (start); }
    };
    [[noreturn]] inline void fail (FailureKind kind) { throw Failure {kind}; }
    inline Failure caught () noexcept {
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
    inline void check (const Options& o) {
      if (o.hooks && o.hooks->cancelled && o.hooks->cancelled ())
        fail (FailureKind::cancelled);
      if (static_cast<std::size_t> (bdd_getnodenum ()) > o.max_live_bdd_nodes)
        fail (FailureKind::guard_limit);
      if (o.hooks && o.hooks->memory_exhausted && o.hooks->memory_exhausted ())
        fail (FailureKind::memory_limit);
    }
    inline void hit (const Options& o, FaultPoint point) {
      check (o);
      if (o.hooks && o.hooks->fail && o.hooks->fail (point))
        fail (FailureKind::injected);
    }
    inline void bytes (const Options& o, std::size_t retained, std::size_t temporary = 0) {
      if (retained > o.max_bytes || temporary > o.max_bytes - retained)
        fail (FailureKind::memory_limit);
    }
    inline void insert (Set& s, ClosureId id) {
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
        std::size_t* retained_;
        std::size_t charged_ = 0;
        void check_bytes () {
          const auto estimate =
              128 * (generated_.size () + memo_.size ()) + sizeof (Formula) * arcs_;
          bytes (options_, retained_ ? *retained_ : 0, estimate - charged_);
          if (retained_) {
            *retained_ += estimate - charged_;
            charged_ = estimate;
          }
        }
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
            check_bytes ();
            for (auto c : a)
              work.push_back (c);
          }
          return f;
        }

      public:
        explicit Normalizer (const Options& o, std::size_t* retained = nullptr)
            : options_ (o), retained_ (retained) {}
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
          check_bytes ();
          return result;
        }
    };
  }

}
