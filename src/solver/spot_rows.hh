#pragma once

/// Deliberately explicit P2 successor oracle.  This neither constructs a game
/// nor translates/postprocesses an automaton.  Providers and their AP inventory
/// must remain immutable for the job; no destination row is requested on discovery.

#include "solver/spot_state_ids.hh"

#include <spot/twa/twagraph.hh>

#include <algorithm>
#include <deque>
#include <exception>
#include <optional>

namespace acacia::spot_rows {

  enum class IncrementMode { frozen_acacia, generic_transition_buchi };

  inline const char* mode_name (IncrementMode mode) {
    return mode == IncrementMode::frozen_acacia
               ? "frozen-acacia" : "generic-transition-buchi";
  }

  // Construction tags deliberately prevent inferring one convention from the
  // other. FrozenAcacia requires the exact post-preprocessing action boundary,
  // including its coordinate order and Boolean threshold. It is eager metadata.
  struct FrozenAcacia {
      spot::const_twa_graph_ptr graph;
      std::size_t bool_threshold;
  };
  struct GenericTransitionBuchi {
      spot::const_twa_ptr provider;
  };

  struct SpotEdge {
      StateId destination;
      bdd condition;
      spot::acc_cond::mark_t acceptance;
  };
  struct RankEdge {
      StateId destination;
      bdd condition;
      bool increment;
  };

  // Process-local fingerprint of ordered normalized edges (including BDD ids).
  // Never an equality proof, persistent key, or exploration-order heuristic.
  using RowDigest = std::uint64_t;
  struct CompleteRankRow {
      std::vector<RankEdge> edges;
      RowDigest digest = 14695981039346656037ULL;
      std::vector<SpotEdge> spot_edges;  // Keep the original marks for inspection.
  };

  enum class RowState { not_requested, building, complete, failed_or_resource_limited };
  enum class Status { complete, resource_limit, failed };

  inline const char* status_name (Status status) {
    switch (status) {
      case Status::complete: return "COMPLETE";
      case Status::resource_limit: return "RESOURCE_LIMIT";
      case Status::failed: return "FAILED";
    }
    return "FAILED";
  }

  struct RowLimits {
      std::size_t max_rows = 200000;
      std::size_t max_edges_per_row = 2000000;
  };
  struct RowResult {
      IncrementMode mode;
      Status status;
      const CompleteRankRow* row;  // Borrowed; non-null only for complete rows.
      std::exception_ptr error = {};
  };

  // Missing trailing coordinates mean -1. Evaluating never resizes the input,
  // so discovering provider states cannot mutate a previously stored rank key.
  using Rank = std::vector<std::int32_t>;
  struct RankResult {
      IncrementMode mode;
      Status status;
      std::optional<Rank> rank;  // No partial arithmetic result on failure.
      std::exception_ptr error = {};
  };

  class SpotRows {
    public:
      explicit SpotRows (FrozenAcacia view, RowLimits limits = {})
        : SpotRows (view.graph, IncrementMode::frozen_acacia, limits) {
        graph_ = view.graph.get ();
        if (not graph_->prop_state_acc ().is_true ())
          throw std::invalid_argument ("frozen-acacia requires state acceptance metadata");
        if (view.bool_threshold > graph_->num_states ())
          throw std::invalid_argument ("frozen-acacia Boolean threshold exceeds graph size");
        bool_threshold_ = view.bool_threshold;
        for (unsigned q = 0; q < graph_->num_states (); ++q)
          if (ids_.intern (graph_->state_from_number (q)->clone ()) != q)
            throw std::logic_error ("frozen-acacia graph coordinate mismatch");
        initial_ = ids_.intern (provider_->get_init_state ());
        check_contract ();
        entries_.resize (ids_.size ());
      }

      explicit SpotRows (GenericTransitionBuchi view, RowLimits limits = {})
        : SpotRows (std::move (view.provider), IncrementMode::generic_transition_buchi, limits) {
        initial_ = ids_.intern (provider_->get_init_state ());
        check_contract ();
        entries_.resize (ids_.size ());
      }

      SpotRows (const SpotRows&) = delete;
      SpotRows& operator= (const SpotRows&) = delete;
      SpotRows (SpotRows&&) = delete;
      SpotRows& operator= (SpotRows&&) = delete;

