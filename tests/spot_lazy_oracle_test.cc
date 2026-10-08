#include "research/explicit_forward_game.hh"
#include "solver/spot_lazy_game.hh"
#include "tiny_spot_game.hh"
#include "utils/verbose.hh"

#include <iostream>
#include <random>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

namespace {
  using namespace acacia::spot_lazy_game;
  namespace games = acacia::testing::spot_games;
  namespace reference = acacia::research;
  using acacia::OracleLayout;
  using Fields = std::map<std::string, std::string>;
  size_t predicates = 0, outcomes = 0, rejected = 0, interrupted = 0;

  void expect (bool value, const char* message) {
    if (!value)
      throw std::runtime_error (message);
  }
  template <typename T>
  T take (const letters::Result<T>& result) {
    if (!result.value) {
      if (result.error)
        std::rethrow_exception (result.error);
      throw std::runtime_error (std::string ("query must complete: ") +
                                letters::unknown_name (result.unknown));
    }
    return *result.value;
  }
  std::vector<Rank> ranks (unsigned n, int K) {
    std::vector<Rank> result;
    std::vector<Rank::Entry> entries;
    const auto add = [&] (auto&& self, unsigned q) -> void {
      if (q == n) {
        result.emplace_back (entries, K);
        return;
      }
      self (self, q + 1);
      for (int v = 0; v <= K; ++v) {
        entries.emplace_back (q, v);
        self (self, q + 1);
        entries.pop_back ();
      }
    };
    add (add, 0);
    return result;
  }
  Rank successor (RowStore& store, const Rank& source, bdd letter, int K) {
    std::vector<Rank::Entry> entries;
    for (auto [q, v] : source.entries ())
      for (const auto& edge : store.get (q).edges)
        if ((letter & edge.condition) != bddfalse)
          entries.emplace_back (edge.destination, std::min (K, v + int (edge.increment)));
    return Rank {entries, K};
  }
  bdd expected_threshold (RowStore& store, const Rank& source, rows::StateId q, int h, int K) {
    if (h <= -1)
      return bddtrue;
    if (h > K)
      return bddfalse;
    bdd result = bddfalse;
    for (auto [p, v] : source.entries ())
      for (const auto& edge : store.get (p).edges)
        if (edge.destination == q && v + int (edge.increment) >= h)
          result |= edge.condition;
    return result;
  }
  void compare_predicates (RowStore& store, const letters::WorkerAlphabet& a, int K,
                           bool exhaustive, std::mt19937& rng) {
    Reader reader {store, false, {}};
    Oracle scan {reader, store, a, K, OracleLayout::scan};
    Oracle grouped {reader, store, a, K, OracleLayout::grouped};
    const auto domain = ranks (store.cache->state_count (), K);
    const auto letters = games::letters (a, Variables::all);
    const auto count = exhaustive ? domain.size () : size_t (8);
    for (size_t i = 0; i < count; ++i) {
      const auto& source = domain[exhaustive ? i : rng () % domain.size ()];
      for (unsigned q = 0; q <= store.cache->state_count (); ++q)
        for (int h = -3; h <= K + 2; ++h) {
          const auto expected = expected_threshold (store, source, q, h, K);
          expect (take (scan.threshold (source, q, h)) == expected &&
                      take (grouped.threshold (source, q, h)) == expected,
                  "threshold layouts agree with the active-edge OR, including boundaries");
          ++predicates;
        }
      const auto targets = exhaustive ? domain.size () : size_t (8);
      for (size_t j = 0; j <= targets; ++j) {
        const Rank target = j == targets
                                ? Rank {{{rows::StateId (store.cache->state_count ()), 0}}, K}
                                : domain[exhaustive ? j : rng () % domain.size ()];
        bdd up = bddfalse, down = bddfalse, eq = bddfalse;
        for (const auto letter : letters) {
          const auto next = successor (store, source, letter, K);
          if (target.leq (next))
            up |= letter;
          if (next.leq (target))
            down |= letter;
          if (next == target)
            eq |= letter;
          expect (take (scan.evaluate (source, letter)) == next &&
                      take (grouped.evaluate (source, letter)) == next,
                  "evaluation preserves bottom, increments and saturation");
        }
        for (int repeat = 0; repeat < 2; ++repeat) {
          expect (
              take (scan.up (source, target)) == up && take (grouped.up (source, target)) == up,
              "upward preimage matches enumerated arithmetic");
          expect (take (scan.down (source, target)) == down &&
                      take (grouped.down (source, target)) == down,
                  "downward preimage matches enumerated arithmetic");
          expect (
              take (scan.eq (source, target)) == eq && take (grouped.eq (source, target)) == eq,
              "equality preimage matches enumerated arithmetic");
          predicates += 3;
        }
      }
    }
  }
  void compare_outcomes (RowStore& store, const letters::WorkerAlphabet& a, int K,
                         size_t bool_threshold) {
    std::vector<std::vector<reference::action_vec>> actions;
    for (const auto u : games::letters (a, Variables::inputs)) {
      actions.emplace_back ();
      for (const auto c : games::letters (a, Variables::outputs)) {
        reference::action_vec action (store.cache->state_count ());
        for (unsigned q = 0; q < store.cache->state_count (); ++q)
          for (const auto& edge : store.get (q).edges)
            if ((u & c & edge.condition) != bddfalse)
              action[edge.destination].emplace_back (q, edge.increment);
        actions.back ().push_back (std::move (action));
      }
    }
    const auto expected = reference::solve_explicit_forward_game (
        reference::initial_vector (store.cache->state_count (), store.cache->initial_id ()),
        actions, static_cast<VECTOR_ELT_T> (K), bool_threshold);
    for (const auto semantics :
         {ChoiceSemantics {SuccessorRelation::exact, OutputChoice::constant},
          ChoiceSemantics {SuccessorRelation::downward, OutputChoice::existential}}) {
      const auto scan = Search {store,
                                a,
                                K,
                                {},
                                semantics,
                                LosingInputSearch::off,
                                LeanVerifier::off,
                                OracleLayout::scan}
                            .solve ();
      const auto grouped = Search {store,
                                   a,
                                   K,
                                   {},
                                   semantics,
                                   LosingInputSearch::off,
                                   LeanVerifier::off,
                                   OracleLayout::grouped}
                               .solve ();
      expect (scan.failure == Unknown::none && grouped.failure == Unknown::none &&
                  scan.status == grouped.status &&
                  (scan.status == solver_detail::forward_result_status::win_k) ==
                      (expected.status == reference::forward_status::win_k),
              "representation-only fixed-K outcomes agree with the explicit game");
      expect (scan.nodes.size () == grouped.nodes.size () &&
                  scan.proofs.size () == grouped.proofs.size () &&
                  scan.expansions == grouped.expansions &&
                  scan.choices_created == grouped.choices_created,
              "representation-only search shape is preserved");
      for (size_t i = 0; i < scan.nodes.size (); ++i) {
        const auto& x = scan.nodes[i];
        const auto& y = grouped.nodes[i];
        expect (x.rank == y.rank && x.losing == y.losing && x.covered_inputs == y.covered_inputs &&
                    x.choices.size () == y.choices.size (),
                "rank and coverage traces are preserved");
        for (size_t j = 0; j < x.choices.size (); ++j)
          expect (x.choices[j].input_region == y.choices[j].input_region &&
                      x.choices[j].constant_output == y.choices[j].constant_output &&
                      x.choices[j].successor == y.choices[j].successor &&
                      x.choices[j].active == y.choices[j].active,
                  "choice traces are preserved");
      }
      for (auto layout : {OracleLayout::scan, OracleLayout::grouped}) {
        auto corrupt = grouped;
        if (grouped.status == solver_detail::forward_result_status::win_k) {
          corrupt.nodes.at (corrupt.initial).covered_inputs = bddfalse;
          expect (!verify_winning_certificate (store, a, K, corrupt, {}, semantics,
                                               LeanVerifier::off, layout)
                       .value,
                  "fresh verifier rejects corrupted coverage after warm search");
        }
        else {
          corrupt.proofs.at (*corrupt.initial_proof)
              .record.dependencies.push_back (*corrupt.initial_proof);
          expect (
              !verify_losing_proof (store, a, K, corrupt, {}, semantics, LeanVerifier::off, layout)
                   .value,
              "fresh verifier rejects corrupted chronology after warm search");
        }
        ++rejected;
      }
      ++outcomes;
    }
  }

