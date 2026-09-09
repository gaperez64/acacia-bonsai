#include "solver/spot_lazy_buchi_view.hh"
#include "solver/sparse_forward_rank.hh"
#include "utils/verbose.hh"

#include <spot/tl/parse.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/translate.hh>

#include <iostream>
#include <unordered_map>

namespace utils { unsigned verbose = 0; voutstream vout; }
namespace posets::vectors { size_t bool_threshold = 0; }

namespace {
  namespace lazy = acacia::spot_lazy;
  namespace rows = acacia::spot_rows;
  namespace letters = acacia::spot_letters;
  using Sparse = rows::SparseForwardRank<>;
  using Mark = spot::acc_cond::mark_t;
  using Code = spot::acc_cond::acc_code;
  int failures = 0;
  unsigned languages = 0, paired_games = 0;
  bool expect (const std::string& name, bool condition) {
    if (condition) return true;
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
    return false;
  }
  struct Destroy { void operator() (const spot::state* p) const { p->destroy (); } };
  using Owned = std::unique_ptr<const spot::state, Destroy>;
  struct Release {
      const spot::twa* provider;
      void operator() (spot::twa_succ_iterator* i) const { provider->release_iter (i); }
  };
  using Iter = std::unique_ptr<spot::twa_succ_iterator, Release>;

  spot::formula formula (const std::string& f) {
    auto parsed = spot::parse_infix_psl (f);
    if (parsed.format_errors (std::cerr)) throw std::runtime_error ("invalid test formula");
    return parsed.f;
  }
  spot::twa_graph_ptr graph (unsigned n, unsigned m, Code code) {
    auto g = spot::make_twa_graph (spot::make_bdd_dict ());
    g->set_acceptance (m, code);
    g->new_states (n);
    g->set_init_state (0);
    return g;
  }
  auto view (spot::const_twa_ptr p, lazy::Limits limits = {}) {
    auto result = lazy::make_view ([&] { return p; }, limits);
    if (not result.value) {
      if (result.error) std::rethrow_exception (result.error);
      throw std::runtime_error ("view admission failed");
    }
    return *result.value;
  }
  // OFFLINE TEST ORACLE ONLY: these are the sole full graph materializations
  // and language comparisons. No such helpers are called by the P5 headers.
  auto materialize (spot::const_twa_ptr p) {
    return spot::make_twa_graph (p, spot::twa::prop_set::all ());
  }

  struct Counts { unsigned acquired = 0, released = 0; std::vector<unsigned> sources; };
  class Observed final : public spot::twa {
    public:
      spot::twa_graph_ptr graph;
      std::shared_ptr<Counts> counts = std::make_shared<Counts> ();
      enum class Fault { none, allocation, length, capacity, count, meaning, late_meaning, bdd_growth, mark };
      Fault fault = Fault::none;
      mutable bdd retained = bddtrue;
      explicit Observed (spot::twa_graph_ptr g) : twa (g->get_dict ()), graph (std::move (g)) {
        copy_ap_of (graph);
        set_acceptance (graph->acc ());
      }
      ~Observed () override { delete iter_cache_; iter_cache_ = nullptr; }
      const spot::state* get_init_state () const override { return graph->get_init_state (); }
      std::string format_state (const spot::state*) const override { throw std::logic_error ("no provider formatting allowed"); }
      spot::twa_succ_iterator* succ_iter (const spot::state* s) const override {
        ++counts->acquired;
        counts->sources.push_back (graph->state_number (s));
        if (fault == Fault::allocation) throw std::bad_alloc ();
        if (fault == Fault::length) throw std::length_error ("injected row capacity");
        if (fault == Fault::capacity) {
          const spot::acc_cond too_many {SPOT_MAX_ACCSETS + 1, Code::t ()};
          (void) too_many;
        }
        if (fault == Fault::count) const_cast<Observed*> (this)->set_generalized_buchi (3);
        if (fault == Fault::meaning) const_cast<Observed*> (this)->set_acceptance (2, Code ("Inf(0) | Inf(1)"));
        if (fault == Fault::bdd_growth) {
          for (const auto& ap : graph->ap ()) retained &= bdd_ithvar (get_dict ()->varnum (ap));
          retained = !retained; // hold the new nodes beyond iterator acquisition
        }
        class Proxy final : public spot::twa_succ_iterator {
          public:
            const Observed* owner;
            Iter iter;
            Proxy (const Observed* p, const spot::state* state)
              : owner (p), iter (p->graph->succ_iter (state), {p->graph.get ()}) {}
            ~Proxy () override { ++owner->counts->released; }
            bool first () override { return iter->first (); }
            bool next () override {
              const bool more = iter->next ();
              if (not more && owner->fault == Fault::late_meaning)
                const_cast<Observed*> (owner)->set_acceptance (2, Code ("Inf(0) | Inf(1)"));
              return more;
            }
            bool done () const override { return iter->done (); }
            bdd cond () const override { return iter->cond (); }
            Mark acc () const override { return owner->fault == Fault::mark ? Mark {3} : iter->acc (); }
            const spot::state* dst () const override { return iter->dst (); }
        };
        return new Proxy (this, s);
      }
  };