      IncrementMode mode () const { return mode_; }
      StateId initial_id () const { return initial_; }
      std::size_t state_count () const { return ids_.size (); }
      std::size_t complete_rows () const { return complete_rows_; }
      const spot::state* canonical_state (StateId id) const { return ids_[id]; }

      RowState state (StateId id) const {
        (void) ids_[id];
        return id < entries_.size () ? entries_[id].state : RowState::not_requested;
      }

      Rank initial_rank () const {
        Rank rank (state_count (), -1);
        rank[initial_] = 0;
        return rank;
      }

      Rank safe_caps (std::int32_t K) const {
        check_bound (K);
        Rank caps (state_count (), K - 1);
        if (mode_ == IncrementMode::frozen_acacia)
          std::fill (caps.begin () + bool_threshold_, caps.end (), 0);
        return caps;
      }

      bool is_safe (const Rank& rank, std::int32_t K) const {
        check_rank (rank, K);
        for (std::size_t q = 0; q < rank.size (); ++q) {
          const auto cap = mode_ == IncrementMode::frozen_acacia && q >= bool_threshold_
                               ? 0 : K - 1;
          if (rank[q] > cap)
            return false;
        }
        return true;
      }

      RowResult row (StateId source) {
        (void) ids_[source];
        try {
          entries_.resize (ids_.size ());
        }
        catch (const std::bad_alloc&) {
          return {mode_, Status::resource_limit, nullptr, std::current_exception ()};
        }
        catch (const std::length_error&) {
          return {mode_, Status::resource_limit, nullptr, std::current_exception ()};
        }
        catch (...) {
          return {mode_, Status::failed, nullptr, std::current_exception ()};
        }
        auto& entry = entries_[source];  // deque references survive destination discovery
        auto fail = [&] (Status status, std::exception_ptr error = {}) {
          if (entry.state == RowState::complete)
            --complete_rows_;
          entry.state = RowState::failed_or_resource_limited;
          entry.failure = status;
          entry.error = error;
          return RowResult {mode_, status, nullptr, error};
        };
        try {
          check_contract ();
          if (entry.state == RowState::complete)
            return {mode_, Status::complete, &*entry.row};
          if (entry.state == RowState::failed_or_resource_limited)
            return {mode_, entry.failure, nullptr, entry.error};
          if (entry.state == RowState::building)
            return {mode_, Status::failed, nullptr};
          entry.state = RowState::building;
          if (complete_rows_ >= limits_.max_rows)
            return fail (Status::resource_limit);
          CompleteRankRow temporary;
          {
            std::unique_ptr<spot::twa_succ_iterator, release_iterator> iter {
                provider_->succ_iter (ids_[source]), {provider_.get ()}};
            if (not iter)
              throw std::runtime_error ("Spot provider returned a null iterator");
            for (iter->first (); not iter->done (); iter->next ()) {
              if (temporary.edges.size () >= limits_.max_edges_per_row)
                return fail (Status::resource_limit);
              const bdd condition = iter->cond ();
              const auto acceptance = iter->acc ();
              if ((acceptance - acceptance_.all_sets ()) != spot::acc_cond::mark_t {})
                throw std::runtime_error ("Spot edge uses an undeclared acceptance set");
              const StateId destination = ids_.intern (iter->dst ());
              entries_.resize (ids_.size ());
              if (graph_ && destination >= graph_->num_states ())
                throw std::runtime_error ("frozen-acacia discovered an unknown graph state");
              // This is the legacy pair payload used by MONA/semantic-MONA,
              // regardless of ACACIA_TRANSITION_ACCEPTANCE. The iterator mark
              // belongs to the SOURCE in an SBA and is not this increment.
              const bool increment = graph_ ? graph_->state_is_accepting (destination)
                                            : acceptance.has (0);
              temporary.spot_edges.push_back ({destination, condition, acceptance});
              temporary.edges.push_back ({destination, condition, increment});
              for (const auto value : {RowDigest (destination), RowDigest (condition.id ()),
                                       RowDigest (increment)}) {
                temporary.digest ^= value;
                temporary.digest *= 1099511628211ULL;
              }
            }
          }  // release_iter() before publishing, including success and empty rows
          check_contract ();
          entry.row.emplace (std::move (temporary));
          entry.state = RowState::complete;
          ++complete_rows_;
          return {mode_, Status::complete, &*entry.row};
        }
        catch (const std::bad_alloc&) {
          return fail (Status::resource_limit, std::current_exception ());
        }
        catch (const std::length_error&) {
          return fail (Status::resource_limit, std::current_exception ());
        }
        catch (...) {
          return fail (Status::failed, std::current_exception ());
        }
      }

