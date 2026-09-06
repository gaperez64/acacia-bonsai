#include "solver/spot_letter_oracle.hh"
#include "tiny_spot_game.hh"
#include "research/explicit_forward_game.hh"
#include "utils/verbose.hh"

#include <functional>
#include <iostream>
#include <random>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}
extern char** environ;

namespace {
  using namespace acacia::spot_letters;
  using namespace acacia::spot_rows;
  namespace reference = acacia::research;
  int failures = 0;
  std::size_t comparisons = 0, query_unknowns = 0, expected_unknowns = 0;
  std::size_t agreeing_games = 0, game_unknowns = 0, wins = 0, losses = 0;
  constexpr std::uint32_t seed = 0x50334bdd;

  bool expect (const std::string& name, bool condition) {
    if (condition) return true;
    if (failures < 30) std::cerr << "FAIL: " << name << '\n';
    ++failures;
    return false;
  }
  template <typename T> bool complete (const std::string& name, const Result<T>& r) {
    if (not r.value) ++query_unknowns;
    return expect (name + " completed", r.value.has_value () && r.unknown == Unknown::none);
  }
  bool truth (bdd f, bdd letter) { return (f & letter) != bddfalse; }
  bool leq (const Rank& a, const Rank& b) { return Oracle::leq (a, b); }
  bool equal (const Rank& a, const Rank& b) { return leq (a, b) && leq (b, a); }
  bool down (const Rank& r, const std::vector<Rank>& G) {
    return std::any_of (G.begin (), G.end (), [&] (const auto& g) { return leq (r, g); });
  }
  bool up (const Rank& r, const std::vector<Rank>& L) {
    return std::any_of (L.begin (), L.end (), [&] (const auto& ell) { return leq (ell, r); });
  }
  std::vector<Rank> ranks (const Rank& caps) {
    std::vector<Rank> result;
    Rank rank (caps.size (), -1);
    std::function<void (std::size_t)> visit = [&] (std::size_t q) {
      if (q == caps.size ()) { result.push_back (rank); return; }
      for (rank[q] = -1; rank[q] <= caps[q]; ++rank[q]) visit (q + 1);
    };
    visit (0);
    return result;
  }
  using acacia::testing::spot_games::letters;
  using acacia::testing::spot_games::graph;
  using acacia::testing::spot_games::alphabet;
  bool finish_rows (SpotRows& rows) {
    for (StateId p = 0; p < rows.state_count (); ++p)
      if (rows.row (p).status != Status::complete) return false;
    return true;
  }
  Rank successor (SpotRows& rows, const Rank& r, bdd letter, std::int32_t K) {
    const auto result = rows.evaluate (r, letter, K);  // P2, no third tau evaluator
    if (not result.rank) throw std::runtime_error ("P2 evaluator did not complete");
    return *result.rank;
  }
  bool direct_losing (SpotRows& rows, const WorkerAlphabet& ap, const Rank& r,
                      const std::vector<Rank>& L, std::int32_t K) {
    for (const auto& u : letters (ap, Variables::inputs)) {
      bool all_bad = true;
      for (const auto& c : letters (ap, Variables::outputs)) {
        const auto next = successor (rows, r, u & c, K);
        all_bad &= not rows.is_safe (next, K) || up (next, L);
      }
      if (all_bad) return true;
    }
    return false;
  }
  bool direct_invariant (SpotRows& rows, const WorkerAlphabet& ap, const Rank& initial,
                         const std::vector<Rank>& G, std::int32_t K) {
    if (not down (initial, G)) return false;
    for (const auto& g : G) if (not rows.is_safe (g, K)) return false;
    for (const auto& g : G)
      for (const auto& u : letters (ap, Variables::inputs)) {
        bool covered = false;
        for (const auto& c : letters (ap, Variables::outputs))
          covered |= down (successor (rows, g, u & c, K), G);
        if (not covered) return false;
      }
    return true;
  }
  void compare_predicate (const std::string& name, const Result<bdd>& actual,
                          const WorkerAlphabet& ap, const std::vector<bdd>& alphabet,
                          const std::function<bool (std::size_t)>& expected) {
    if (not complete (name, actual)) return;
    expect (name + " AP-only support", bdd_exist (bdd_support (*actual.value), ap.ap_vars) == bddtrue);
    for (std::size_t i = 0; i < alphabet.size (); ++i) {
      expect (name, truth (*actual.value, alphabet[i]) == expected (i));
      ++comparisons;
    }
  }

