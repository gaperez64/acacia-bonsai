#pragma once

#include "solver/spot_letter_oracle.hh"

#include <concepts>
#include <functional>

namespace acacia::spot_rows {
  // This is an Acacia value type, not a Posets container. In C4/C5 every
  // coordinate is numeric. boolean_states::forward_saturation materializes
  // states, mutates the automaton in place AND sets the global
  // posets::vectors::bool_threshold defining the safe envelope. It cannot run
  // here: the lazy path is structurally Boolean-state-free, unlike C0/C3.
  template <std::signed_integral Value = std::int32_t>
    requires (sizeof (Value) <= sizeof (std::int32_t))
  class SparseForwardRank {
    public:
      using Entry = std::pair<StateId, Value>;
      explicit SparseForwardRank (std::vector<Entry> entries, Value K) {
        check_bound (K);
        for (const auto& [q, value] : entries) {
          (void) q;
          if (value < -1 || value > K) throw std::invalid_argument ("sparse rank outside [-1,K]");
        }
        std::sort (entries.begin (), entries.end ());
        for (const auto& [q, value] : entries) {
          if (value == -1) continue;
          if (not entries_.empty () && entries_.back ().first == q)
            entries_.back ().second = std::max (entries_.back ().second, value);
          else entries_.emplace_back (q, value);
        }
        // Hash of the normalized entries, computed once here because
        // entries_ cannot change afterwards.  hash() used to recompute this
        // chain on every call, and both Search::interned_ and Oracle::prepared_
        // are unordered_map<Rank, ...>, so every lookup re-hashed the vector.
        for (const auto& [q, value] : entries_)
          for (const auto v : {std::size_t (q), std::size_t (value)})
            hash_ ^= v + std::size_t (0x9e3779b9) + (hash_ << 6) + (hash_ >> 2);
      }
      const std::vector<Entry>& entries () const { return entries_; }
      Value at (StateId q) const {
        const auto it = std::lower_bound (entries_.begin (), entries_.end (), q,
                                         [] (const Entry& e, StateId id) { return e.first < id; });
        return it != entries_.end () && it->first == q ? it->second : Value (-1);
      }
      bool operator== (const SparseForwardRank& rhs) const { return entries_ == rhs.entries_; }
      bool leq (const SparseForwardRank& rhs) const {
        auto right = rhs.entries_.begin ();
        for (const auto& [q, value] : entries_) {
          while (right != rhs.entries_.end () && right->first < q) ++right;
          if (right == rhs.entries_.end () || right->first != q || value > right->second) return false;
        }
        return true;
      }
      std::size_t hash () const { return hash_; }
      bool is_safe (Value K) const {
        check_bound (K);
        bool safe = true;
        for (const auto& [q, value] : entries_) {
          (void) q;
          if (value > K) throw std::invalid_argument ("sparse rank exceeds this bound");
          safe &= value < K;
        }
        return safe;
      }
      static Value increment (Value value, bool accepting, Value K) {
        check_bound (K);
        if (value < -1 || value > K) throw std::invalid_argument ("invalid sparse contribution");
        if (value == -1) return -1;
        // Widen BEFORE addition: byte 127 + 1 and int32 max + 1 are safe.
        return static_cast<Value> (std::min (std::int64_t (K), std::int64_t (value) + int (accepting)));
      }
    private:
      static void check_bound (Value K) {
        if (K < 1) throw std::invalid_argument ("sparse rank bound must be positive");
      }
      // No mutable entries, arena pointer/size, Boolean threshold, or bound in
      // the key. A value can be assigned, but its stored pairs cannot be edited.
      std::vector<Entry> entries_;
      // Derived from entries_ by the constructor, which is the only writer.
      // Defaulted copy and move carry it; a moved-from rank keeps a hash for
      // entries it no longer owns, which no caller observes because interning
      // copies into the map before moving into the node (spot_lazy_game.hh
      // intern()) and never reads the source again.
      std::size_t hash_ = 0;
  };

  // Exact P2 row arithmetic with sparse keys and P3's checked BDD operations.
  // All active rows complete before arithmetic; discovered destinations alone
  // never request their rows. There is no whole-arena scan or dense allocation.
  template <typename Value>
  spot_letters::Result<SparseForwardRank<Value>> evaluate_sparse (
      const std::shared_ptr<SpotRows>& rows, const spot_letters::WorkerAlphabet& alphabet,
      const SparseForwardRank<Value>& rank, bdd valuation, Value K,
      spot_letters::QueryLimits limits = {}) {
    spot_letters::Oracle boundary {rows, alphabet, K};
    boundary.set_limits (limits);
    return boundary.template query<SparseForwardRank<Value>> ([&] (auto& b) {
      if (rows->mode () != IncrementMode::generic_transition_buchi)
        throw spot_letters::detail::Failure {spot_letters::Unknown::invalid_query};
      (void) rank.is_safe (K);
      (void) b.restrict_total (bddtrue, valuation, spot_letters::Variables::all);
      std::vector<std::pair<Value, const CompleteRankRow*>> active;
      for (const auto& [q, value] : rank.entries ()) {
        b.step ();
        if (q >= rows->state_count ()) throw spot_letters::detail::Failure {spot_letters::Unknown::invalid_query};
        const auto r = rows->row (q);
        if (r.status != Status::complete || not r.row)
          throw spot_letters::detail::Failure {r.status == Status::resource_limit
              ? spot_letters::Unknown::resource_limit : spot_letters::Unknown::row_failure, r.error};
        active.emplace_back (value, r.row);
      }
      std::vector<typename SparseForwardRank<Value>::Entry> result;
      for (const auto& [value, row] : active)
        for (const auto& edge : row->edges) {
          b.require_support (edge.condition, alphabet.ap_vars);
          if (b.land (valuation, edge.condition) != bddfalse)
            result.emplace_back (edge.destination, SparseForwardRank<Value>::increment (value, edge.increment, K));
        }
      return SparseForwardRank<Value> {std::move (result), K};
    });
  }
} // namespace acacia::spot_rows

template <typename Value> struct std::hash<acacia::spot_rows::SparseForwardRank<Value>> {
    std::size_t operator() (const acacia::spot_rows::SparseForwardRank<Value>& rank) const {
      return rank.hash ();
    }
};
