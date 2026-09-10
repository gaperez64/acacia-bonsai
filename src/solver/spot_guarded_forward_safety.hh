#pragma once

// P4 / C3: dense ranks on the frozen Acacia graph. No action table, input
// classes, sparse coordinates, or conditional downward covering are used.
#include "solver/certificate_verifier.hh"
#include "solver/forward_game_nodes.hh"
#include "solver/minimal_losing_antichain.hh"
#include "solver/spot_letter_oracle.hh"
#include "solver/spot_worker_record.hh"

#include <chrono>

namespace acacia::spot_guarded {
  using spot_rows::Rank;
  using spot_letters::Oracle;
  using spot_letters::Unknown;
  using spot_letters::Variables;
  using solver_detail::forward_result_status;
  using solver_detail::losing_reason;
  using RankNodeId = std::size_t;
  using TotalOutputValuation = bdd;  // Checked total output cube, constant on C.
  struct ChoiceRef {
      RankNodeId source;
      std::size_t choice;
  };
  struct GuardedChoice {
      bdd input_region;
      TotalOutputValuation output;
      RankNodeId successor;
      bool active;
  };
  struct GuardedRankNode {
      Rank rank;
      bool active_rows_complete = false;
      bool losing = false;
      std::vector<GuardedChoice> choices;
      bdd covered_inputs = bddfalse;
      std::vector<ChoiceRef> incoming;
      bool queued = false;
  };
  struct RowIdentity {
      spot_rows::StateId source;
      spot_rows::RowDigest digest;
      bool operator== (const RowIdentity&) const = default;
  };
  struct GuardedLosingProof {
      solver_detail::losing_proof record;
      Rank rank;
      std::optional<bdd> input;
      // Row-independent UNSAFE/SUBSUMPTION rules have no row obligations.
      std::vector<RowIdentity> rows;
  };
  struct Limits {
      std::size_t max_rank_nodes = 200000;
      std::size_t max_choices = 2000000;
      std::size_t max_expansions = std::numeric_limits<std::size_t>::max ();
      spot_rows::RowLimits rows;
      spot_letters::QueryLimits queries;
      spot_rows::RowLimits verifier_rows;
      spot_letters::QueryLimits verifier_queries;
  };
  struct SolveResult {
      // Certificates contain BDD guards; retain their AP registrations until
      // after every guard is destroyed (members are destroyed in reverse).
      spot::const_twa_graph_ptr provider;
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
    using Failure = spot_letters::detail::Failure;
    // These checks remain live under NDEBUG, including the test build.
    inline void require (bool condition) {
      if (not condition) throw Failure {Unknown::invalid_query};
    }
    template <typename T> T take (spot_letters::Result<T> result) {
      if (not result.value) throw Failure {result.unknown, result.error};
      return std::move (*result.value);
    }
    inline void row_ok (spot_rows::Status status) {
      if (status != spot_rows::Status::complete)
        throw Failure {status == spot_rows::Status::resource_limit
                           ? Unknown::resource_limit : Unknown::row_failure};
    }
    inline std::vector<RowIdentity> complete_rows (
        spot_rows::SpotRows& rows, Oracle& oracle, const Rank& rank) {
      return take (oracle.query<std::vector<RowIdentity>> ([&] (auto& b) {
        std::vector<RowIdentity> result;
        for (std::size_t p = 0; p < rank.size (); ++p) {
          b.step ();
          if (rank[p] == -1) continue;
          const auto row = rows.row (static_cast<spot_rows::StateId> (p));
          row_ok (row.status);
          require (row.row != nullptr);
          result.push_back ({static_cast<spot_rows::StateId> (p), row.row->digest});
        }
        return result;
      }));
    }
    struct RankLeq {
        bool operator() (const Rank& a, const Rank& b) const { return Oracle::leq (a, b); }
    };
    template <typename T, typename F> spot_letters::Result<T> checked (F&& f) {
      spot_letters::BuddyErrors errors;
      try { return {f (), Unknown::none, {}}; }
      catch (const Failure& e) { return {std::nullopt, e.why, e.error}; }
      catch (const std::bad_alloc&) { return {std::nullopt, Unknown::resource_limit, std::current_exception ()}; }
      catch (const std::length_error&) { return {std::nullopt, Unknown::resource_limit, std::current_exception ()}; }
      catch (...) { return {std::nullopt, Unknown::invalid_query, std::current_exception ()}; }
    }
    inline double elapsed (std::chrono::steady_clock::time_point start) {
      return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - start).count ();
    }
  }

  // Like certificate_verifier.hh's action-table verifier, rebuild semantic
  // images from the game. Fresh P2 rows AND a fresh P3 oracle ensure search
  // flags, cached predicates, row digests, and selected ids cannot certify
  // themselves. The frozen graph/AP inventory must remain immutable.
  inline spot_letters::Result<std::vector<Rank>> verify_winning_certificate (
      spot_rows::FrozenAcacia view, const spot_letters::WorkerAlphabet& alphabet,
      std::int32_t K, const SolveResult& certificate, const Limits& limits = {}) {
    return detail::checked<std::vector<Rank>> ([&] {
      auto rows = std::make_shared<spot_rows::SpotRows> (view, limits.verifier_rows);
      Oracle oracle {rows, alphabet, K};
      oracle.set_limits (limits.verifier_queries);
      detail::require (certificate.failure == Unknown::none && not certificate.pending_loss
                       && not certificate.pending_expansion && certificate.initial < certificate.nodes.size ());
      detail::require (certificate.nodes[certificate.initial].rank == rows->initial_rank ());
      std::vector<bool> seen (certificate.nodes.size (), false);
      solver_detail::forward_work_queue<RankNodeId> todo {certificate.initial};
      std::vector<Rank> reached;
      while (not todo.empty ()) {
        const auto id = todo.front ();
        todo.pop_front ();
        if (seen[id]) continue;
        seen[id] = true;
        const auto& node = certificate.nodes[id];
        detail::require (not node.losing && not node.queued && node.active_rows_complete
                         && rows->is_safe (node.rank, K));
        (void) detail::complete_rows (*rows, oracle, node.rank);
        bdd covered = bddfalse;
        for (const auto& choice : node.choices) {
          if (not choice.active) continue;
          detail::require (choice.successor < certificate.nodes.size ());
          const auto& target = certificate.nodes[choice.successor];
          detail::require (not target.losing && rows->is_safe (target.rank, K));
          const bdd eq = detail::take (oracle.eq (node.rank, target.rank));
          const bdd exact = detail::take (oracle.restrict_total (eq, choice.output, Variables::outputs));
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
          for (const auto& g : result) { b.step (); dominated |= Oracle::leq (r, g); }
          if (dominated) continue;
          std::erase_if (result, [&] (const auto& g) { b.step (); return Oracle::leq (g, r); });
          result.push_back (r);
        }
        return result;
      }));
      // A second obligation: forall u exists c Good at EVERY maximal generator.
      detail::require (detail::take (oracle.invariant (rows->initial_rank (), generators, 0))
                       == spot_letters::Invariant::verified);
      return generators;
    });
  }

  inline spot_letters::Result<bool> verify_losing_proof (
      spot_rows::FrozenAcacia view, const spot_letters::WorkerAlphabet& alphabet,
      std::int32_t K, const SolveResult& certificate, const Limits& limits = {}) {
    return detail::checked<bool> ([&] {
      auto rows = std::make_shared<spot_rows::SpotRows> (view, limits.verifier_rows);
      Oracle oracle {rows, alphabet, K};
      oracle.set_limits (limits.verifier_queries);
      detail::require (certificate.failure == Unknown::none && not certificate.pending_loss
                       && certificate.initial_proof && *certificate.initial_proof < certificate.proofs.size ());
      for (std::size_t id = 0; id < certificate.proofs.size (); ++id) {
        const auto& proof = certificate.proofs[id];
        const auto& record = proof.record;
        detail::take (oracle.query<bool> ([&] (auto& b) {
          detail::require (record.id == id && record.node < certificate.nodes.size ()
                           && proof.rank == certificate.nodes[record.node].rank);
          for (const auto dep : record.dependencies) {
            b.step ();
            detail::require (dep < id);  // chronological IDs prove acyclicity
          }
          return true;
        }));
        const bool safe = rows->is_safe (proof.rank, K);  // also validates dense domain
        switch (record.reason) {
          case losing_reason::env_unsafe:
            detail::require (not safe && record.dependencies.empty () && proof.rows.empty () && not proof.input);
            break;
          case losing_reason::env_subsumed:
            detail::require (record.dependencies.size () == 1 && proof.rows.empty () && not proof.input);
            detail::require (Oracle::leq (certificate.proofs[record.dependencies[0]].rank, proof.rank));
            break;
          case losing_reason::env_losing_input: {
            detail::require (safe && proof.input.has_value ());
            detail::require (detail::complete_rows (*rows, oracle, proof.rank) == proof.rows);
            std::vector<Rank> earlier;
            for (const auto dep : record.dependencies) earlier.push_back (certificate.proofs[dep].rank);
            const bdd bad = detail::take (oracle.bad (proof.rank, earlier, id));
            const bdd all_outputs = detail::take (oracle.restrict_total (bad, *proof.input, Variables::inputs));
            detail::require (all_outputs == bddtrue);
            break;
          }
          default: detail::require (false);
        }
      }
      const auto& root = certificate.proofs[*certificate.initial_proof];
      detail::require (root.record.node == certificate.initial && root.rank == rows->initial_rank ());
      return true;
    });
  }

  class Search {
    public:
      Search (spot_rows::FrozenAcacia view, spot_letters::WorkerAlphabet alphabet,
              std::int32_t K, Limits limits = {})
        : view_ (std::move (view)), alphabet_ (std::move (alphabet)), K_ (K), limits_ (limits),
          rows_ (std::make_shared<spot_rows::SpotRows> (view_, limits.rows)), oracle_ (rows_, alphabet_, K) {
        result_.provider = view_.graph;
        oracle_.set_limits (limits_.queries);
      }
      SolveResult solve () {
        const auto started = std::chrono::steady_clock::now ();
        const auto search = detail::checked<bool> ([&] {
          result_.initial = intern (rows_->initial_rank ());
          for (;;) {
            propagate_losses ();
            if (result_.nodes[result_.initial].losing || open_.empty ()) return true;
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
        if (spot_records::active) {
          const auto& m = oracle_.metrics ();
          spot_records::put ("search_bdd_operations", std::to_string (m.bdd_operations));
          spot_records::put ("search_peak_live_nodes", std::to_string (m.peak_live_nodes));
          spot_records::put ("search_peak_result_nodes", std::to_string (m.peak_result_nodes));
          spot_records::put ("search_rows_generated", std::to_string (rows_->complete_rows ()));
        }
        result_.pending_loss = not losses_.empty ();
        result_.pending_expansion = not open_.empty ();
        if (not search.value) {
          result_.failure = search.unknown;
          result_.status = search.unknown == Unknown::resource_limit
                               ? forward_result_status::resource_limit : forward_result_status::unknown;
          return std::move (result_);
        }
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
            result_.status = forward_result_status::unknown;  // verifier resource failure is UNKNOWN
            result_.failure = verified.unknown;
          }
        }
        result_.verify_ms = detail::elapsed (verifying);
        return std::move (result_);
      }
    private:
      spot_rows::FrozenAcacia view_;
      spot_letters::WorkerAlphabet alphabet_;
      std::int32_t K_;
      Limits limits_;
      std::shared_ptr<spot_rows::SpotRows> rows_;
      Oracle oracle_;
      SolveResult result_;
      std::map<Rank, RankNodeId> interned_;
      solver_detail::forward_work_queue<RankNodeId> open_, losses_;
      solver_detail::minimal_losing_antichain<Rank, detail::RankLeq> losing_;
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
        if (found != interned_.end ()) return found->second;
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
        for (const auto id : generators_) ranks.push_back (result_.proofs[id].rank);
        return ranks;
      }
      std::optional<std::size_t> subsumer (const Rank& r) const {
        if (not losing_.subsumes (r)) return std::nullopt;
        for (const auto id : generators_)
          if (Oracle::leq (result_.proofs[id].rank, r)) return id;
        detail::require (false);
        return std::nullopt;
      }
      void enqueue_loss (RankNodeId id, losing_reason reason,
                         std::vector<std::size_t> deps = {}, std::optional<bdd> input = {},
                         std::vector<RowIdentity> rows = {}) {
        auto& node = result_.nodes[id];
        if (node.losing) return;
        const auto proof_id = result_.proofs.size ();
        for (const auto dep : deps) detail::require (dep < proof_id);
        result_.proofs.push_back ({{proof_id, reason, id, 0, std::move (deps)},
                                  node.rank, input, std::move (rows)});
        node.losing = true;
        if (id == result_.initial) result_.initial_proof = proof_id;
        if (losing_.insert (node.rank)) {
          std::erase_if (generators_, [&] (auto old) { return Oracle::leq (node.rank, result_.proofs[old].rank); });
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
            if (result_.nodes[choice.successor].losing) choice.active = false;
            if (choice.active) covered = b.lor (covered, choice.input_region);
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
        // OTF-AND-SPOT.md handoff 7.4, in order. Copy the rank: interning s may grow nodes.
        if (result_.nodes[id].losing) return;
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
        if (result_.nodes[id].covered_inputs == bddtrue) return;
        const bdd missing = detail::take (oracle_.query<bdd> ([&] (auto& b) {
          return b.negate (result_.nodes[id].covered_inputs);
        }));
        const auto input = detail::take (oracle_.model (missing, Variables::inputs));
        detail::require (input.has_value ());
        const bdd bad = detail::take (oracle_.bad (rank, losing_ranks (), result_.proofs.size ()));
        const bdd bad_c = detail::take (oracle_.restrict_total (bad, *input, Variables::inputs));
        if (bad_c == bddtrue) {
          enqueue_loss (id, losing_reason::env_losing_input, generators_, input, std::move (row_ids));
          return;
        }
        const bdd available = detail::take (oracle_.query<bdd> ([&] (auto& b) { return b.negate (bad_c); }));
        const auto output = detail::take (oracle_.model (available, Variables::outputs));
        detail::require (output.has_value ());
        const bdd letter = detail::take (oracle_.query<bdd> ([&] (auto& b) { return b.land (*input, *output); }));
        auto successor = rows_->evaluate (rank, letter, K_);
        detail::row_ok (successor.status);
        detail::require (successor.rank.has_value () && rows_->is_safe (*successor.rank, K_)
                         && not losing_.subsumes (*successor.rank));
        const bdd eq = detail::take (oracle_.eq (rank, *successor.rank));
        const bdd exact = detail::take (oracle_.restrict_total (eq, *output, Variables::outputs));
        const bdd region = detail::take (oracle_.query<bdd> ([&] (auto& b) {
          const bdd C = b.land (missing, exact);
          b.require_support (C, alphabet_.inputs);
          detail::require (b.restrict_total (C, *input, Variables::inputs) == bddtrue);
          // Real runtime checks: every choice adds a previously uncovered input.
          detail::require (b.satisfiable (C) && b.land (C, result_.nodes[id].covered_inputs) == bddfalse);
          return C;
        }));
        if (result_.choices_created >= limits_.max_choices)
          throw detail::Failure {Unknown::resource_limit};
        const auto sid = intern (std::move (*successor.rank));
        detail::require (not result_.nodes[sid].losing);
        auto& source = result_.nodes[id];
        const auto choice_id = source.choices.size ();
        source.choices.push_back ({region, *output, sid, true});
        result_.nodes[sid].incoming.push_back ({id, choice_id});
        ++result_.choices_created;
        source.covered_inputs = detail::take (oracle_.query<bdd> ([&] (auto& b) { return b.lor (source.covered_inputs, region); }));
        enqueue (sid);
        if (source.covered_inputs != bddtrue) enqueue (id);
      }
  };

  inline SolveResult solve (spot_rows::FrozenAcacia view, spot_letters::WorkerAlphabet alphabet,
                            std::int32_t K, Limits limits = {}) {
    const auto started = std::chrono::steady_clock::now ();
    double prep_ms = 0;
    auto outcome = detail::checked<SolveResult> ([&] {
      Search search {std::move (view), std::move (alphabet), K, limits};
      prep_ms = detail::elapsed (started);
      return search.solve ();
    });
    if (outcome.value) {
      outcome.value->prep_ms = prep_ms;
      return std::move (*outcome.value);
    }
    SolveResult result;
    result.failure = outcome.unknown;
    result.status = outcome.unknown == Unknown::resource_limit ? forward_result_status::resource_limit
                                                             : forward_result_status::unknown;
    result.prep_ms = detail::elapsed (started);
    return result;
  }
}  // namespace acacia::spot_guarded
