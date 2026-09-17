#pragma once

#include "solver/spot_state_ids.hh"

#include <exception>
#include <functional>
#include <span>
#include <spot/twa/twagraph.hh>
#include <variant>

namespace acacia::closure_buchi {
  using StateId = spot_rows::StateId;
  using ClosureId = std::uint32_t;

  enum class FailureKind {
    unsupported_operator,
    normalization_limit,
    branch_limit,
    guard_limit,
    state_limit,
    row_limit,
    memory_limit,
    cancelled,
    injected,
    invalid_state,
    unexpected
  };
  struct Failure {
      FailureKind kind;
      std::exception_ptr error = {};
  };
  template <typename T>
  using Result = std::variant<T, Failure>;

  enum class FaultPoint { branch, guard, interning, publication };
  // Optional per-provider test hooks. Changing the hook permits a failed row
  // to be retried; neither failures nor partial rows are cached.
  struct Hooks {
      std::function<bool ()> cancelled;
      std::function<bool (FaultPoint)> fail;
  };
  struct Options {
      std::size_t max_normalization_nodes = 100000;
      std::size_t max_normalization_depth = 512;
      std::size_t max_states = 200000;  // each of the underlying/cursor arenas
      std::size_t max_rows = 200000;
      std::size_t max_branches_per_row = 2000000;
      std::size_t max_steps_per_row = 10000000;
      std::size_t max_guards_per_row = 2000000;
      std::size_t max_edges_per_row = 2000000;
      std::size_t max_live_bdd_nodes = std::numeric_limits<std::size_t>::max ();
      // Estimated live provider + scratch storage, excluding the shared BDD
      // manager (limited above). Allocation failures are caught independently.
      std::size_t max_bytes = std::numeric_limits<std::size_t>::max ();
      std::shared_ptr<Hooks> hooks;
  };
  struct Edge {
      StateId destination;
      bdd condition;
      bool accepting;
  };
  using Row = std::span<const Edge>;
  struct Counters {
      std::uint64_t normalization_ns = 0, factory_ns = 0;
      std::uint64_t raw_row_ns = 0, cursor_row_ns = 0, first_row_ns = 0;
      std::uint64_t branches_considered = 0, branches_pruned = 0, guards_generated = 0;
      std::size_t states_discovered = 0, complete_rows = 0, raw_rows = 0, edges = 0;
      std::size_t retained_bytes = 0;
  };

  // twa has no typed failure channel. Its adapter throws this exception before
  // returning an iterator on failure; checked callers must map it to UNKNOWN.
  class AdapterFailure final : public std::exception {
    public:
      explicit AdapterFailure (Failure failure) noexcept : failure_ (std::move (failure)) {}
      const char* what () const noexcept override { return "closure Buchi provider: UNKNOWN"; }
      const Failure& failure () const noexcept { return failure_; }

    private:
      Failure failure_;
  };

  // Single-worker/thread use, like the shared BuDDy manager. Rows and structural
  // spans remain valid for the provider lifetime, including after discoveries.
  class Provider final : public spot::twa {
    public:
      static Result<std::shared_ptr<Provider>> create (spot::formula worker_formula,
                                                       const spot::bdd_dict_ptr& dict,
                                                       Options options = {}) noexcept;
      ~Provider () override;
      Provider (const Provider&) = delete;
      Provider& operator= (const Provider&) = delete;

      Result<Row> request_row (StateId id) const noexcept;
      StateId initial_state () const noexcept;
      std::size_t discovered_states () const noexcept;
      std::size_t complete_rows () const noexcept;
      bool is_complete (StateId id) const;
      std::span<const ClosureId> obligations (StateId id) const;
      std::size_t cursor (StateId id) const;
      std::span<const spot::formula> closure () const noexcept;
      std::span<const ClosureId> untils () const noexcept;
      spot::formula normalized_formula () const;
      const Counters& counters () const noexcept;

      // The only eager exploration policy; graph IDs equal provider IDs.
      // Failure returns no partial graph (already completed rows remain valid).
      Result<spot::twa_graph_ptr> materialize () const noexcept;

      const spot::state* get_init_state () const override;
      const spot::state* state_from_id (StateId id) const;
      StateId state_id (const spot::state* state) const;
      spot::twa_succ_iterator* succ_iter (const spot::state* source) const override;
      std::string format_state (const spot::state* state) const override;

    private:
      struct Impl;
      struct State;
      struct Iterator;
      explicit Provider (std::shared_ptr<Impl> impl);
      std::shared_ptr<Impl> impl_;
  };
}  // namespace acacia::closure_buchi
