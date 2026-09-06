#pragma once

// P5, otf.md 8.4: one acceptance obligation per transition, never skip levels.
// Use with SpotRows::GenericTransitionBuchi; initial rank and cursor are zero.
#include "solver/spot_letter_oracle.hh"

#include <spot/twaalgos/ltl2taa.hh>

#include <functional>
#include <set>
#include <type_traits>

namespace acacia::spot_lazy {
  using spot_rows::StateId;

  enum class Status { accepted, unknown, declined };
  template <typename T> struct Result {
      std::optional<T> value;
      Status status;
      std::exception_ptr error = {};
  };
  struct Declined : std::runtime_error { using std::runtime_error::runtime_error; };
  struct ResourceLimit : std::length_error { using std::length_error::length_error; };

  struct Limits {
      unsigned max_acceptance_sets = SPOT_MAX_ACCSETS;
      // Cap each of the underlying and cursor-state arenas, including initial.
      std::size_t max_states = 200000;
      std::size_t max_live_bdd_nodes = std::numeric_limits<std::size_t>::max ();
      spot_rows::RowLimits rows;
  };

  // No partial value on failure. Native BuDDy exhaustion exits inconclusively
  // through P3's hook; a worker parent must treat exit 2/signals as UNKNOWN.
  template <typename F> auto attempt (F&& f) {
    using T = std::invoke_result_t<F>;
    spot_letters::BuddyErrors errors;
    try { return Result<T> {f (), Status::accepted}; }
    catch (const Declined&) { return Result<T> {{}, Status::declined, std::current_exception ()}; }
    // Includes bad_alloc/length_error and Spot's native acceptance-capacity
    // runtime_error (acc_cond::report_too_many_sets), without parsing messages.
    // Unexpected provider errors never establish an unsupported-language claim.
    catch (...) { return Result<T> {{}, Status::unknown, std::current_exception ()}; }
  }

  namespace detail {
    inline void bdd_budget (const Limits& limits) {
      if (static_cast<std::size_t> (bdd_getnodenum ()) > limits.max_live_bdd_nodes)
        throw ResourceLimit ("lazy Buchi BDD budget exceeded");
    }

    struct Context {
        spot::const_twa_ptr provider;
        spot_rows::SpotStateIds ids;
        const spot::acc_cond acceptance;
        const bdd ap_vars;
        const Limits limits;
        StateId initial = 0;
        std::map<StateId, std::vector<spot_rows::SpotEdge>> rows;
        std::set<std::pair<StateId, unsigned>> cursors;
        mutable std::exception_ptr failure;

        Context (spot::const_twa_ptr p, Limits l)
          : provider (std::move (p)), ids (provider), acceptance (provider->acc ()),
            ap_vars (provider->ap_vars ()), limits (l) {
          if (acceptance.num_sets () > limits.max_acceptance_sets)
            throw ResourceLimit ("lazy Buchi acceptance budget exceeded");
          // is_generalized_buchi inspects only acc_code, requiring precisely
          // Inf(0)&...&Inf(m-1). Explicit false is supported with no accepting
          // edges, including when declared (unused) sets exist.
          if (not acceptance.is_f () && not acceptance.is_generalized_buchi ())
            throw Declined ("lazy Buchi requires a conjunction of all Inf sets or false");
          check ();
          initial = intern (provider->get_init_state ());
          cursor (initial, 0);
          check ();
        }

        void check () const {
          if (failure) std::rethrow_exception (failure);
          try {
            if (provider->acc () != acceptance || provider->ap_vars () != ap_vars)
              throw Declined ("lazy provider changed its fixed acceptance/AP inventory");
            // The generic twa contract is nonalternating. Only twa_graph can
            // expose raw universal destinations; is_existential is a storage
            // flag, not an SCC pass (prop_universal means something else).
            if (const auto* g = dynamic_cast<const spot::twa_graph*> (provider.get ());
                g && not g->is_existential ())
              throw Declined ("lazy Buchi requires nonalternating successors");
            bdd_budget (limits);
          }
          catch (...) { failure = std::current_exception (); throw; }
        }