  // Exhaustive over ALL [-1,K] source/target ranks and letters, including
  // already saturated ranks and values above frozen Boolean safety caps.
  void exhaustive (const std::string& name, std::shared_ptr<SpotRows> rows,
                   const WorkerAlphabet& ap, std::int32_t K) {
    if (not expect (name + " complete fixture", finish_rows (*rows))) return;
    const auto all = ranks (Rank (rows->state_count (), K));
    const auto valuations = letters (ap, Variables::all);
    const int variables_before = bdd_varnum ();
    Oracle oracle {rows, ap, K};
    for (const auto& r : all) {
      std::vector<Rank> next;
      for (const auto& letter : valuations) next.push_back (successor (*rows, r, letter, K));
      for (StateId q = 0; q <= rows->state_count (); ++q)
        for (std::int64_t h = -2; h <= K + 1; ++h)
          compare_predicate (name + " threshold", oracle.threshold (r, q, h), ap, valuations,
                             [&] (auto i) { return Oracle::at (next[i], q) >= h; });
      compare_predicate (name + " unsafe", oracle.unsafe (r), ap, valuations,
                         [&] (auto i) { return not rows->is_safe (next[i], K); });
      compare_predicate (name + " empty Bad", oracle.bad (r, {}, 0), ap, valuations,
                         [&] (auto i) { return not rows->is_safe (next[i], K); });
      compare_predicate (name + " empty Good", oracle.good (r, {}, 0), ap, valuations,
                         [] (auto) { return false; });
      const auto empty_losing = oracle.losing (r, {}, 0);
      if (complete (name + " empty losing set", empty_losing))
        expect (name + " empty losing set quantification", (*empty_losing.value == Losing::proved_losing)
                == direct_losing (*rows, ap, r, {}, K));
      expect (name + " empty invariant rejected", oracle.invariant (r, {}, 0).value == Invariant::rejected);
      Epoch epoch = 1;
      for (const auto& target : all) {
        compare_predicate (name + " UpPre", oracle.up_pre (r, target), ap, valuations,
                           [&] (auto i) { return leq (target, next[i]); });
        compare_predicate (name + " DownPre destination union", oracle.down_pre (r, target), ap, valuations,
                           [&] (auto i) { return leq (next[i], target); });
        compare_predicate (name + " Eq three cases", oracle.eq (r, target), ap, valuations,
                           [&] (auto i) { return equal (next[i], target); });
        const std::vector<Rank> generators {target, all[(epoch * 7) % all.size ()]};
        compare_predicate (name + " Bad", oracle.bad (r, generators, epoch), ap, valuations,
                           [&] (auto i) { return not rows->is_safe (next[i], K) || up (next[i], generators); });
        compare_predicate (name + " Good", oracle.good (r, generators, epoch), ap, valuations,
                           [&] (auto i) { return down (next[i], generators); });
        const auto losing = oracle.losing (r, generators, epoch);
        if (complete (name + " losing", losing))
          expect (name + " exists u forall c", (*losing.value == Losing::proved_losing)
                  == direct_losing (*rows, ap, r, generators, K));
        const auto invariant = oracle.invariant (r, generators, epoch);
        if (complete (name + " invariant", invariant))
          expect (name + " safe, initial coverage, forall u exists c", (*invariant.value == Invariant::verified)
                  == direct_invariant (*rows, ap, r, generators, K));
        ++epoch;
      }
    }
    expect (name + " no BDD variables allocated", bdd_varnum () == variables_before);
  }