      /// Exact tau under one complete AP valuation. All active rows must finish
      /// before any max/min arithmetic; an absent edge contributes nothing.
      RankResult evaluate (const Rank& rank, bdd valuation, std::int32_t K) {
        try {
          check_contract ();
          check_rank (rank, K);
          if (valuation == bddfalse || bdd_support (valuation) != ap_vars_
              || bdd_satoneset (valuation, ap_vars_, bddfalse) != valuation)
            throw std::invalid_argument ("rank evaluator requires one complete AP valuation");
          for (std::size_t p = 0; p < rank.size (); ++p)
            if (rank[p] != -1) {
              const auto result = row (static_cast<StateId> (p));
              if (result.status != Status::complete)
                return {mode_, result.status, std::nullopt, result.error};
            }
          Rank successor (state_count (), -1);
          for (std::size_t p = 0; p < rank.size (); ++p)
            if (rank[p] != -1)
              for (const auto& edge : entries_[p].row->edges)
                if ((valuation & edge.condition) != bddfalse) {
                  const auto value = rank[p] == K ? K : rank[p] + int (edge.increment);
                  successor[edge.destination] = std::max (successor[edge.destination], value);
                }
          return {mode_, Status::complete, std::move (successor)};
        }
        catch (const std::bad_alloc&) {
          return {mode_, Status::resource_limit, std::nullopt, std::current_exception ()};
        }
        catch (...) {
          return {mode_, Status::failed, std::nullopt, std::current_exception ()};
        }
      }

    private:
      struct Entry {
          RowState state = RowState::not_requested;
          std::optional<CompleteRankRow> row;
          Status failure = Status::failed;
          std::exception_ptr error;
      };
      struct release_iterator {
          const spot::twa* provider;
          void operator() (spot::twa_succ_iterator* iter) const { provider->release_iter (iter); }
      };

      SpotRows (spot::const_twa_ptr provider, IncrementMode mode, RowLimits limits)
        : provider_ (std::move (provider)), ids_ (provider_), mode_ (mode), limits_ (limits),
          acceptance_ (provider_->acc ()), ap_vars_ (provider_->ap_vars ()) {
        // twa's abstract interface specifies nonalternating successors. Graphs
        // can additionally store raw alternation: reject those via their actual
        // representation API, never via the unrelated prop_universal().
        if (const auto* graph = dynamic_cast<const spot::twa_graph*> (provider_.get ());
            graph && not graph->is_existential ())
          throw std::invalid_argument ("Spot rows require a nonalternating view");
        if (not acceptance_.is_buchi () || acceptance_.num_sets () != 1)
          throw std::invalid_argument ("Spot rows require ordinary single-set Buchi acceptance");
      }

      void check_contract () const {
        if (provider_->acc () != acceptance_ || provider_->ap_vars () != ap_vars_)
          throw std::runtime_error ("Spot provider changed its fixed acceptance/AP inventory");
      }
      static void check_bound (std::int32_t K) {
        if (K < 1)
          throw std::invalid_argument ("rank bound K must be positive");
      }
      void check_rank (const Rank& rank, std::int32_t K) const {
        check_bound (K);
        if (rank.size () > state_count ()
            || (graph_ && rank.size () != state_count ()))
          throw std::invalid_argument ("rank has invalid coordinates for this mode");
        for (const auto value : rank)
          if (value < -1 || value > K)
            throw std::invalid_argument ("rank value is outside [-1,K]");
      }

      // Destruction is explicitly reversed: cached guards, AP cube, metadata,
      // canonical states, provider. Holding the provider also retains its BDD
      // dictionary and AP registrations; iterators never escape row().
      spot::const_twa_ptr provider_;
      SpotStateIds ids_;
      IncrementMode mode_;
      RowLimits limits_;
      spot::acc_cond acceptance_;
      bdd ap_vars_;
      const spot::twa_graph* graph_ = nullptr;  // borrowed from provider_, frozen only
      std::size_t bool_threshold_ = 0;
      StateId initial_ = 0;
      std::size_t complete_rows_ = 0;
      std::deque<Entry> entries_;
  };

}  // namespace acacia::spot_rows