        StateId intern (const spot::state* owned) {
          const auto id = ids.intern (owned); // consumes even on failure
          if (ids.size () > limits.max_states)
            throw ResourceLimit ("lazy Buchi underlying state budget exceeded");
          return id;
        }
        void cursor (StateId id, unsigned j) {
          const auto key = std::make_pair (id, j);
          if (not cursors.contains (key)) {
            if (cursors.size () >= limits.max_states)
              throw ResourceLimit ("lazy Buchi cursor state budget exceeded");
            cursors.insert (key);
          }
        }

        const std::vector<spot_rows::SpotEdge>& row (StateId id) {
          check ();
          if (const auto found = rows.find (id); found != rows.end ()) return found->second;
          try {
            if (rows.size () >= limits.rows.max_rows)
              throw ResourceLimit ("lazy Buchi underlying row budget exceeded");
            std::vector<spot_rows::SpotEdge> complete;
            {
              struct Release {
                  const spot::twa* provider;
                  void operator() (spot::twa_succ_iterator* i) const { provider->release_iter (i); }
              };
              // P0: TAA constructs the WHOLE row here, before first()/next().
              // Budgets are checked around this indivisible public operation;
              // hard time/memory caps still belong to the worker process.
              std::unique_ptr<spot::twa_succ_iterator, Release> it {
                  provider->succ_iter (ids[id]), {provider.get ()}};
              check ();
              if (not it) throw Declined ("lazy provider returned a null iterator");
              for (it->first (); not it->done (); it->next ()) {
                check ();
                if (complete.size () >= limits.rows.max_edges_per_row)
                  throw ResourceLimit ("lazy Buchi edge budget exceeded");
                const auto marks = it->acc ();
                if ((marks - acceptance.all_sets ()) != spot::acc_cond::mark_t {})
                  throw Declined ("lazy provider used an undeclared acceptance set");
                const bdd guard = it->cond ();
                const auto destination = intern (it->dst ());
                complete.push_back ({destination, guard, marks});
              }
            } // release even on failure; detect changes made at release as well
            check ();
            return rows.emplace (id, std::move (complete)).first->second;
          }
          catch (...) { failure = std::current_exception (); throw; }
        }
    };

    class CursorState final : public spot::state {
      public:
        std::shared_ptr<Context> context;
        StateId underlying;
        unsigned cursor;
        CursorState (std::shared_ptr<Context> c, StateId q, unsigned j)
          : context (std::move (c)), underlying (q), cursor (j) {}
        int compare (const spot::state* other) const override {
          const auto& rhs = *static_cast<const CursorState*> (other);
          const auto* left_provider = context->provider.get ();
          const auto* right_provider = rhs.context->provider.get ();
          if (left_provider != right_provider)
            return std::less<const spot::twa*> {} (left_provider, right_provider) ? -1 : 1;
          const int cmp = context->ids[underlying]->compare (rhs.context->ids[rhs.underlying]);
          if (cmp) return cmp;
          return cursor < rhs.cursor ? -1 : cursor > rhs.cursor ? 1 : 0;
        }
        std::size_t hash () const override {
          auto h = std::hash<const spot::twa*> {} (context->provider.get ());
          for (auto v : {context->ids[underlying]->hash (), std::size_t (cursor)})
            h ^= v + std::size_t (0x9e3779b9) + (h << 6) + (h >> 2);
          return h;
        }
        CursorState* clone () const override { return new CursorState (*this); }
    };

    inline const spot::state* state (const std::shared_ptr<Context>& c, StateId q, unsigned j) {
      c->check ();
      try {
        c->cursor (q, j);
        return new CursorState (c, q, j);
      }
      catch (...) { c->failure = std::current_exception (); throw; }
    }

    class Iterator final : public spot::twa_succ_iterator {
      public:
        Iterator (std::shared_ptr<Context> c, StateId q, unsigned j)
          : context_ (std::move (c)), row_ (context_->row (q)), cursor_ (j) {}
        bool first () override { context_->check (); index_ = 0; return not done (); }
        bool next () override { context_->check (); ++index_; return not done (); }
        bool done () const override { context_->check (); return index_ == row_.size (); }
        bdd cond () const override { context_->check (); return row_.at (index_).condition; }
        spot::acc_cond::mark_t acc () const override {
          const auto [next, accepting] = step ();
          (void) next;
          return accepting ? spot::acc_cond::mark_t {0} : spot::acc_cond::mark_t {};
        }
        const spot::state* dst () const override {
          const auto [next, accepting] = step ();
          (void) accepting;
          return state (context_, row_.at (index_).destination, next);
        }
      private:
        std::pair<unsigned, bool> step () const {
          context_->check ();
          const auto& edge = row_.at (index_);
          const auto& a = context_->acceptance;
          if (a.is_f ()) return {0, false};
          const unsigned m = a.num_sets ();
          if (m == 0) return {0, true}; // existing edges only: no dead-end loop
          if (not edge.acceptance.has (cursor_)) return {cursor_, false};
          if (cursor_ < m - 1) return {cursor_ + 1, false};
          return {0, true};
        }
        std::shared_ptr<Context> context_;
        const std::vector<spot_rows::SpotEdge>& row_;
        unsigned cursor_;
        std::size_t index_ = 0;
    };
  }