  void fixtures () {
    for (const bool input : {false, true})
      for (const bool output : {false, true}) {
        auto g = graph (2, false);
        const auto ap = alphabet (g, input, output);
        const bdd u = input ? ap.inputs : bdd (bddtrue);
        const bdd c = output ? ap.outputs : bdd (bddtrue);
        // Parallel increments; an inactive source with accepting edges; one
        // overflowing enabled branch alongside a nonoverflowing branch; gaps.
        g->new_edge (0, 1, u);
        g->new_edge (0, 1, u & c, {0});
        g->new_edge (1, 0, c, {0});
        const auto rows = std::make_shared<SpotRows> (GenericTransitionBuchi {g});
        exhaustive ("parallel/gaps input=" + std::to_string (input) + " output=" + std::to_string (output), rows, ap, 2);
      }
    {
      auto g = graph (2, true);
      const auto ap = alphabet (g, true, true);
      g->new_edge (0, 1, bddtrue);  // destination 1 is accepting (Boolean cap 0)
      g->new_edge (0, 0, ap.inputs);
      g->new_edge (1, 0, ap.outputs, {0});
      g->new_edge (1, 1, !ap.outputs, {0});
      auto rows = std::make_shared<SpotRows> (FrozenAcacia {g, 1});
      exhaustive ("mixed Boolean-tail overflow", rows, ap, 3);
      Oracle oracle {rows, ap, 3};
      const auto unsafe = oracle.unsafe (Rank {0, -1});
      expect ("Boolean overflow below numeric K", unsafe.value == bddtrue);
      const auto absent = oracle.down_pre (Rank {0, -1}, Rank {2, -1});
      expect ("activated absent upper coordinate rejects every letter", absent.value == bddfalse);
    }
    {
      auto g = graph (2, true);
      const auto ap = alphabet (g, true, true);
      g->new_edge (0, 1, ap.inputs & ap.outputs);
      // State 1 is a complete empty row. Letters outside u&c enable no edge.
      exhaustive ("empty row/no enabled transition", std::make_shared<SpotRows> (FrozenAcacia {g, 2}), ap, 1);
    }
    {
      auto g = graph (1, false);
      auto ap = alphabet (g, true, true);
      g->new_edge (0, 0, ap.inputs ^ ap.outputs, {0});
      auto rows = std::make_shared<SpotRows> (GenericTransitionBuchi {g});
      exhaustive ("different outputs for different inputs", rows, ap, 1);
      Oracle oracle {rows, ap, 1};
      const std::vector<Rank> G {Rank {0}};
      expect ("controller observes input", oracle.invariant (Rank {0}, G, 0).value == Invariant::verified);
      expect ("not bad remains unresolved", oracle.losing (Rank {0}, {}, 0).value == Losing::unresolved);
      const bdd good = *oracle.good (Rank {0}, G, 0).value;
      expect ("no single output works for all inputs", bdd_exist (bdd_forall (good, ap.inputs), ap.outputs) == bddfalse);
      // The same worker monitor under an already swapped partition has the
      // opposite quantifier result when only c controls an accepting step.
      auto other = graph (1, false);
      auto swapped = alphabet (other, true, true);
      other->new_edge (0, 0, swapped.outputs, {0});
      auto other_rows = std::make_shared<SpotRows> (GenericTransitionBuchi {other});
      Oracle controller {other_rows, swapped, 1};
      expect ("original worker output controllable", controller.losing (Rank {0}, {}, 0).value == Losing::unresolved);
      std::swap (swapped.inputs, swapped.outputs);
      controller.reset (other_rows, swapped, 1);
      expect ("transformed worker partition honored", controller.losing (Rank {0}, {}, 0).value == Losing::proved_losing);
    }
  }