  void frozen_destination_acceptance () {
    auto g = games::graph (3, true);
    const auto a = games::alphabet (g, true, true);
    g->new_edge (0, 1, a.inputs);
    g->new_edge (0, 1, a.outputs);
    g->new_edge (1, 2, bddtrue, {0});
    g->new_edge (2, 2, bddtrue);
    RowStore store {rows::FrozenAcacia {g, 2}, {}};
    Reader reader {store, false, {}};
    for (auto layout : {OracleLayout::scan, OracleLayout::grouped}) {
      Oracle oracle {reader, store, a, 2, layout};
      expect (take (oracle.threshold (Rank {{{0, 0}}, 2}, 1, 1)) == (a.inputs | a.outputs),
              "nonaccepting source increments into an accepting destination");
      expect (take (oracle.threshold (Rank {{{1, 0}}, 2}, 2, 1)) == bddfalse,
              "accepting source does not increment into a nonaccepting destination");
      expect (take (oracle.bad (Rank {{{1, 1}}, 2}, {}, 0)) == bddtrue,
              "Boolean unsafe aggregation uses cap zero");
      expect (take (oracle.eq (Rank {{{0, 2}}, 2}, Rank {{{1, 2}}, 2})) == (a.inputs | a.outputs),
              "equality at K includes contributions above K");
    }
  }