  void language_checks () {
    for (const auto* spelling : {"true", "false", "F a", "G a", "GF a", "FG a",
                                 "a U b", "a R b", "a W b", "a M b", "XXX a",
                                 "X(X(a U X b))", "GF a & GF b", "GF a & GF b & GF c",
                                 "G(a -> F b)", "(a U b) | G c"}) {
      const auto f = formula (spelling);
      auto dict = spot::make_bdd_dict ();
      auto admitted = lazy::make_taa (f, dict);
      if (not expect (std::string (spelling) + " TAA admitted", admitted.value.has_value ())) continue;
      expect ("factory requests zero TGBA rows", (*admitted.value)->underlying_rows () == 0);
      auto complete = materialize (*admitted.value);
      auto reference = spot::translator {dict}.run (f);
      expect (std::string (spelling) + " full language equivalence", spot::are_equivalent (complete, reference));
      expect ("ordinary transition Buchi", complete->acc ().is_buchi ()
              && not (*admitted.value)->prop_state_acc ().is_true ());
      (*admitted.value)->check_contract ();
      ++languages;
    }
  }

  void cursor_cases () {
    for (const unsigned m : {0u, 1u, 2u, 3u}) {
      auto g = graph (1, m, Code::generalized_buchi (m));
      g->new_edge (0, 0, bddtrue, g->acc ().all_sets ());
      auto provider = std::make_shared<Observed> (g);
      auto wrapped = view (provider);
      auto cache = std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {wrapped});
      auto r = cache->initial_rank ();
      expect ("initial cursor/rank 0 and no row", r == rows::Rank {0} && provider->counts->acquired == 0);
      for (unsigned t = 1; t <= 6; ++t) {
        const auto next = cache->evaluate (r, bddtrue, 9);
        if (not expect ("complete cursor transition", next.rank.has_value ())) break;
        r = *next.rank;
        const auto active = std::find_if (r.begin (), r.end (), [] (int v) { return v != -1; });
        expect ("several marks advance only ONE index", active != r.end ()
                && *active == static_cast<int> (t / std::max (1u, m)));
      }
      expect ("underlying complete row shared across cursors", provider->counts->acquired == 1
              && wrapped->underlying_rows () == 1 && cache->state_count () == std::max (1u, m));
      expect ("all-mark cycle language", spot::are_equivalent (materialize (wrapped), materialize (provider)));
    }

    auto parallel = graph (1, 1, Code::buchi ());
    const auto v = parallel->register_ap ("a");
    const bdd a = bdd_ithvar (v);
    parallel->new_edge (0, 0, a, {0});
    parallel->new_edge (0, 0, !a);
    auto w = view (parallel);
    rows::SpotRows r {rows::GenericTransitionBuchi {w}};
    const auto row = r.row (0);
    expect ("same destination with different marks preserved", row.row && row.row->edges.size () == 2
            && row.row->edges[0].destination == row.row->edges[1].destination
            && row.row->edges[0].increment && not row.row->edges[1].increment);
    expect ("marks never ORed across guards", *r.evaluate ({0}, a, 2).rank == rows::Rank {1}
            && *r.evaluate ({0}, !a, 2).rank == rows::Rank {0});
    expect ("different-mark language", spot::are_equivalent (materialize (w), parallel));