  void helper_and_cache_tests () {
    auto g = graph (2, true);
    auto ap = alphabet (g, true, true);
    g->new_edge (0, 1, ap.inputs, {0});
    g->new_edge (1, 1, ap.outputs);  // frozen destination increment is zero
    auto rows = std::make_shared<SpotRows> (FrozenAcacia {g, 2});
    Oracle oracle {rows, ap, 2};
    const auto all = letters (ap, Variables::all);
    const auto functions = std::vector<bdd> {bddfalse, bddtrue, ap.inputs, ap.outputs,
                                            ap.inputs ^ ap.outputs, ap.inputs | ap.outputs};
    for (const auto& f : functions) {
      const auto selected = oracle.model (f);
      if (complete ("stable total model", selected)) {
        const auto first = std::find_if (all.begin (), all.end (), [&] (auto letter) { return truth (f, letter); });
        expect ("false-first recorded order and explicit don't-cares",
                first == all.end () ? not *selected.value : *selected.value == *first);
      }
      for (const auto& letter : all)
        expect ("total restriction", oracle.restrict_total (f, letter, Variables::all).value
                == (truth (f, letter) ? bdd (bddtrue) : bdd (bddfalse)));
      const auto ops = oracle.query<bool> ([&] (auto& b) {
        return b.land (f, bddtrue) == f && b.lor (f, bddfalse) == f
               && b.negate (b.negate (f)) == f && b.supported (f, ap.ap_vars)
               && b.nodes (f) == bdd_nodecount (f) && b.satisfiable (f) == (f != bddfalse)
               && b.tautology (f) == (f == bddtrue)
               && b.exists (f, ap.outputs) == bdd_exist (f, ap.outputs)
               && b.forall (f, ap.inputs) == bdd_forall (f, ap.inputs);
      });
      expect ("BDD helper operations", ops.value == true);
    }
    expect ("input-only total restriction",
            oracle.restrict_total (ap.inputs ^ ap.outputs, !ap.inputs, Variables::inputs).value == ap.outputs);
    expect ("output-only total restriction",
            oracle.restrict_total (ap.inputs ^ ap.outputs, ap.outputs, Variables::outputs).value == !ap.inputs);
    expect ("input model fills don't care", oracle.model (bddtrue, Variables::inputs).value == std::optional<bdd> (!ap.inputs));
    expect ("partial cube rejected", not oracle.restrict_total (bddtrue, bddtrue, Variables::all).value);
    expect ("false cube rejected", not oracle.restrict_total (bddtrue, bddfalse, Variables::all).value);
    std::reverse (ap.order.begin (), ap.order.end ());
    oracle.reset (rows, ap, 2);
    expect ("recorded AP order overrides manager order", oracle.model (ap.inputs ^ ap.outputs).value
            == std::optional<bdd> (ap.inputs & !ap.outputs));
    bdd_reorder (BDD_REORDER_SIFT);
    expect ("stable after BDD reordering", oracle.model (ap.inputs ^ ap.outputs).value
            == std::optional<bdd> (ap.inputs & !ap.outputs));

    const Rank source {0, -1}, absent {-1, -1}, full {1, 1};
    const auto first = oracle.threshold (source, 1, 0);
    const auto hits = oracle.metrics ().threshold_hits;
    expect ("threshold reused", oracle.threshold (source, 1, 0).value == first.value
            && oracle.metrics ().threshold_hits > hits);
    expect ("Good empty epoch", oracle.good (source, {}, 1).value == bddfalse);
    expect ("Good replacement epoch", oracle.good (source, {full}, 2).value == bddtrue);
    expect ("Good reused epoch with different contents", oracle.good (source, {absent}, 2).value == !ap.inputs);
    expect ("Bad empty", oracle.bad (source, {}, 1).value == bddfalse);
    expect ("Bad epoch replacement", oracle.bad (source, {absent}, 2).value == bddtrue);
    expect ("Bad same epoch replacement", oracle.bad (source, {}, 2).value == bddfalse);
    const auto complete_before = rows->complete_rows ();
    expect ("K2 source rank 1 safe successor", oracle.unsafe (Rank {1, -1}).value == bddfalse);
    oracle.set_bound (1);
    expect ("K context invalidates queries", oracle.unsafe (Rank {1, -1}).value == ap.inputs);
    expect ("K keeps immutable P2 rows", rows->complete_rows () == complete_before);

    auto generic = std::make_shared<SpotRows> (GenericTransitionBuchi {g});
    oracle.reset (generic, ap, 1);
    expect ("mode replacement changes increment", oracle.unsafe (Rank {0}).value == ap.inputs);
    expect ("generic discovery does not change old key", generic->state_count () == 2);
    const auto cached_hits = oracle.metrics ().threshold_hits;
    oracle.threshold (Rank {0}, 1, 1);
    oracle.threshold (Rank {0, -1}, 1, 1);
    expect ("missing trailing coordinates share immutable rank identity", oracle.metrics ().threshold_hits > cached_hits);
    auto boolean = std::make_shared<SpotRows> (FrozenAcacia {g, 1});
    oracle.reset (boolean, ap, 3);
    expect ("coordinate layout replacement invalidates cap queries", oracle.unsafe (Rank {1, -1}).value == ap.inputs);
    auto replacement = graph (2, true);
    replacement->copy_ap_of (g);
    oracle.reset (std::make_shared<SpotRows> (FrozenAcacia {replacement, 2}), ap, 3);
    expect ("provider replacement invalidates every predicate", oracle.threshold (source, 1, 0).value == bddfalse);
    expect ("invalid rank is unknown", not oracle.threshold (Rank {4, -1}, 1, 0).value);
    oracle.set_bound (0);
    expect ("invalid K is unknown", not oracle.unsafe (source).value);

    for (int kind = 0; kind < 4; ++kind) {
      auto invalid = ap;
      if (kind == 0) invalid.inputs = bddtrue;
      if (kind == 1) invalid.inputs = invalid.outputs;
      if (kind == 2) invalid.inputs = !invalid.inputs;
      if (kind == 3) invalid.order.push_back (invalid.order.front ());
      oracle.reset (rows, invalid, 2);
      expect ("reject invalid worker partition/order", not oracle.unsafe (source).value);
    }
    auto outsider = spot::make_twa_graph (g->get_dict ());
    const int extra = outsider->register_ap ("outside-worker");
    oracle.reset (rows, ap, 2);
    expect ("model rejects outside AP", not oracle.model (bdd_ithvar (extra)).value);
    auto bad_guard = graph (1, false);
    bad_guard->copy_ap_of (g);
    bad_guard->new_edge (0, 0, bdd_ithvar (extra));
    oracle.reset (std::make_shared<SpotRows> (GenericTransitionBuchi {bad_guard}), ap, 2);
    expect ("guards outside worker inventory rejected", not oracle.unsafe (Rank {0}).value);

    std::weak_ptr<SpotRows> weak = rows;
    oracle.reset (rows, ap, 2);
    rows.reset ();
    expect ("oracle owns provider rows", not weak.expired ());
    expect ("cached guards retain dictionary", oracle.threshold (source, 1, 0).value == ap.inputs);
    oracle.reset (nullptr, ap, 2);
    expect ("reset releases old provider", weak.expired ());
  }