  void identity_and_lazy_suffixes () {
    auto g = games::graph (2, false);
    const auto a = games::alphabet (g, true, false);
    g->new_edge (0, 0, a.inputs);
    RowStore store {FixedBuchi {{g}, {}, {}}, {}};
    Reader reader {store, false, {}};
    Oracle oracle {reader, store, a, 64, OracleLayout::grouped};
    const Rank source {{{0, 61}}, 64};
    const Rank collision {{{1, 0}}, 64};
    expect (source.hash () == collision.hash () && source != collision,
            "fixture has an actual cached-rank hash collision");
    (void) take (oracle.evaluate (source, a.inputs));
    Fields fields;
    Reporter report {[&] (const auto& k, const auto& v) { fields[k] = v; }};
    oracle.report (report, "");
    expect (fields.at ("cached_thresholds") == "0", "prepare never eagerly constructs suffix ORs");
    expect (take (oracle.threshold (source, 0, 1)) == a.inputs, "first requested suffix is exact");
    oracle.report (report, "");
    expect (fields.at ("cached_thresholds") == "1", "only the requested threshold is cached");
    expect (take (oracle.up (source, source)) == a.inputs &&
                take (oracle.up (source, collision)) == bddfalse &&
                take (oracle.up (source, source)) == a.inputs,
            "full rank equality prevents colliding target IDs from aliasing");
    Rank temporary {{{0, 61}}, 64};
    expect (take (oracle.up (source, temporary)) == a.inputs, "equal rank copies share identity");
    temporary = collision;
    expect (take (oracle.up (source, temporary)) == bddfalse,
            "assignment at the same rank address cannot alias its previous identity");
    oracle.report (report, "");
    expect (fields.at ("interned_targets") == "2", "interner owns exactly two immutable values");
    const auto thresholds = fields.at ("cached_thresholds");
    oracle.clear_preimages (source);
    oracle.report (report, "");
    expect (fields.at ("interned_targets") == "0" && fields.at ("cached_preimages") == "0" &&
                fields.at ("cached_thresholds") == thresholds,
            "eviction reclaims every unreferenced target while retaining thresholds");
    expect (take (oracle.up (source, collision)) == bddfalse &&
                take (oracle.up (source, source)) == a.inputs,
            "reinterning colliding ranks after eviction remains exact");
  }