    auto two = graph (1, 2, Code::generalized_buchi (2));
    two->new_edge (0, 0, bddtrue, {0});
    two->new_edge (0, 0, bddtrue, {1});
    auto two_view = view (two);
    rows::SpotRows two_rows {rows::GenericTransitionBuchi {two_view}};
    const auto first = two_rows.row (0);
    expect ("different marks give different cursors", first.row && first.row->edges.size () == 2
            && first.row->edges[0].destination != first.row->edges[1].destination);
    expect ("two-mark branches preserve language", spot::are_equivalent (materialize (two_view), two));

    auto missing = graph (1, 2, Code::generalized_buchi (2));
    missing->new_edge (0, 0, bddtrue, {0});
    auto missing_view = view (missing);
    expect ("fairness set missing forever rejects", spot::are_equivalent (materialize (missing_view), spot::formula::ff ()));
    for (unsigned m : {0u, 1u, 2u}) {
      auto dead = graph (2, m, Code::generalized_buchi (m));
      dead->new_edge (0, 1, bddtrue, dead->acc ().all_sets ());
      auto dead_view = view (dead);
      rows::SpotRows dead_rows {rows::GenericTransitionBuchi {dead_view}};
      auto successor = dead_rows.evaluate ({0}, bddtrue, 3);
      auto empty = dead_rows.evaluate (*successor.rank, bddtrue, 3);
      expect ("finite accepting prefix followed by dead end dies", empty.rank
              && std::all_of (empty.rank->begin (), empty.rank->end (), [] (int v) { return v == -1; }));
      expect ("dead ends are not accepted", spot::are_equivalent (materialize (dead_view), spot::formula::ff ()));
    }
    for (unsigned m : {0u, 2u}) {
      auto none = graph (1, m, Code::f ());
      none->new_edge (0, 0, bddtrue, none->acc ().all_sets ());
      auto none_view = view (none);
      rows::SpotRows none_rows {rows::GenericTransitionBuchi {none_view}};
      expect ("constant false never increments", *none_rows.evaluate ({0}, bddtrue, 2).rank == rows::Rank {0});
      expect ("constant false keeps empty language", spot::are_equivalent (materialize (none_view), spot::formula::ff ()));
    }
  }

  void ownership () {
    auto g = graph (1, 2, Code::generalized_buchi (2));
    g->new_edge (0, 0, bddtrue, {0, 1});
    auto provider = std::make_shared<Observed> (g);
    const auto counts = provider->counts;
    std::weak_ptr<spot::twa> alive = provider;
    auto a = view (provider), b = view (provider);
    Owned a0 {a->get_init_state ()}, b0 {b->get_init_state ()}, clone {a0->clone ()};
    expect ("same provider semantic equality across arenas", a0->compare (b0.get ()) == 0 && a0->hash () == b0->hash ());
    expect ("clone identity", clone->compare (a0.get ()) == 0 && clone->hash () == a0->hash ());
    Owned a1;
    {
      Iter it {a->succ_iter (a0.get ()), {a.get ()}};
      it->first ();
      a1.reset (it->dst ());
      Owned repeat {it->dst ()};
      expect ("repeated destination ownership", a1->compare (repeat.get ()) == 0 && a1->hash () == repeat->hash ());
      expect ("cursor included in identity/hash", a0->compare (a1.get ()) != 0 && a0->hash () != a1->hash ());
    } // early iterator release
    try { Iter it {a->succ_iter (a1.get ()), {a.get ()}}; it->first (); throw 42; }
    catch (int) {}
    auto foreign = view (g);
    Owned other {foreign->get_init_state ()};
    expect ("provider identity included", a0->compare (other.get ()) != 0 && a0->hash () != other->hash ());
    a.reset (); b.reset (); provider.reset ();
    expect ("owned states retain provider after wrappers die", not alive.expired ());
    a0.reset (); b0.reset (); clone.reset (); a1.reset ();
    expect ("provider and iterators eventually released", alive.expired () && counts->acquired == counts->released);
  }

  void failures_and_budgets () {
    const auto f = formula ("GF a & GF b");
    const auto dict = spot::make_bdd_dict ();
    auto acceptance = lazy::make_taa (f, dict, {.max_acceptance_sets = 1});
    expect ("factory acceptance budget UNKNOWN", not acceptance.value && acceptance.status == lazy::Status::unknown);
    auto states = lazy::make_taa (f, dict, {.max_states = 0});
    expect ("factory initial state budget UNKNOWN", not states.value && states.status == lazy::Status::unknown);
    auto bdds = lazy::make_taa (f, dict, {.max_live_bdd_nodes = 0});
    expect ("factory BDD budget UNKNOWN", not bdds.value && bdds.status == lazy::Status::unknown);
    auto oom = lazy::make_view ([] () -> spot::const_twa_ptr { throw std::bad_alloc (); });
    expect ("factory bad_alloc UNKNOWN", not oom.value && oom.status == lazy::Status::unknown);
    auto capacity = lazy::make_view ([] () -> spot::const_twa_ptr { throw std::length_error ("factory capacity"); });
    expect ("factory capacity UNKNOWN", not capacity.value && capacity.status == lazy::Status::unknown);
    auto native_capacity = lazy::make_view ([] () -> spot::const_twa_ptr {
      const spot::acc_cond too_many {SPOT_MAX_ACCSETS + 1, Code::t ()};
      (void) too_many;
      return {};
    });
    expect ("Spot native acceptance capacity UNKNOWN", not native_capacity.value
            && native_capacity.status == lazy::Status::unknown);
    auto unsupported = lazy::make_taa (formula ("{a[*];b}<>-> c"), dict);
    expect ("unsupported PSL DECLINED", not unsupported.value && unsupported.status == lazy::Status::declined);
    for (const auto& code : {Code ("Fin(0)"), Code ("Inf(0) | Inf(1)"), Code ("Inf(0)"), Code::t ()}) {
      auto p = graph (1, 2, code);
      auto result = lazy::make_view ([&] { return p; });
      expect ("arbitrary/incomplete acceptance DECLINED", not result.value && result.status == lazy::Status::declined);
    }
    auto alternating = graph (2, 1, Code::buchi ());
    alternating->new_univ_edge (0, {0, 1}, bddtrue, {0});
    auto alt = lazy::make_view ([&] { return alternating; });
    expect ("raw alternation DECLINED", not alt.value && alt.status == lazy::Status::declined);

    for (const auto fault : {Observed::Fault::allocation, Observed::Fault::length, Observed::Fault::capacity, Observed::Fault::count,
                             Observed::Fault::meaning, Observed::Fault::late_meaning, Observed::Fault::mark}) {
      auto g = graph (1, 2, Code::generalized_buchi (2));
      g->new_edge (0, 0, bddtrue, {0, 1});
      auto p = std::make_shared<Observed> (g);
      p->fault = fault;
      auto w = view (p);
      rows::SpotRows cache {rows::GenericTransitionBuchi {w}};
      const auto r = cache.row (0);
      const bool resource = fault == Observed::Fault::allocation || fault == Observed::Fault::length;
      expect ("failed whole row never published", not r.row && cache.complete_rows () == 0
              && r.status == (resource ? rows::Status::resource_limit : rows::Status::failed));
      auto result = w->checked ([] { return true; });
      expect ("row failure stays UNKNOWN or DECLINED", not result.value
              && result.status == (resource || fault == Observed::Fault::capacity
                                       ? lazy::Status::unknown : lazy::Status::declined));
      expect ("failed row never retried as empty", not cache.row (0).row && p->counts->acquired == 1);
    }
    // Mutation after a previously successful row must also invalidate cache hits.
    auto g = graph (1, 2, Code::generalized_buchi (2));
    g->new_edge (0, 0, bddtrue, {0, 1});
    auto w = view (g);
    rows::SpotRows cache {rows::GenericTransitionBuchi {w}};
    expect ("pre-mutation row complete", cache.row (0).row != nullptr);
    g->set_acceptance (2, Code ("Inf(0) | Inf(1)"));
    auto changed = w->checked ([&] { return cache.row (0); });
    expect ("cached operation declines changed meaning", not changed.value && changed.status == lazy::Status::declined);
    g->set_generalized_buchi (2);
    expect ("decline latched even after restoration", w->checked ([] { return true; }).status == lazy::Status::declined);

    for (unsigned kind = 0; kind < 4; ++kind) {
      auto p = graph (2, 2, Code::generalized_buchi (2));
      p->new_edge (0, kind == 0 ? 1 : 0, bddtrue, {0, 1});
      lazy::Limits limits;
      if (kind < 2) limits.max_states = 1; // underlying state, then cursor state
      if (kind == 2) limits.rows.max_rows = 0;
      if (kind == 3) limits.rows.max_edges_per_row = 0;
      auto limited = view (p, limits);
      auto r = std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {limited});
      letters::Oracle oracle {r, {bddtrue, bddtrue, bddtrue, {}}, 2};
      auto result = oracle.unsafe (r->initial_rank ());
      expect ("row state/cursor/row/edge budget UNKNOWN through P3", not result.value
              && result.unknown == letters::Unknown::resource_limit && r->complete_rows () == 0);
    }
    auto p = graph (1, 0, Code::t ());
    for (unsigned i = 0; i < 8; ++i) p->register_ap ("budget" + std::to_string (i));
    p->new_edge (0, 0, bddtrue);
    auto growing = std::make_shared<Observed> (p);
    growing->fault = Observed::Fault::bdd_growth;
    auto limited = view (growing, {.max_live_bdd_nodes = static_cast<std::size_t> (bdd_getnodenum ())});
    rows::SpotRows r {rows::GenericTransitionBuchi {limited}};
    const auto result = r.row (0);
    expect ("BDD growth inside succ_iter is UNKNOWN", result.status == rows::Status::resource_limit
            && not result.row && r.complete_rows () == 0);
  }

  letters::WorkerAlphabet alphabet (const spot::const_twa_ptr& p) {
    letters::WorkerAlphabet result {p->ap_vars (), bddtrue, bddtrue, {}};
    for (auto ap : p->ap ()) {
      const auto var = p->get_dict ()->varnum (ap);
      (result.order.empty () ? result.inputs : result.outputs) &= bdd_ithvar (var);
      result.order.push_back (var);
    }
    return result;
  }
  std::vector<bdd> valuations (const letters::WorkerAlphabet& a, bdd vars) {
    std::vector<bdd> result {bddtrue};
    for (int v : a.order) {
      if (bdd_exist (vars, bdd_ithvar (v)) == vars) continue;
      std::vector<bdd> next;
      for (const auto& prefix : result) {
        next.push_back (prefix & bdd_nithvar (v));
        next.push_back (prefix & bdd_ithvar (v));
      }
      result = std::move (next);
    }
    return result;
  }
  using Game = std::unordered_map<Sparse, bool>; // semantic key -> safe-game winning membership
  // TEST ONLY: exhaustively enumerate the finite fixed-K game, then remove
  // states with an input having no winning output. This is not a new backend.
  Game finite_game (const std::shared_ptr<rows::SpotRows>& r, const letters::WorkerAlphabet& a,
                    rows::SpotStateIds& semantic, int K) {
    std::vector<Sparse> nodes {Sparse {{{r->initial_id (), 0}}, K}};
    std::unordered_map<Sparse, std::size_t> ids {{nodes.front (), 0}};
    std::vector<std::vector<std::vector<std::size_t>>> edges;
    const auto inputs = valuations (a, a.inputs), outputs = valuations (a, a.outputs);
    for (std::size_t n = 0; n < nodes.size (); ++n) {
      if (nodes.size () > 20000) throw std::runtime_error ("test fixed-K arena unexpectedly large");
      const auto source = nodes[n];
      edges.emplace_back ();
      if (not source.is_safe (K)) continue;
      for (auto u : inputs) {
        edges[n].emplace_back ();
        for (auto c : outputs) {
          auto next = rows::evaluate_sparse (r, a, source, u & c, K);
          if (not next.value) throw std::runtime_error ("fixed-K row failure");
          auto [it, inserted] = ids.emplace (*next.value, nodes.size ());
          if (inserted) nodes.push_back (*next.value);
          edges[n].back ().push_back (it->second);
        }
      }
    }
    std::vector<bool> winning;
    for (const auto& node : nodes) winning.push_back (node.is_safe (K));
    bool changed;
    do {
      changed = false;
      for (std::size_t n = 0; n < nodes.size (); ++n) {
        if (not winning[n]) continue;
        for (const auto& choices : edges[n]) {
          bool good = false;
          for (auto target : choices) good |= winning[target];
          if (not good) { winning[n] = false; changed = true; break; }
        }
      }
    } while (changed);
    Game result;
    for (std::size_t n = 0; n < nodes.size (); ++n) {
      std::vector<Sparse::Entry> key;
      for (auto [q, value] : nodes[n].entries ())
        key.emplace_back (semantic.intern (r->canonical_state (q)->clone ()), value);
      result.emplace (Sparse {std::move (key), K}, winning[n]);
    }
    return result;
  }

  void fixed_k_controls () {
    for (const auto* spelling : {"true", "false", "F a", "GF a", "FG a", "a U b", "GF a & GF b"}) {
      const auto f = formula (spelling);
      // Exactly ONE provider factory, with the P0 choice fixed in both controls.
      const auto provider = spot::ltl_to_taa (f, spot::make_bdd_dict (), false);
      const auto a = alphabet (provider);
      for (int K : {1, 2}) {
        const auto eager_view = view (provider), lazy_view = view (provider);
        auto eager = std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {eager_view});
        auto demand = std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {lazy_view});
        // C4 only: deliberately drain every reachable cursor row before play.
        for (rows::StateId q = 0; q < eager->state_count (); ++q)
          if (not eager->row (q).row) throw std::runtime_error ("eager control row failure");
        expect ("C5 still unexpanded when C4 complete", demand->complete_rows () == 0
                && lazy_view->underlying_rows () == 0 && eager->complete_rows () == eager->state_count ());
        rows::SpotStateIds semantic {eager_view};
        const auto expected = finite_game (eager, a, semantic, K);
        const auto actual = finite_game (demand, a, semantic, K);
        expect (std::string (spelling) + " identical fixed-K game membership K=" + std::to_string (K), expected == actual);
        eager_view->check_contract (); lazy_view->check_contract ();
        ++paired_games;
      }
    }
  }

  void omitted_row_verifier () {
    auto g = graph (2, 1, Code::buchi ());
    g->new_edge (0, 1, bddtrue);
    g->new_edge (1, 1, bddtrue, {0});
    auto observed = std::make_shared<Observed> (g);
    auto w = view (observed);
    auto search = std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {w});
    search->row (0);
    expect ("false candidate setup really omits a reachable row", observed->counts->sources == std::vector<unsigned> {0}
            && search->state (1) == rows::RowState::not_requested);
    // A faulty consumer treats row 1 as empty and proposes this safe downset.
    const std::vector<rows::Rank> candidate {{0, 0}};
    // Fresh wrapper rows AND P3 oracle verify semantics, not search cache flags.
    auto verifier_view = view (observed);
    auto verifier_rows = std::make_shared<rows::SpotRows> (rows::GenericTransitionBuchi {verifier_view});
    verifier_rows->row (0); // establish this certificate's stable coordinates
    letters::Oracle verifier {verifier_rows, {bddtrue, bddtrue, bddtrue, {}}, 1};
    const auto result = verifier_view->checked ([&] {
      return verifier.invariant (verifier_rows->initial_rank (), candidate, 0);
    });
    expect ("omitted-row false candidate rejected by verifier", result.value && result.value->value
            && *result.value->value == letters::Invariant::rejected && verifier_rows->complete_rows () == 2);
    expect ("verifier actually requests the missing row", observed->counts->sources == std::vector<unsigned> ({0, 0, 1}));
  }
}

int main () {
  try {
    language_checks (); cursor_cases (); ownership (); failures_and_budgets ();
    fixed_k_controls (); omitted_row_verifier ();
  }
  catch (const std::exception& e) { expect (std::string ("unexpected exception: ") + e.what (), false); }
  if (not failures) std::cerr << "spot-lazy: " << languages << " language checks, " << paired_games
                            << " fixed-K pairs, cursor/ownership/budget/decline/verifier cases pass\n";
  return failures ? 1 : 0;
}