  void resource_tests () {
    auto g = graph (2, true);
    const auto ap = alphabet (g, true, true);
    g->new_edge (0, 1, ap.inputs);
    g->new_edge (1, 0, ap.outputs, {0});
    auto rows = std::make_shared<SpotRows> (FrozenAcacia {g, 2});
    Oracle measure {rows, ap, 2};
    expect ("measure complete query", measure.eq (Rank {1, 1}, Rank {1, 2}).value.has_value ());
    Oracle oracle {rows, ap, 2};
    QueryLimits limits;
    limits.max_steps = measure.metrics ().steps / 2;
    oracle.set_limits (limits);
    const auto limited = oracle.eq (Rank {1, 1}, Rank {1, 2});
    expect ("resource failure halfway through query", not limited.value && limited.unknown == Unknown::resource_limit
            && oracle.metrics ().bdd_operations > 0 && oracle.metrics ().steps == limits.max_steps);
    ++expected_unknowns;
    oracle.set_limits ({});
    expect ("no partial predicate cached after failure", oracle.eq (Rank {1, 1}, Rank {1, 2}).value
            == measure.eq (Rank {1, 1}, Rank {1, 2}).value);
    std::size_t checkpoints = 0;
    limits = {};
    limits.aborted = [] (void* data) { return ++*static_cast<std::size_t*> (data) > 7; };
    limits.abort_data = &checkpoints;
    oracle.set_limits (limits);
    const auto aborted = oracle.bad (Rank {0, 0}, {Rank {0, 0}}, 1);
    expect ("aborted query is unknown", not aborted.value && aborted.unknown == Unknown::aborted);
    ++expected_unknowns;
    oracle.set_limits ({});
    const auto allocation = oracle.query<bdd> ([&] (auto& b) -> bdd {
      (void) b.lor (ap.inputs, ap.outputs);
      throw std::bad_alloc {};
    });
    expect ("C++ allocation failure after BDD work", not allocation.value && allocation.unknown == Unknown::resource_limit);
    ++expected_unknowns;
    limits = {};
    limits.max_live_nodes = 0;
    oracle.set_limits (limits);
    expect ("node budget applies to cached verdicts", not oracle.losing (Rank {0, 0}, {}, 0).value);
    expect ("node budget applies to invariants", not oracle.invariant (Rank {0, 0}, {}, 0).value);
    expected_unknowns += 2;
    auto partial = std::make_shared<SpotRows> (FrozenAcacia {g, 2}, RowLimits {1, 10});
    oracle.reset (partial, ap, 2);
    oracle.set_limits ({});
    const auto incomplete = oracle.unsafe (Rank {0, 0});
    expect ("active row failure discards predicates", not incomplete.value && incomplete.unknown == Unknown::resource_limit
            && partial->complete_rows () == 1 && partial->state (1) == RowState::failed_or_resource_limited);
    ++expected_unknowns;
  }