  void identity_retention () {
    auto g = games::graph (3, false);
    const auto a = games::alphabet (g, true, true);
    for (unsigned q = 0; q < 3; ++q) {
      g->new_edge (q, q, a.inputs, {0});
      g->new_edge (q, (q + 1) % 3, a.outputs);
    }
    RowStore store {FixedBuchi {{g}, {}, {}}, {}};
    store.enumerate_and_freeze ();
    Reader reader {store, false, {}};
    Oracle scan {reader, store, a, 64, OracleLayout::scan};
    Oracle grouped {reader, store, a, 64, OracleLayout::grouped};
    const Rank source {{{0, 61}}, 64}, other {{{1, 0}}, 64}, third {{{2, 1}}, 64};
    Fields fields;
    Reporter report {[&] (const auto& k, const auto& v) { fields[k] = v; }};
    const auto compare = [&] (const Rank& r, const Rank& target) {
      expect (take (grouped.up (r, target)) == take (scan.up (r, target)) &&
                  take (grouped.down (r, target)) == take (scan.down (r, target)) &&
                  take (grouped.eq (r, target)) == take (scan.eq (r, target)),
              "retained and reinterned targets agree with scan for every preimage kind");
      predicates += 3;
    };
    compare (source, source);
    compare (source, other);
    compare (other, source);
    grouped.report (report, "");
    const auto warm_thresholds = fields.at ("cached_thresholds");
    expect (fields.at ("interned_targets") == "2" && fields.at ("cached_preimages") == "9",
            "identities are shared across source ranks and all three preimage kinds");
    grouped.clear_preimages (source);
    scan.clear_preimages (source);
    grouped.report (report, "");
    expect (fields.at ("interned_targets") == "1" && fields.at ("cached_preimages") == "3" &&
                fields.at ("cached_thresholds") == warm_thresholds,
            "clearing one source reclaims only targets with no remaining memo references");
    compare (source, third);
    compare (other, source);
    grouped.report (report, "");
    expect (fields.at ("interned_targets") == "2" && fields.at ("cached_preimages") == "6",
            "new identities cannot alias a surviving source's preimages");
    grouped.retain_preimages ({other});
    scan.retain_preimages ({other});
    grouped.report (report, "");
    expect (fields.at ("interned_targets") == "1" && fields.at ("cached_preimages") == "3",
            "retaining a source reclaims targets referenced only by evicted sources");

    expect (take (grouped.evaluate (third, a.inputs & a.outputs)) ==
                take (scan.evaluate (third, a.inputs & a.outputs)),
            "preparing the churn source preserves exact evaluation");
    // A fully warm threshold cache bounds payload independently of churn history.
    Oracle warm {reader, store, a, 64, OracleLayout::grouped};
    for (const auto& r : {source, other, third})
      for (unsigned q = 0; q < 3; ++q)
        for (int h = 0; h <= 64; ++h)
          (void) take (warm.threshold (r, q, h));
    (void) take (warm.up (other, source));
    (void) take (warm.down (other, source));
    (void) take (warm.eq (other, source));
    const auto retained_payload_bound = warm.storage_bytes_estimate ();

    // Grow beyond several interner rehashes, then retain just the warm source.
    for (int i = 0; i < 6000; ++i)
      compare (source, Rank {{{0, i % 65}, {1, (i / 65) % 65}, {2, i / (65 * 65)}}, 64});
    grouped.report (report, "");
    expect (fields.at ("interned_targets") == "6001", "growth keeps distinct identities exact");
    const auto peak_bytes = std::stoull (fields.at ("oracle_payload_bytes_estimate"));
    const auto peak_rank_bytes = std::stoull (fields.at ("cache_rank_bytes"));
    grouped.clear_preimages (source);
    scan.clear_preimages (source);
    grouped.report (report, "");
    expect (
        fields.at ("interned_targets") == "1" && fields.at ("cached_preimages") == "3" &&
            std::stoull (fields.at ("oracle_payload_bytes_estimate")) < peak_bytes &&
            std::stoull (fields.at ("oracle_payload_bytes_estimate")) <= retained_payload_bound &&
            std::stoull (fields.at ("cache_rank_bytes")) < peak_rank_bytes,
        "eviction releases target ranks and excess buckets in existing byte reports");
    compare (other, source);
    const auto retained_bytes = std::stoull (fields.at ("cache_rank_bytes"));
    // Unique generated targets keep one source warm while two others churn.
    for (int i = 6000; i < 18000; ++i) {
      const Rank target {{{0, i % 65}, {1, (i / 65) % 65}, {2, i / (65 * 65)}}, 64};
      compare (source, target);
      compare (third, target);
      grouped.clear_preimages (source);
      scan.clear_preimages (source);
      grouped.report (report, "");
      expect (fields.at ("interned_targets") == "2" && fields.at ("cached_preimages") == "6",
              "a shared churn target remains live until its last source is evicted");
      if (i % 2 == 0) {
        grouped.clear_preimages (third);
        scan.clear_preimages (third);
      }
      else {
        grouped.retain_preimages ({other});
        scan.retain_preimages ({other});
      }
      grouped.report (report, "");
      expect (
          fields.at ("interned_targets") == "1" && fields.at ("cached_preimages") == "3" &&
              std::stoull (fields.at ("cache_rank_bytes")) == retained_bytes &&
              std::stoull (fields.at ("oracle_payload_bytes_estimate")) <= retained_payload_bound,
          "long successful churn retains exactly the warm source's identity");
      compare (other, source);
    }
    const auto warm_bytes = std::stoull (fields.at ("oracle_payload_bytes_estimate"));
    grouped.retain_preimages ({});
    scan.retain_preimages ({});
    grouped.report (report, "");
    expect (fields.at ("interned_targets") == "0" && fields.at ("cached_preimages") == "0" &&
                std::stoull (fields.at ("oracle_payload_bytes_estimate")) < warm_bytes,
            "evicting the last source releases the entire interner and its buckets");
    compare (source, other);
    compare (source, source);
    std::cout << "18000 distinct generated targets; 12000 bounded churn cycles\n";
  }