  class LazyBuchiView final : public spot::twa {
    public:
      explicit LazyBuchiView (spot::const_twa_ptr provider, Limits limits = {})
        : twa (dictionary (provider)), context_ (std::make_shared<detail::Context> (provider, limits)) {
        set_buchi ();
        prop_state_acc (false);
        copy_ap_of (provider);
        check_contract ();
      }
      const spot::state* get_init_state () const override {
        return detail::state (context_, context_->initial, 0);
      }
      spot::twa_succ_iterator* succ_iter (const spot::state* source) const override {
        check_contract ();
        const auto* s = dynamic_cast<const detail::CursorState*> (source);
        if (not s || s->context.get () != context_.get ())
          throw Declined ("state belongs to a different lazy Buchi view");
        // Preserve parallel marks. No merging at all is needed here; P3 ORs
        // guards only for equal destination/threshold contributions.
        return new detail::Iterator (context_, s->underlying, s->cursor);
      }
      std::string format_state (const spot::state* source) const override {
        const auto* s = dynamic_cast<const detail::CursorState*> (source);
        if (not s || s->context.get () != context_.get ()) throw Declined ("foreign cursor state");
        // Local diagnostic only; no provider printer or graph traversal.
        return std::to_string (s->underlying) + ":" + std::to_string (s->cursor);
      }
      void check_contract () const { context_->check (); }
      std::size_t underlying_rows () const { return context_->rows.size (); }

      // Enclose a consumer operation, including cached P2/P3 queries: twa::acc
      // is nonvirtual, so P2 alone cannot see changes behind this wrapper.
      template <typename F> auto checked (F&& f) const {
        return attempt ([&] {
          check_contract ();
          auto result = f ();
          check_contract ();
          return result;
        });
      }
    private:
      static spot::bdd_dict_ptr dictionary (const spot::const_twa_ptr& p) {
        if (not p) throw Declined ("lazy Buchi requires a provider");
        return p->get_dict ();
      }
      std::shared_ptr<detail::Context> context_;
  };

  // Factory seam also permits deterministic resource-failure injection. It
  // never requests a successor row or changes the supplied worker formula.
  // As with P3's BuddyErrors, initialize the worker dictionary before entry.
  template <typename Factory> auto make_view (Factory&& factory, Limits limits = {}) {
    return attempt ([&] {
      detail::bdd_budget (limits);
      auto provider = factory ();
      detail::bdd_budget (limits);
      return std::make_shared<LazyBuchiView> (std::move (provider), limits);
    });
  }
  inline auto make_taa (spot::formula worker_formula, const spot::bdd_dict_ptr& dict,
                        Limits limits = {}) {
    return make_view ([&] () -> spot::const_twa_ptr {
      if (not worker_formula.is_ltl_formula ()) throw Declined ("TAA route requires LTL");
      // P0 verdict A. Fixed for BOTH C4 and C5; refined rules are a different arm.
      return spot::ltl_to_taa (worker_formula, dict, false);
    }, limits);
  }

  // Helper audit (pinned Spot, see benchmarking/SPOT-OTF-API-AUDIT.md):
  // acc predicates/equality inspect acc_code; ap/ap_vars/copy_ap_of/register_ap
  // inspect/register only AP metadata; graph::is_existential reads its storage
  // flag; state hash/compare/clone/destroy and release_iter manage local objects.
  // Only get_init_state and the requested succ_iter row reach the provider.
  // bdd_getnodenum is a manager counter, not an automaton traversal. No is_empty,
  // accepting_run, Fin removal, SCC, copy, postprocessing, degeneralization,
  // complete-provider printer or whole-automaton statistics are called here.
} // namespace acacia::spot_lazy