  // Test-only finite-domain iteration of the P3 losing query. The header and
  // replay tool contain no game backend. Every WIN here must separately pass
  // the exact candidate-invariant test; an unresolved losing query is not a win.
  std::optional<bool> symbolic_fixed_k (Oracle& oracle, SpotRows& rows, std::int32_t K) {
    const auto domain = ranks (rows.safe_caps (K));
    std::vector<Rank> L;
    Epoch epoch = 0;
    while (true) {
      std::vector<Rank> added;
      for (const auto& r : domain) {
        if (up (r, L)) continue;
        const auto result = oracle.losing (r, L, epoch);
        if (not result.value) return std::nullopt;
        if (*result.value == Losing::proved_losing) added.push_back (r);
      }
      if (added.empty ()) break;
      L.insert (L.end (), added.begin (), added.end ());
      ++epoch;
    }
    if (up (rows.initial_rank (), L)) return false;
    std::vector<Rank> G;
    for (const auto& r : domain) if (not up (r, L)) G.push_back (r);
    const auto invariant = oracle.invariant (rows.initial_rank (), G, epoch);
    if (not invariant.value) return std::nullopt;
    expect ("finite-domain survivors form verified invariant", *invariant.value == Invariant::verified);
    if (*invariant.value != Invariant::verified) return std::nullopt;
    return true;
  }
  void random_games () {
    std::mt19937 rng {seed};
    for (unsigned game = 0; game < 5000; ++game) {
      const bool frozen = game % 2 == 0;
      const auto fixture = acacia::testing::spot_games::random_game (rng, game, frozen);
      const auto& g = fixture.graph;
      const auto& ap = fixture.alphabet;
      const auto K = fixture.K;
      const auto bool_threshold = fixture.bool_threshold;
      auto rows = frozen ? std::make_shared<SpotRows> (FrozenAcacia {g, bool_threshold})
                         : std::make_shared<SpotRows> (GenericTransitionBuchi {g});
      if (not finish_rows (*rows)) { ++game_unknowns; continue; }
      // Adapt complete P2 rows to the EXISTING explicit oracle's action payload.
      // No transition arithmetic is reproduced here.
      std::vector<std::vector<reference::action_vec>> actions;
      for (const auto& u : letters (ap, Variables::inputs)) {
        actions.emplace_back ();
        for (const auto& c : letters (ap, Variables::outputs)) {
          reference::action_vec action (rows->state_count ());
          for (StateId p = 0; p < rows->state_count (); ++p)
            for (const auto& edge : rows->row (p).row->edges)
              if (truth (edge.condition, u & c)) action[edge.destination].emplace_back (p, edge.increment);
          actions.back ().push_back (std::move (action));
        }
      }
      const auto expected = reference::solve_explicit_forward_game (
          reference::initial_vector (rows->state_count (), rows->initial_id ()), actions,
          static_cast<VECTOR_ELT_T> (K), frozen ? bool_threshold : rows->state_count ());
      Oracle oracle {rows, ap, K};
      const int before = bdd_varnum ();
      const auto actual = symbolic_fixed_k (oracle, *rows, K);
      expect ("random game AP-only manager", bdd_varnum () == before);
      if (not actual || expected.status == reference::forward_status::resource_limit) {
        ++game_unknowns;
        continue;
      }
      const bool expected_win = expected.status == reference::forward_status::win_k;
      if (expect ("fixed-seed game " + std::to_string (game), *actual == expected_win)) {
        ++agreeing_games;
        if (*actual) ++wins; else ++losses;
      }
    }
    expect ("at least 5000 completed agreeing games", agreeing_games >= 5000);
    expect ("both game verdicts exercised", wins > 0 && losses > 0);
  }