  void failures_and_counters () {
    auto g = games::graph (3, false);
    const auto a = games::alphabet (g, true, true);
    for (unsigned p = 0; p < 3; ++p)
      for (unsigned repeat = 0; repeat < 6; ++repeat) {
        g->new_edge (p, 0, a.inputs, {0});
        g->new_edge (p, 0, a.outputs);
        g->new_edge (p, 1, !a.inputs, {0});
      }
    g->new_edge (0, 2, bddtrue);
    const Rank source {{{0, 0}, {1, 1}, {2, 2}}, 3};
    const Rank target {{{0, 1}, {1, 2}}, 3};
    Fields scan_fields, grouped_fields;
    for (const auto layout : {OracleLayout::scan, OracleLayout::grouped}) {
      Fields& fields = layout == OracleLayout::scan ? scan_fields : grouped_fields;
      Reporter reporter {[&] (const auto& k, const auto& v) { fields[k] = v; }};
      RowStore store {FixedBuchi {{g}, {}, {}}, {}, reporter};
      Reader reader {store, false, {}};
      Oracle oracle {reader, store, a, 3, layout};
      expect (take (oracle.eq (source, target)) == take (oracle.eq (source, target)),
              "cached equality is exact");
      for (int h = -1; h <= 4; ++h)
        for (unsigned q = 0; q <= 3; ++q)
          (void) take (oracle.threshold (source, q, h));
      oracle.report (reporter, "");
      expect (fields.at ("preimage_calls") == "2" && fields.at ("preimage_hits") == "1",
              "call and miss counters expose exact memo hits");
      const auto baseline = take (oracle.eq (source, target));
      const auto steps = oracle.work_metrics ().steps;
      // Every checked-query failure, including unrelated BDD work, must clear
      // prepared thresholds and target identities together.
      letters::QueryLimits limits;
      limits.max_steps = 0;
      oracle.set_limits (limits);
      const auto failed = oracle.query<bdd> ([] (auto& b) { return b.lor (bddtrue, bddfalse); });
      expect (!failed.value && failed.unknown == Unknown::resource_limit,
              "failed checked query remains inconclusive");
      oracle.set_limits ({});
      (void) take (oracle.eq (source, Rank {{}, 3}));  // reuse IDs in the fresh generation
      expect (
          take (oracle.eq (source, target)) == baseline && oracle.work_metrics ().steps > steps,
          "cleared identities cannot alias predicates from an earlier generation");
      ++interrupted;
      const auto allocation = oracle.query<bdd> ([] (auto& b) -> bdd {
        (void) b.lor (bddtrue, bddfalse);
        throw std::bad_alloc {};
      });
      expect (!allocation.value && allocation.unknown == Unknown::resource_limit,
              "allocation failure clears warm prepared caches");
      oracle.report (reporter, "after_");
      expect (fields.at ("after_cached_thresholds") == "0" &&
                  fields.at ("after_cached_preimages") == "0" &&
                  fields.at ("after_interned_targets") == "0",
              "all predicate caches and identity generation clear together");
      const auto complete_steps = [&] {
        Oracle fresh {reader, store, a, 3, layout};
        (void) take (fresh.eq (source, target));
        return fresh.work_metrics ().steps;
      }();
      for (size_t stop = 0; stop < complete_steps; ++stop) {
        Oracle fresh {reader, store, a, 3, layout};
        letters::QueryLimits budget;
        budget.max_steps = stop;
        fresh.set_limits (budget);
        const auto result = fresh.eq (source, target);
        expect (!result.value && result.unknown == Unknown::resource_limit,
                "every mid-query resource stop is inconclusive");
        fresh.set_limits ({});
        expect (take (fresh.eq (source, target)) == baseline,
                "no partial prepared group, suffix OR or preimage survives a failed query");
        ++interrupted;
      }
      size_t checkpoints = 0;
      limits = {};
      limits.aborted = [] (void* p) { return ++*static_cast<size_t*> (p) == 90; };
      limits.abort_data = &checkpoints;
      Oracle fresh {reader, store, a, 3, layout};
      fresh.set_limits (limits);
      const auto cancelled = fresh.eq (source, target);
      expect (!cancelled.value && cancelled.unknown == Unknown::aborted,
              "abort during preparation is inconclusive");
      fresh.set_limits ({});
      expect (take (fresh.eq (source, target)) == baseline, "abort leaves no partial predicate");
      limits = {};
      limits.max_live_nodes = 0;
      fresh.set_limits (limits);
      expect (!fresh.eq (source, target).value, "node limits apply to warm caches");
      ++interrupted;
    }
    expect (std::stoull (grouped_fields.at ("prepared_levels")) <
                    std::stoull (scan_fields.at ("prepared_levels")) &&
                std::stoull (grouped_fields.at ("threshold_levels_scanned")) <
                    std::stoull (scan_fields.at ("threshold_levels_scanned")),
            "grouping and suffix scans reduce measured work");
    expect (grouped_fields.at ("preimage_key_entries_copied") == "0" &&
                grouped_fields.at ("target_intern_misses") == "1" &&
                scan_fields.at ("preimage_key_entries_copied") == "4",
            "grouped memo keys intern a target once and never copy per query");
    for (auto layout : {OracleLayout::scan, OracleLayout::grouped}) {
      RowStore partial {FixedBuchi {{g}, {}, {}}, rows::RowLimits {1, 100}};
      Reader reader {partial, false, {}};
      Oracle oracle {reader, partial, a, 3, layout};
      const auto result = oracle.eq (source, target);
      expect (!result.value && result.unknown == Unknown::resource_limit,
              "incomplete active rows never publish preparation");
      ++interrupted;
    }
    std::cout << "scan/grouped level visits: " << scan_fields.at ("threshold_levels_scanned")
              << '/' << grouped_fields.at ("threshold_levels_scanned") << '\n';
  }
}  // namespace

int main () {
  try {
    const auto dict = spot::make_bdd_dict ();
    letters::BuddyErrors errors;
    std::mt19937 rng {games::seed};
    frozen_destination_acceptance ();
    identity_and_lazy_suffixes ();
    identity_retention ();
    failures_and_counters ();
    for (unsigned game = 0; game < 160; ++game) {
      const bool frozen = game % 2 == 0;
      const auto fixture = games::random_game (rng, game, frozen);
      auto store = frozen ? std::make_unique<RowStore> (
                                rows::FrozenAcacia {fixture.graph, fixture.bool_threshold},
                                rows::RowLimits {})
                          : std::make_unique<RowStore> (FixedBuchi {{fixture.graph}, {}, {}},
                                                        rows::RowLimits {});
      store->enumerate_and_freeze ();
      compare_predicates (*store, fixture.alphabet, fixture.K, game < 16, rng);
      compare_outcomes (*store, fixture.alphabet, fixture.K, fixture.bool_threshold);
    }
    std::cout << predicates << " threshold/preimage comparisons; " << outcomes
              << " fixed-K matches; " << rejected << " corrupt certificates rejected; "
              << interrupted << " inconclusive stops; seed=" << games::seed << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 1;
  }
}