  int fatal_child (int code) {
    auto g = graph (1, false);
    const auto ap = alphabet (g, true, true);
    g->new_edge (0, 0, ap.inputs ^ ap.outputs, {0});
    auto rows = std::make_shared<SpotRows> (GenericTransitionBuchi {g});
    Oracle oracle {rows, ap, 1};
    struct Injection { unsigned calls = 0; int code; } injection {0, code};
    QueryLimits limits;
    limits.abort_data = &injection;
    limits.aborted = [] (void* data) {
      auto& state = *static_cast<Injection*> (data);
      if (++state.calls == 16) {
        // Inject through the installed linked BuDDy hook, after BDD work.
        auto handler = bdd_error_hook (nullptr);
        bdd_error_hook (handler);
        if (not handler) std::_Exit (7);
        handler (state.code);
        std::_Exit (8);  // returning from the hook would be unsound
      }
      return false;
    };
    oracle.set_limits (limits);
    const auto result = oracle.unsafe (Rank {0});
    std::cerr << "fatal fixture returned: checkpoints=" << injection.calls
              << " reason=" << unknown_name (result.unknown) << '\n';
    return 9;
  }
  int exhaust_child () {
    // A real linked-library allocation failure, in a fresh, small manager.
    if (bdd_init (101, 101) < 0) return 6;
    BuddyErrors errors;
    bdd_gbc_hook (nullptr);
    auto g = graph (1, false);
    WorkerAlphabet ap {bddtrue, bddtrue, bddtrue, {}};
    for (int i = 0; i < 16; ++i) {
      const int var = g->register_ap ("ap" + std::to_string (i));
      ap.order.push_back (var);
      ap.inputs &= bdd_ithvar (var);
    }
    ap.ap_vars = g->ap_vars ();
    auto rows = std::make_shared<SpotRows> (GenericTransitionBuchi {g});
    Oracle oracle {rows, ap, 1};
    bdd_setmaxnodenum (bdd_getallocnum () + 1);
    const auto result = oracle.query<bdd> ([&] (auto& b) {
      std::vector<bdd> retained;
      for (unsigned mask = 0; mask < 65536; ++mask) {
        bdd cube = bddtrue;
        for (unsigned i = 0; i < ap.order.size (); ++i)
          cube = b.land (cube, mask & (1U << i) ? bdd_ithvar (ap.order[i]) : bdd_nithvar (ap.order[i]));
        retained.push_back (cube);
      }
      return bdd (bddtrue);
    });
    (void) result;
    return 9;  // only the fatal hook's inconclusive exit is expected
  }
  void fatal_error_tests (const char* program) {
    // posix_spawn execs a fresh process. No child uses inherited manager state.
    for (const int code : {BDD_MEMORY, BDD_NODENUM, BDD_BREAK, 0}) {
      const std::string argument = std::to_string (code);
      char* args[] = {const_cast<char*> (program), const_cast<char*> (code ? "--fatal-buddy" : "--exhaust-buddy"),
                      const_cast<char*> (argument.c_str ()), nullptr};
      pid_t pid;
      if (not expect ("spawn fatal BuDDy fixture", posix_spawn (&pid, program, nullptr, nullptr, args, environ) == 0)) continue;
      int status = 0;
      expect ("wait for inconclusive child", waitpid (pid, &status, 0) == pid);
      expect ("BuDDy resource/abort never a decisive verdict", WIFEXITED (status)
              && WEXITSTATUS (status) == BuddyErrors::inconclusive_exit);
      ++expected_unknowns;
    }
  }
}  // namespace

int main (int argc, char** argv) {
  try {
    if (argc == 3 && std::string {argv[1]} == "--fatal-buddy") return fatal_child (std::stoi (argv[2]));
    if (argc == 3 && std::string {argv[1]} == "--exhaust-buddy") return exhaust_child ();
    const auto dictionary = spot::make_bdd_dict ();
    BuddyErrors errors;
    fixtures ();
    helper_and_cache_tests ();
    resource_tests ();
    fatal_error_tests (argv[0]);
    random_games ();
  }
  catch (const std::exception& error) {
    expect (std::string {"unexpected exception: "} + error.what (), false);
  }
  std::cout << "spot-letter-oracle: " << comparisons << " exhaustive rank/letter predicate comparisons; "
            << "seed=" << seed << "; agreeing_games=" << agreeing_games << " (win=" << wins << ", lose=" << losses
            << "); game_unknowns=" << game_unknowns << "; query_unknowns=" << query_unknowns
            << "; expected_failure_unknowns=" << expected_unknowns << '\n';
  if (failures) std::cerr << failures << " check(s) failed\n";
  return failures ? 1 : 0;
}
