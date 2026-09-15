// Unmeasured correctness reference only. This is not a C4/C5 timing arm.
// Compile the research specialization in isolation without its CLI entry point.
#define ACACIA_PROVIDER_REPLAY_TESTING
#include "research/spot_provider_replay.cc"

#include "research/explicit_forward_game.hh"

#include <fcntl.h>
#include <random>
#include <type_traits>
using namespace replay;
static_assert (not std::is_aggregate_v<SparseChoice>);

constexpr ChoiceSemantics all_semantics[] {
    {SuccessorRelation::exact, OutputChoice::constant},
    {SuccessorRelation::exact, OutputChoice::existential},
    {SuccessorRelation::downward, OutputChoice::constant},
    {SuccessorRelation::downward, OutputChoice::existential}};

void expect (bool condition, const std::string& name) {
  if (not condition)
    throw std::runtime_error ("FAIL: " + name);
}

void check_verifier_split (const Fields& fields) {
  for (const auto* key : {"queries", "steps", "bdd_operations"}) {
    std::size_t sum = 0;
    for (const auto* phase : {"traversal", "invariant", "proof_bad"})
      sum += std::stoull (fields.at (std::string ("verify_") + phase + "_" + key));
    expect (sum == std::stoull (fields.at (std::string ("verify_") + key)),
            "verifier phases partition the cumulative " + std::string (key));
  }
}

void check_solve_counters (const SolveResult& result, const Fields& fields) {
  expect (result.scan_tombstones + result.scan_prefilter_rejects + result.scan_exact_compares ==
              result.subsumption_nodes_checked, "scan buckets partition every loop iteration");
  expect (result.scan_exact_compares >= result.subsumption_nodes_invalidated,
          "every invalidation requires an exact comparison");
  expect (result.proofs_total == result.proofs.size () &&
          result.proofs_in_initial_cone <= result.proofs_total, "proof cone fits the certificate");
  std::size_t sum = 0, maximum = 0;
  std::set<std::size_t> cone;
  if (result.initial_proof) cone.insert (*result.initial_proof);
  // Independent chronological propagation, rather than the production DFS.
  for (auto it = result.proofs.rbegin (); it != result.proofs.rend (); ++it) {
    const auto& deps = it->record.dependencies;
    sum += deps.size ();
    maximum = std::max (maximum, deps.size ());
    if (cone.contains (it->record.id)) cone.insert (deps.begin (), deps.end ());
  }
  expect (result.proofs_in_initial_cone == cone.size () &&
          result.dependency_list_len_sum == sum && result.dependency_list_len_max == maximum,
          "shape counters describe the recorded dependencies, including an absent root");
  Fields published;
  rank_metrics (result, Reporter {[&] (const auto& k, const auto& v) { published[k] = v; }});
  for (const auto* key : {"scan_tombstones", "scan_prefilter_rejects", "scan_exact_compares",
                         "proofs_total", "proofs_in_initial_cone", "dependency_list_len_sum",
                         "losing_journal_rank_bytes", "incremental_bad_cache_bytes",
                         "incremental_bad_journal_replay", "incremental_bad_cache_hits",
                         "incremental_bad_full_rebuilds",
                         "dependency_list_len_max"}) {
    expect (fields.at (key) == published.at (key),
            "destructor and SolveResult reader agree on " + std::string (key));
    expect (std::count (columns.begin (), columns.end (), key) == 1,
            "replay TSV contains exactly one column for " + std::string (key));
  }
  check_verifier_split (fields);
  const bool win = result.status == forward_result_status::win_k;
  for (const auto* key : {"queries", "steps", "bdd_operations"}) {
    const auto suffix = std::string ("_") + key;
    expect (std::stoull (fields.at ("verify_proof_bad" + suffix)) ==
                (win ? 0 : std::stoull (fields.at ("verify" + suffix))),
            "only the losing verifier charges the proof loop");
    if (not win)
      expect (fields.at ("verify_traversal" + suffix) == "0" &&
              fields.at ("verify_invariant" + suffix) == "0", "losing replay resets winning phases");
    for (const auto* phase : {"traversal", "invariant", "proof_bad"})
      expect (std::count (columns.begin (), columns.end (), "verify_" + std::string (phase) + suffix)
                  == 1, "verifier split is retained in the replay TSV");
  }
  if (win) {
    std::size_t queries = 1;  // maximal-antichain construction
    std::set<RankNodeId> reached {result.initial};
    std::vector<RankNodeId> todo {result.initial};
    while (not todo.empty ()) {
      const auto id = todo.back ();
      todo.pop_back ();
      ++queries;  // complete_rows
      for (const auto& choice : result.nodes[id].choices)
        if (choice.active) {
          queries += 3;  // reachability, output projection, coverage
          if (reached.insert (choice.successor).second) todo.push_back (choice.successor);
        }
    }
    expect (std::stoull (fields.at ("verify_traversal_queries")) == queries &&
            fields.at ("verify_invariant_queries") == "1", "winning query counts follow the obligations");
  }
  else {
    std::size_t queries = result.proofs.size ();  // chronology for EVERY proof
    for (const auto& proof : result.proofs)
      if (proof.record.reason == losing_reason::env_losing_input) queries += 3;
    expect (std::stoull (fields.at ("verify_proof_bad_queries")) == queries,
            "losing query count includes every proof, its rows, Bad and input restriction");
  }
}

namespace acacia::spot_lazy_game {
// SearchTestAccess exposes search state and operations under the replay-test
// build guard. Its legacy_* helpers are a test-only reference implementation
// used to compare against production expansion.
struct SearchTestAccess {
    static auto& result (Search& s) { return s.result_; }
    static auto& oracle (Search& s) { return s.oracle_; }
    static auto& journal (Search& s) { return s.oracle_.search_loss_journal_; }
    static auto& losing (Search& s) { return s.losing_; }
    static auto& bad_cache (Search& s) { return s.oracle_.search_bad_cache_; }
    static size_t bad_journal_replay (Search& s) { return s.oracle_.search_bad_journal_replay_; }
    static bool prepared_empty (Search& s) { return s.oracle_.prepared_.empty (); }
    static void bad_budget (Search& s, size_t bytes) { s.oracle_.search_bad_budget_ = bytes; }
    static void insert_loss (Search& s, const Rank& rank) {
      s.enqueue_loss (s.intern (rank), losing_reason::env_unsafe);
    }
    static auto search_bad (Search& s, const Rank& rank) {
      return s.oracle_.search_bad_from_loss_journal (rank, s.losing_.ranks ());
    }
    static auto search_bad (Search& s, const Rank& rank, bdd missing) {
      return s.oracle_.search_bad_from_loss_journal (rank, s.losing_.ranks (), missing);
    }
    static void check_bad (Search& s, const Rank& rank) {
      Reader reader {s.view_, false, {}};
      Oracle fresh {reader, s.view_, s.alphabet_, s.K_};
      const auto expected = detail::take (fresh.bad (rank, s.losing_.ranks (), 0));
      expect (detail::take (search_bad (s, rank)) == expected,
              "journal union equals a fresh aggregate over the live antichain");
      expect (journal (s).size () == s.losing_.insertions,
              "journal contains exactly the genuinely inserted generators");
    }
    static void drain (Search& s) { s.propagate_losses (); }
    static void initialize (Search& s) {
      s.result_.initial = s.intern (s.rows_->initial_rank ());
      detail::take (s.oracle_.query<bool> ([] (auto&) { return true; }));
    }
    static SolveResult scan_buckets (RowStore& store, const letters::WorkerAlphabet& alphabet) {
      Search s {store, alphabet, 2};
      s.intern (Rank {{{0, 1}}, 2});
      const auto tombstone = s.intern (Rank {{{2, 0}}, 2});
      s.result_.nodes[tombstone].losing = true;
      s.intern (Rank {{}, 2});                  // support rejection
      s.intern (Rank {{{1, 0}}, 2});            // mass rejection
      s.intern (Rank {{{1, 1}}, 2});            // equal mass, exact rejection
      s.intern (Rank {{{0, 1}, {1, 0}}, 2});    // merge join, invalidated
      s.enqueue_loss (0, losing_reason::env_unsafe);
      s.publish_counters ();
      return std::move (s.result_);
    }
    static void loss (Search& s, RankNodeId target) {
      s.result_.nodes.at (target).losing = true;
      s.losses_.push_back (target);
    }
    static void check_coverage (Search& s) {
      for (RankNodeId id = 0; id < s.result_.nodes.size (); ++id)
        expect (s.reconstruct_coverage (id) == s.result_.nodes[id].covered_inputs,
                "incremental coverage equals a fresh left-to-right OR");
    }
    static std::size_t activate (Search& s, RankNodeId source, bdd candidate, RankNodeId target) {
      auto& node = s.result_.nodes.at (source);
      const auto region = detail::take (s.oracle_.query<bdd> ([&] (auto& b) {
        return b.land (candidate, b.negate (node.covered_inputs));
      }));
      expect (region != bddfalse && not s.result_.nodes.at (target).losing,
              "activation admits only missing inputs with a live target");
      const auto id = node.choices.size ();
      node.choices.push_back (s.semantics_.output_choice == OutputChoice::constant
                                 ? SparseChoice::constant (region, bddtrue, target)
                                 : SparseChoice::existential (region, target));
      s.result_.nodes[target].incoming.push_back ({source, id});
      node.covered_inputs = detail::take (s.oracle_.query<bdd> ([&] (auto& b) {
        return b.lor (node.covered_inputs, region);
      }));
      check_coverage (s);
      return id;
    }
    static void legacy_rebuild (Search& s, RankNodeId id) {
      auto& node = s.result_.nodes[id];
      node.covered_inputs = detail::take (s.oracle_.query<bdd> ([&] (auto& b) {
        bdd covered = bddfalse;
        for (auto& choice : node.choices) {
          b.step ();
          if (s.result_.nodes[choice.successor].losing)
            choice.active = false;
          if (choice.active)
            covered = b.lor (covered, choice.input_region);
        }
        return covered;
      }));
    }
    static void legacy_drain (Search& s) {
      while (not s.losses_.empty ()) {
        const auto id = s.losses_.front ();
        std::set<RankNodeId> affected;
        for (const auto& ref : s.result_.nodes[id].incoming) {
          auto& choice = s.result_.nodes[ref.source].choices[ref.choice];
          if (choice.active && choice.successor == id) {
            choice.active = false;
            affected.insert (ref.source);
          }
        }
        for (const auto source : affected) {
          legacy_rebuild (s, source);
          if (not s.result_.nodes[source].losing) {
            ++s.result_.reopened_sources;
            if (not s.result_.nodes[source].queued)
              ++s.reopen_enqueues_;
            s.enqueue (source);
          }
        }
        s.losses_.pop_front ();
      }
    }
    static void same_decisions (Search& s, Search& ref) {
      const auto& a = s.result_;
      const auto& b = ref.result_;
      expect (s.open_ == ref.open_ && s.losses_ == ref.losses_ &&
              s.interned_ == ref.interned_ && s.losing_.ranks () == ref.losing_.ranks () &&
              s.losing_.proof_ids () == ref.losing_.proof_ids (),
              "S3 preserves queue order, interning and losing generators at every step");
      expect (a.initial == b.initial && a.initial_proof == b.initial_proof &&
              a.expansions == b.expansions && a.choices_created == b.choices_created &&
              a.nodes.size () == b.nodes.size () && a.proofs.size () == b.proofs.size () &&
              s.reopen_enqueues_ == ref.reopen_enqueues_ &&
              s.subsumption_scans_ == ref.subsumption_scans_ &&
              s.nodes_checked_ == ref.nodes_checked_ && s.nodes_invalidated_ == ref.nodes_invalidated_,
              "S3 preserves search decisions and actual reopen enqueues");
      // Legacy rebuilding eagerly cleared choices for other pending targets,
      // suppressing their later reopen attempts. The previous sprint's S3
      // accounts for each event; queued flags still ensure exactly the same
      // actual enqueues above.
      expect (a.reopened_sources >= b.reopened_sources,
              "explicit invalidations retain every legacy reopen attempt");
      for (std::size_t id = 0; id < a.nodes.size (); ++id) {
        const auto& x = a.nodes[id];
        const auto& y = b.nodes[id];
        expect (x.rank == y.rank && x.active_rows_complete == y.active_rows_complete &&
                x.losing == y.losing && x.queued == y.queued &&
                x.covered_inputs == y.covered_inputs && x.choices.size () == y.choices.size () &&
                x.incoming.size () == y.incoming.size (), "S3 preserves every node");
        for (std::size_t j = 0; j < x.choices.size (); ++j) {
          const auto& c = x.choices[j];
          const auto& d = y.choices[j];
          expect (c.input_region == d.input_region && c.constant_output == d.constant_output &&
                  c.successor == d.successor && c.active == d.active,
                  "S3 preserves every choice, output, target and invalidation");
        }
        for (std::size_t j = 0; j < x.incoming.size (); ++j)
          expect (x.incoming[j].source == y.incoming[j].source &&
                  x.incoming[j].choice == y.incoming[j].choice,
                  "S3 preserves reverse-edge order");
      }
      for (std::size_t id = 0; id < a.proofs.size (); ++id) {
        const auto& x = a.proofs[id];
        const auto& y = b.proofs[id];
        expect (x.rank == y.rank && x.input == y.input && x.rows == y.rows &&
                x.record.id == y.record.id && x.record.reason == y.record.reason &&
                x.record.node == y.record.node && x.record.witness == y.record.witness &&
                x.record.dependencies == y.record.dependencies,
                "S3 preserves proof allocation, witnesses, dependencies and row obligations");
      }
    }
    static SolveResult compare_rebuild (RowStore& store, const letters::WorkerAlphabet& alphabet,
                                       int K, ChoiceSemantics semantics, LosingInputSearch mode) {
      Search s {store, alphabet, K, {}, semantics, mode};
      Search ref {store, alphabet, K, {}, semantics, mode};
      detail::take (detail::checked<bool> ([&] {
        store.phase = Phase::search;
        s.result_.initial = s.intern (s.rows_->initial_rank ());
        ref.result_.initial = ref.intern (ref.rows_->initial_rank ());
        for (;;) {
          s.propagate_losses ();
          legacy_drain (ref);
          same_decisions (s, ref);
          check_coverage (s);
          if (s.result_.nodes[s.result_.initial].losing || s.open_.empty ())
            break;
          for (auto* search : {&s, &ref}) {
            const auto id = search->open_.front ();
            search->open_.pop_front ();
            search->result_.nodes[id].queued = false;
            ++search->result_.expansions;
            // All losses have drained. Rebuilding before the unchanged setup
            // is equivalent to the old rebuild immediately after that setup.
            if (search == &ref)
              legacy_rebuild (ref, id);
            search->expand (id);
          }
          same_decisions (s, ref);
          check_coverage (s);
        }
        return true;
      }));
      // Finish through the real solve()/verification boundary in both cases.
      auto actual = s.solve ();
      const auto expected = ref.solve ();
      expect (actual.status == expected.status && actual.failure == expected.failure &&
              actual.generators == expected.generators, "S3 preserves verified certificates");
      return actual;
    }
    static SolveResult compare_incremental_bad (RowStore& store,
                                                const letters::WorkerAlphabet& alphabet,
                                                int K, ChoiceSemantics semantics,
                                                LosingInputSearch mode, LeanVerifier lean) {
      Search s {store, alphabet, K, {}, semantics, mode, lean, IncrementalBad::on};
      Search ref {store, alphabet, K, {}, semantics, mode, lean, IncrementalBad::off};
      detail::take (detail::checked<bool> ([&] {
        store.phase = Phase::search;
        initialize (s);
        initialize (ref);
        for (;;) {
          s.propagate_losses ();
          ref.propagate_losses ();
          same_decisions (s, ref);
          expect (s.result_.reopened_sources == ref.result_.reopened_sources,
                  "D1b also preserves every reopen attempt");
          if (s.result_.nodes[s.result_.initial].losing || s.open_.empty ())
            break;
          for (auto* search : {&s, &ref}) {
            const auto id = search->open_.front ();
            search->open_.pop_front ();
            search->result_.nodes[id].queued = false;
            ++search->result_.expansions;
            search->expand (id);
          }
          same_decisions (s, ref);
        }
        return true;
      }));
      auto actual = s.solve ();
      const auto expected = ref.solve ();
      expect (actual.status == expected.status && actual.failure == expected.failure &&
              actual.generators == expected.generators &&
              actual.pending_loss == expected.pending_loss &&
              actual.pending_expansion == expected.pending_expansion,
              "D1b preserves verified certificates and verdicts");
      expect (expected.incremental_bad_cache_bytes == 0 &&
              expected.losing_journal_rank_bytes == 0 &&
              expected.incremental_bad_cache_hits == 0 &&
              expected.incremental_bad_journal_replay == 0 &&
              expected.incremental_bad_full_rebuilds == 0,
              "disabled D1b retains no journal or incremental cache and performs no replay");
      expect (actual.incremental_bad_full_rebuilds == 0,
              "small differential games fit the incremental cache budget");
      return actual;
    }
};
} // namespace acacia::spot_lazy_game

std::vector<bdd> valuations (const letters::WorkerAlphabet& a, bdd vars) {
  std::vector<bdd> out {bddtrue};
  for (int v : a.order) {
    if (bdd_exist (vars, bdd_ithvar (v)) == vars)
      continue;
    std::vector<bdd> next;
    for (auto p : out) {
      next.push_back (p & bdd_nithvar (v));
      next.push_back (p & bdd_ithvar (v));
    }
    out = std::move (next);
  }
  return out;
}
// Independent arithmetic contract, including deactivated choices: target loss
// does not change the promise originally made by a region. Only this tiny test
// oracle enumerates outputs to check existential choices.
int choice_regions (RowStore& store, const letters::WorkerAlphabet& a, int K,
                    const SolveResult& actual, std::size_t& region_inputs) {
  for (const auto& node : actual.nodes)
    for (const auto& choice : node.choices) {
      const bool constant = actual.semantics.output_choice == OutputChoice::constant;
      if (choice.constant_output.has_value () != constant)
        return 6;
      const auto outputs = constant ? std::vector<bdd> {*choice.constant_output}
                                    : valuations (a, a.outputs);
      const auto& target = actual.nodes.at (choice.successor).rank;
      for (auto u : valuations (a, a.inputs)) {
        if ((u & choice.input_region) == bddfalse)
          continue;
        bool below = false, equal = false;
        for (auto c : outputs) {
          const auto post = rows::evaluate_sparse (store.cache, a, node.rank, u & c, K);
          if (not post.value)
            return 6;
          below |= post.value->leq (target);
          equal |= *post.value == target;
        }
        if (not below)
          return 6;
        if (actual.semantics.successor_relation == SuccessorRelation::exact && not equal)
          return 7;
        ++region_inputs;
      }
    }
  return 0;
}

bdd check_losing_inputs (RowStore& store, const letters::WorkerAlphabet& a, int K,
                        const Rank& rank, const std::vector<Rank>& earlier,
                        std::size_t& input_checks) {
  Reader reader {store, false, {}};
  Oracle oracle {reader, store, a, K};
  const auto projected = oracle.bad (rank, earlier, earlier.size (), bddtrue);
  expect (projected.value.has_value (), "checked universal projection succeeds");
  const auto [bad, H] = *projected.value;
  bdd expected_H = bddfalse;
  const auto inputs = valuations (a, a.inputs);
  for (auto u : inputs) {
    bool every_output_bad = true;
    for (auto c : valuations (a, a.outputs)) {
      const auto post = rows::evaluate_sparse (store.cache, a, rank, u & c, K);
      expect (post.value.has_value (), "independent successor arithmetic succeeds");
      bool unsafe_or_losing = not reader.is_safe (*post.value, K);
      for (const auto& loss : earlier)
        unsafe_or_losing |= loss.leq (*post.value);
      expect (((bad & u & c) != bddfalse) == unsafe_or_losing,
              "Bad matches unsafe or already-losing successor arithmetic");
      every_output_bad &= unsafe_or_losing;
    }
    expect (((H & u) != bddfalse) == every_output_bad,
            "H contains an input exactly when every output is bad");
    if (every_output_bad)
      expected_H |= u;
    ++input_checks;
  }
  expect (H == expected_H, "universal projection leaves exactly the losing inputs free");
  const bdd missing = inputs.front ();
  const auto masked = oracle.bad (rank, earlier, earlier.size (), missing);
  expect (masked.value && masked.value->first == bad && masked.value->second == (missing & H),
          "D excludes covered inputs and shares the same Bad predicate");
  return H;
}

void check_losing_certificate (RowStore& store, const letters::WorkerAlphabet& a, int K,
                               const SolveResult& result) {
  for (auto lean : {LeanVerifier::off, LeanVerifier::on}) {
    const auto checked = verify_losing_proof (store, a, K, result, {}, result.semantics, lean);
    expect (checked.value && *checked.value, "losing proof independently replays with either cache policy");
  }
  for (std::size_t id = 0; id < result.proofs.size (); ++id) {
    const auto& proof = result.proofs[id];
    expect (proof.record.id == id, "losing proof IDs are chronological");
    for (auto dep : proof.record.dependencies)
      expect (dep < id, "losing proof depends only on earlier proofs");
    if (proof.record.reason == losing_reason::env_losing_input) {
      Reader reader {store, false, {}};
      Oracle oracle {reader, store, a, K};
      expect (proof.input.has_value (), "losing input proof stores a witness");
      const auto total = oracle.restrict_total (bddtrue, *proof.input, Variables::inputs);
      expect (total.value && *total.value == bddtrue, "stored witness is a total input cube");
    }
  }
}

void check_lean_dependencies (const SolveResult& full, const SolveResult& lean) {
  expect (full.status == lean.status && lean.failure == Unknown::none &&
          full.proofs.size () == lean.proofs.size () && full.initial_proof == lean.initial_proof &&
          full.expansions == lean.expansions && full.choices_created == lean.choices_created &&
          full.nodes.size () == lean.nodes.size (), "lean verification preserves search decisions");
  for (std::size_t id = 0; id < full.proofs.size (); ++id) {
    const auto& before = full.proofs[id];
    const auto& after = lean.proofs[id];
    expect (before.record.id == after.record.id && before.record.node == after.record.node &&
            before.record.reason == after.record.reason && before.rank == after.rank &&
            before.input == after.input && before.rows == after.rows,
            "lean proofs retain their identity and independent row obligations");
    const auto& candidates = before.record.dependencies;
    const auto& recorded = after.record.dependencies;
    expect (std::includes (candidates.begin (), candidates.end (), recorded.begin (), recorded.end ()),
            "lean dependencies are an ordered subset of the same option-off proof");
    if (after.record.reason != losing_reason::env_losing_input)
      expect (candidates == recorded, "unsafe and subsumption dependencies are unchanged");
  }
  expect (lean.dependency_list_len_sum <= full.dependency_list_len_sum &&
          lean.dependency_list_len_max <= full.dependency_list_len_max,
          "recorded dependency counters reflect the smaller certificate");
}

int differential (const Reporter& report, ChoiceSemantics semantics) {
  unsigned checks = 0, corruptions = 0, arithmetic = 0;
  unsigned redundant_loss_events = 0;
  std::size_t losing_events = 0, antichain_insertions = 0;
  std::size_t broad_scans = 0, nodes_scanned = 0;
  std::size_t subsumption_queries = 0, subsumption_hits = 0;
  std::size_t expansions_total = 0, choices_total = 0, nodes_total = 0;
  std::size_t reopened_total = 0, invalidated_total = 0, removals_total = 0;
  std::size_t reopen_enqueues_total = 0, prefilter_skips_total = 0;
  std::size_t region_inputs = 0;
  std::size_t losing_input_checks = 0, symbolic_region_inputs = 0;
  for (const char* f :
       {"true", "false", "F a", "G a", "GF a", "GF a & GF b", "G(a -> F b)", "(a U b) | G c"}) {
    const auto dict = spot::make_bdd_dict ();
    auto p = spot::ltl_to_taa (spot::parse_infix_psl (f).f, dict, false);
    auto w = std::make_shared<lazy::LazyBuchiView> (p);
    Fields observed;
    RowStore store {w, {}, Reporter {[&] (const auto& key, const auto& value) {
      observed[key] = value;
      report.put (key, value);
    }}};
    store.enumerate_and_freeze ();
    const auto n = store.cache->state_count ();
    for (unsigned mask = 0; mask < (1u << p->ap ().size ()); ++mask) {
      Options opt;
      std::string part;
      for (size_t i = 0; i < p->ap ().size (); ++i)
        part += mask & (1u << i) ? 'u' : 'c';
      opt.partition = part;
      const auto a = alphabet (w, opt, report);
      std::vector<std::vector<acacia::research::action_vec>> table;
      for (auto u : valuations (a, a.inputs)) {
        table.emplace_back ();
        for (auto c : valuations (a, a.outputs)) {
          acacia::research::action_vec action (n);
          for (size_t q = 0; q < n; ++q)
            for (const auto& e : store.get (q).edges)
              if ((e.condition & u & c) != bddfalse)
                action[e.destination].emplace_back (q, e.increment);
          table.back ().push_back (std::move (action));
        }
      }
      for (int K : {1, 2, 3}) {
        const auto ref = acacia::research::solve_explicit_forward_game (
            acacia::research::initial_vector (n, store.cache->initial_id ()), table,
            static_cast<VECTOR_ELT_T> (K), n);
        // Exercise the actual omitted-option default in each build as well as
        // all four explicit combinations across this sweep.
        const auto actual = semantics == default_choice_semantics
                                ? Search {store, a, K}.solve ()
                                : Search {store, a, K, {}, semantics}.solve ();
        if (actual.semantics != semantics)
          return 2;
        const bool win = ref.status == acacia::research::forward_status::win_k;
        if (actual.status != (win ? solver_detail::forward_result_status::win_k
                                  : solver_detail::forward_result_status::lose_k)) {
          std::cerr << "MISMATCH " << f << ' ' << part << ' ' << K << '\n';
          return 1;
        }
        check_solve_counters (actual, observed);
        const auto symbolic = Search {store, a, K, {}, semantics, LosingInputSearch::on}.solve ();
        check_solve_counters (symbolic, observed);
        expect (symbolic.status == actual.status && symbolic.failure == Unknown::none,
                "S2 on/off fixed-K outcomes agree for every automaton, partition and mode");
        for (auto mode : {LosingInputSearch::off, LosingInputSearch::on}) {
          const auto full = Search {store, a, K, {}, semantics, mode, LeanVerifier::off}.solve ();
          check_solve_counters (full, observed);
          const auto lean = Search {store, a, K, {}, semantics, mode, LeanVerifier::on}.solve ();
          check_solve_counters (lean, observed);
          expect (lean.status == actual.status, "V1 on/off matches every fixed-K explicit game");
          check_lean_dependencies (full, lean);
          if (not win) {
            check_losing_certificate (store, a, K, full);
            check_losing_certificate (store, a, K, lean);
          }
          const auto incremental = SearchTestAccess::compare_rebuild (store, a, K, semantics, mode);
          expect (incremental.status == actual.status && incremental.failure == Unknown::none,
                  "S3 and legacy rebuild match the explicit game with S2 off and on");
          for (auto policy : {LeanVerifier::off, LeanVerifier::on}) {
            const auto cached = SearchTestAccess::compare_incremental_bad (
                store, a, K, semantics, mode, policy);
            expect (cached.status == actual.status && cached.failure == Unknown::none,
                    "D1b matches every fixed-K game with S2 and V1 off and on");
          }
        }
        expect (choice_regions (store, a, K, symbolic, symbolic_region_inputs) == 0,
                "symbolic search preserves choice-region arithmetic");
        for (const auto& node : symbolic.nodes)
          check_losing_inputs (store, a, K, node.rank, {}, losing_input_checks);
        if (not win) {
          check_losing_certificate (store, a, K, symbolic);
          LossSet earlier;
          for (const auto& proof : symbolic.proofs) {
            check_losing_inputs (store, a, K, proof.rank, earlier.ranks (), losing_input_checks);
            earlier.insert (proof.rank, proof.record.id);
          }
          auto invalid = symbolic;
          invalid.proofs.at (*invalid.initial_proof)
              .record.dependencies.push_back (*invalid.initial_proof);
          expect (not verify_losing_proof (store, a, K, invalid, {}, semantics).value,
                  "symbolic proof with a nonchronological dependency is rejected");
        }
        Reader reader {store, false, {}};
        Oracle sparse {reader, store, a, K};
        for (const auto& node : actual.nodes)
          for (auto v : valuations (a, a.ap_vars)) {
            const auto expected = rows::evaluate_sparse (store.cache, a, node.rank, v, K);
            const auto computed = sparse.evaluate (node.rank, v);
            if (!expected.value || !computed.value || *expected.value != *computed.value)
              return 3;
            ++arithmetic;
          }
        // Losing-region accounting.  These hold in any correct version and are
        // the baseline the growth-gated scan has to improve without breaking:
        // today one broad scan runs per loss event, so scans >= insertions.
        if (actual.subsumption_hits > actual.subsumption_queries)
          return 8;
        if (actual.losing_antichain_size + actual.losing_removals
            != actual.losing_insertions)
          return 9;
        if (actual.losing_antichain_size > actual.losing_antichain_peak)
          return 10;
        if (actual.subsumption_nodes_invalidated > actual.subsumption_nodes_checked)
          return 11;
        if (actual.subsumption_nodes_checked
            > actual.subsumption_scans * actual.nodes.size ())
          return 12;
        // One broad scan per generator, not one per loss event: the scan is
        // what makes the region's growth known, so it runs exactly when the
        // region grows.
        if (actual.subsumption_scans != actual.losing_insertions)
          return 13;
        if (actual.reopen_enqueues > actual.reopened_sources)
          return 15;
        // A loss event whose rank was already inside the losing region: the
        // proof is recorded but no generator is added.  These are exactly the
        // events whose broad scan can find nothing.
        if (actual.proofs.size () > actual.losing_insertions)
          ++redundant_loss_events;
        expansions_total += actual.expansions;
        choices_total += actual.choices_created;
        nodes_total += actual.nodes.size ();
        reopened_total += actual.reopened_sources;
        reopen_enqueues_total += actual.reopen_enqueues;
        invalidated_total += actual.subsumption_nodes_invalidated;
        removals_total += actual.losing_removals;
        subsumption_queries += actual.subsumption_queries;
        subsumption_hits += actual.subsumption_hits;
        prefilter_skips_total += actual.subsumption_prefilter_skips;
        losing_events += actual.proofs.size ();
        antichain_insertions += actual.losing_insertions;
        broad_scans += actual.subsumption_scans;
        nodes_scanned += actual.subsumption_nodes_checked;

        if (const int code = choice_regions (store, a, K, actual, region_inputs))
          return code;

        auto corrupt = actual;
        if (win) {
          corrupt.nodes[corrupt.initial].choices.clear ();
          if (verify_winning_certificate (store, a, K, corrupt, {}, semantics).value)
            return 4;
        }
        else {
          corrupt.proofs.at (*corrupt.initial_proof)
              .record.dependencies.push_back (*corrupt.initial_proof);
          if (verify_losing_proof (store, a, K, corrupt, {}, semantics).value)
            return 5;
        }
        ++checks;
        ++corruptions;
      }
    }
  }
  std::cout << checks << " explicit-game matches, " << arithmetic << " sparse arithmetic matches, "
            << corruptions << " corrupt certificates rejected\n";
  std::cout << checks << " S2 on/off matches, " << losing_input_checks
            << " universally losing inputs checked by enumeration, " << symbolic_region_inputs
            << " S2 choice-region inputs checked\n";
  std::cout << 2 * checks << " S3/legacy rebuild traces match with S2 off/on\n";
  std::cout << 2 * checks << " V1 on/off explicit-game matches and dependency subset checks\n";
  std::cout << losing_events << " loss events, " << antichain_insertions
            << " antichain insertions, " << broad_scans << " broad scans over "
            << nodes_scanned << " node checks, " << redundant_loss_events
            << " games with a redundant loss event\n";
  std::cout << subsumption_queries << " subsumption queries, " << subsumption_hits
            << " hits, " << prefilter_skips_total << " prefilter skips\n";
  // The search itself must be untouched by how the losing region is scanned.
  std::cout << "search shape: " << expansions_total << " expansions, " << choices_total
            << " choices, " << nodes_total << " nodes, " << reopened_total
            << " reopened, " << invalidated_total << " invalidated, " << removals_total
            << " removals\n";
  std::cout << "reopen enqueues: " << reopen_enqueues_total << ", "
            << region_inputs << " choice-region inputs checked against the\n"
               "                 independent sparse arithmetic\n";
  if (region_inputs == 0) {
    std::cerr << "FAIL: no choice region was checked\n";
    return 16;
  }
  if (redundant_loss_events == 0) {
    // Without one of these the growth gate would be untested by this sweep.
    std::cerr << "FAIL: no game produced a loss event inside the known region\n";
    return 14;
  }
  {
    // A forged strategy omits the accepting successor row. The verifier must
    // request it, charge it as additional active work, and reject the strategy.
    const auto dict = spot::make_bdd_dict ();
    auto p = spot::make_twa_graph (dict);
    p->new_states (2);
    p->set_init_state (0);
    p->set_buchi ();
    p->new_edge (0, 1, bddtrue);
    p->new_edge (1, 1, bddtrue, {0});
    auto w = std::make_shared<lazy::LazyBuchiView> (p);
    RowStore store {w, {}, report};
    store.phase = Phase::search;
    store.get (0);
    SolveResult forged;
    forged.provider = w;
    forged.semantics = semantics;
    forged.nodes.push_back ({Rank {{{0, 0}}, 1}, true});
    forged.nodes.push_back ({Rank {{{1, 0}}, 1}, true});
    for (auto& node : forged.nodes) {
      node.covered_inputs = bddtrue;
      node.choices.push_back (semantics.output_choice == OutputChoice::constant
                                 ? SparseChoice::constant (bddtrue, bddtrue, 1)
                                 : SparseChoice::existential (bddtrue, 1));
    }
    store.phase = Phase::verify;
    const letters::WorkerAlphabet a {bddtrue, bddtrue, bddtrue, {}};
    if (verify_winning_certificate (store, a, 1, forged, {}, semantics).value)
      return 6;
    if (store.search_sources != std::set<StateId> {0} ||
        store.verifier_sources != std::set<StateId> {0, 1} || store.verify_generated != 1)
      return 7;
  }
  return 0;
}

// These kernels use transition-Buchi rows and Search directly, with the same
// arithmetic oracle as the differential sweep. No frontend fast path is involved.
struct Kernel {
    spot::twa_graph_ptr graph = spot::make_twa_graph (spot::make_bdd_dict ());
    letters::WorkerAlphabet alphabet {bddtrue, bddtrue, bddtrue, {}};
    explicit Kernel (unsigned states) {
      graph->set_buchi ();
      graph->prop_state_acc (false);
      graph->new_states (states);
      graph->set_init_state (0);
    }
    bdd ap (const std::string& name, bool input) {
      const int var = graph->register_ap (name);
      const bdd v = bdd_ithvar (var);
      alphabet.order.push_back (var);
      alphabet.ap_vars &= v;
      (input ? alphabet.inputs : alphabet.outputs) &= v;
      return v;
    }
    auto view () const { return std::make_shared<lazy::LazyBuchiView> (graph); }
};

void lean_verifier_dependencies () {
  // Both output branches lose after two steps. By the time the second branch
  // loses, the antichain also contains irrelevant proofs from the first one.
  // The root still needs both branches: removing either leaves a legal output.
  Kernel k {5};
  k.ap ("u", true);
  const bdd c = k.ap ("c", false);
  k.graph->new_edge (0, 1, !c);
  k.graph->new_edge (0, 2, c);
  k.graph->new_edge (1, 3, bddtrue);
  k.graph->new_edge (2, 4, bddtrue);
  k.graph->new_edge (3, 3, bddtrue, {0});
  k.graph->new_edge (4, 4, bddtrue, {0});
  Fields observed;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
  }}};
  store.enumerate_and_freeze ();
  for (auto semantics : all_semantics)
    for (auto mode : {LosingInputSearch::off, LosingInputSearch::on}) {
      const auto full = Search {store, k.alphabet, 1, {}, semantics, mode, LeanVerifier::off}.solve ();
      check_solve_counters (full, observed);
      const auto lean = Search {store, k.alphabet, 1, {}, semantics, mode, LeanVerifier::on}.solve ();
      check_solve_counters (lean, observed);
      expect (lean.status == forward_result_status::lose_k, "two delayed output branches lose");
      check_lean_dependencies (full, lean);
      check_losing_certificate (store, k.alphabet, 1, full);
      check_losing_certificate (store, k.alphabet, 1, lean);
      expect (lean.dependency_list_len_sum < full.dependency_list_len_sum,
              "lean search removes irrelevant antichain dependencies");
      expect (lean.proofs.at (*lean.initial_proof).record.dependencies.size () == 2,
              "root retains both required output branches");
      for (auto policy : {LeanVerifier::off, LeanVerifier::on}) {
        for (std::size_t id = 0; id < lean.proofs.size (); ++id) {
          const auto& proof = lean.proofs[id];
          if (proof.record.reason != losing_reason::env_losing_input)
            continue;
          for (std::size_t i = 0; i < proof.record.dependencies.size (); ++i) {
            auto corrupt = lean;
            auto& deps = corrupt.proofs[id].record.dependencies;
            deps.erase (deps.begin () + static_cast<std::ptrdiff_t> (i));
            const auto rejected = verify_losing_proof (store, k.alphabet, 1, corrupt, {},
                                                       semantics, policy);
            expect (not rejected.value && rejected.unknown == Unknown::invalid_query,
                    "shrinking a minimal dependency list below sufficiency is rejected");
          }
        }
        auto corrupt = lean;
        corrupt.proofs.at (*corrupt.initial_proof).record.dependencies.clear ();
        const auto rejected = verify_losing_proof (store, k.alphabet, 1, corrupt, {}, semantics, policy);
        expect (not rejected.value && rejected.unknown == Unknown::invalid_query,
                "missing all-output dependency rejected");
      }

      // Replay the SAME certificate to isolate eviction from dependency pruning.
      detail::take (verify_losing_proof (store, k.alphabet, 1, full, {}, semantics, LeanVerifier::off));
      const auto cached = observed;
      detail::take (verify_losing_proof (store, k.alphabet, 1, full, {}, semantics, LeanVerifier::on));
      expect (cached.at ("verify_preimage_hits") == "0" &&
              observed.at ("verify_preimage_hits") == "0" &&
              std::stoull (observed.at ("verify_cache_rank_bytes")) <
                  std::stoull (cached.at ("verify_cache_rank_bytes")),
              "losing replay releases preimages that have no reuse");
      for (const auto* key : {"verify_queries", "verify_steps", "verify_bdd_operations",
                             "verify_threshold_hits"})
        expect (observed.at (key) == cached.at (key), "eviction preserves losing replay work");
      std::cout << "two delayed output branches, "
                << (semantics.successor_relation == SuccessorRelation::exact ? "exact" : "downward")
                << '+' << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
                << ", S2 " << (mode == LosingInputSearch::on ? "on" : "off")
                << ": dependency_list_len_sum V1 off/on " << full.dependency_list_len_sum
                << '/' << lean.dependency_list_len_sum << ", verify_cache_rank_bytes "
                << cached.at ("verify_cache_rank_bytes") << '/'
                << observed.at ("verify_cache_rank_bytes") << '\n';
    }
}

void lean_verifier_winning_cache () {
  // The initial rank is strictly below the next rank, which is the only
  // maximal generator. Its downward preimage is reused by the invariant.
  Kernel k {2};
  k.graph->new_edge (0, 0, bddtrue);
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (1, 1, bddtrue);
  Fields observed;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
  }}};
  store.enumerate_and_freeze ();
  const ChoiceSemantics semantics {SuccessorRelation::downward, OutputChoice::constant};
  const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
  expect (result.status == forward_result_status::win_k && result.generators.size () == 1 &&
          result.nodes.size () > 1, "winning cache fixture has a dominated source outside G");
  detail::take (verify_winning_certificate (store, k.alphabet, 1, result, {}, semantics,
                                          LeanVerifier::off));
  const auto cached = observed;
  detail::take (verify_winning_certificate (store, k.alphabet, 1, result, {}, semantics,
                                          LeanVerifier::on));
  expect (std::stoull (observed.at ("verify_preimage_hits")) > 0 &&
          observed.at ("verify_preimage_hits") == cached.at ("verify_preimage_hits") &&
          std::stoull (observed.at ("verify_cache_rank_bytes")) <
              std::stoull (cached.at ("verify_cache_rank_bytes")),
          "winning replay evicts nonmaximal preimages and retains invariant reuse at G");
  expect (observed.at ("verify_invariant_bdd_operations") ==
              cached.at ("verify_invariant_bdd_operations"), "invariant keeps its warm-cache work");

  Reader reader {store, true, {}};
  Oracle oracle {reader, store, k.alphabet, 1};
  const auto& rank = result.generators.front ();
  const auto expected = detail::take (oracle.down (rank, rank));
  oracle.clear_preimages (rank);
  oracle.report (store.report, "before_");
  expect (detail::take (oracle.down (rank, rank)) == expected,
          "evicting a preimage preserves its reconstructed predicate");
  oracle.report (store.report, "after_");
  expect (observed.at ("after_preimage_hits") == observed.at ("before_preimage_hits") &&
          std::stoull (observed.at ("after_threshold_hits")) >
              std::stoull (observed.at ("before_threshold_hits")),
          "preimage eviction retains the thresholds memo");
}

void lean_verifier_retained_thresholds () {
  // As in the winning-cache fixture, the initial source is strictly below G.
  Kernel k {2};
  k.graph->new_edge (0, 0, bddtrue);
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (1, 1, bddtrue);
  Fields observed;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
  }}};
  store.enumerate_and_freeze ();
  const ChoiceSemantics semantics {SuccessorRelation::downward, OutputChoice::constant};
  const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
  expect (result.status == forward_result_status::win_k && result.generators.size () == 1,
          "threshold retention fixture has a single winning generator");
  const auto& evicted = result.nodes.at (result.initial).rank;
  const auto& retained = result.generators.front ();
  expect (evicted != retained && evicted.leq (retained),
          "threshold retention fixture excludes the dominated initial source from G");

  Reader reader {store, true, {}};
  Oracle oracle {reader, store, k.alphabet, 1};
  // Each downward query warms a preimage and the threshold at level 1 for
  // each destination, for both the excluded and retained source.
  const auto expected_evicted = detail::take (oracle.down (evicted, retained));
  const auto expected_retained = detail::take (oracle.down (retained, retained));
  oracle.retain_preimages (result.generators);
  oracle.report (store.report, "before_");

  // Predicate equality guards reconstruction; unchanged preimage hits prove
  // eviction really happened. Both would still pass if retain_preimages also
  // cleared thresholds for evicted sources. The threshold-hit increase fails
  // under that mutation: this query visits each threshold once, so a cleared
  // memo produces only misses. Snapshot before querying the retained source
  // so its hits cannot conceal the loss of the evicted source's thresholds.
  expect (detail::take (oracle.down (evicted, retained)) == expected_evicted,
          "retain_preimages preserves the evicted source's reconstructed predicate");
  oracle.report (store.report, "rebuilt_");
  expect (observed.at ("rebuilt_preimage_hits") == observed.at ("before_preimage_hits"),
          "retain_preimages evicts the excluded source's preimage");
  expect (std::stoull (observed.at ("rebuilt_threshold_hits")) >
              std::stoull (observed.at ("before_threshold_hits")),
          "retain_preimages preserves the evicted source's thresholds memo");

  // The retained predicate and its cache hit check selective eviction. They
  // also pass under the thresholds mutation, which affects only sources outside G.
  expect (detail::take (oracle.down (retained, retained)) == expected_retained,
          "retain_preimages preserves the retained source's predicate");
  oracle.report (store.report, "retained_");
  expect (std::stoull (observed.at ("retained_preimage_hits")) ==
              std::stoull (observed.at ("rebuilt_preimage_hits")) + 1,
          "retain_preimages keeps the retained source's preimage warm");
}

void verifier_phase_metrics () {
  Kernel k {1};
  k.graph->new_edge (0, 0, bddtrue);
  Fields observed;
  bool abort_invariant = false, traversal_finished = false;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
    if (abort_invariant && key == "verify_traversal_queries" && value != "0")
      traversal_finished = true;
  }}};
  store.enumerate_and_freeze ();
  for (auto semantics : all_semantics) {
    const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    check_solve_counters (result, observed);
    const auto successful = observed;
    // Warm only the rank predicates that traversal uses, then independently
    // measure the invariant via the original cumulative Oracle::report API.
    Reader reader {store, true, {}};
    Oracle oracle {reader, store, k.alphabet, 1};
    const auto& rank = result.nodes.at (result.initial).rank;
    detail::take (semantics.successor_relation == SuccessorRelation::exact
                      ? oracle.eq (rank, rank) : oracle.down (rank, rank));
    Fields before, after;
    oracle.report (Reporter {[&] (const auto& key, const auto& value) { before[key] = value; }}, "");
    expect (detail::take (oracle.invariant (rank, result.generators, 0)) == letters::Invariant::verified,
            "independent invariant succeeds with the traversal's predicate cache");
    oracle.report (Reporter {[&] (const auto& key, const auto& value) { after[key] = value; }}, "");
    for (const auto* key : {"queries", "steps", "bdd_operations"})
      expect (std::stoull (successful.at (std::string ("verify_invariant_") + key)) ==
                  std::stoull (after.at (key)) - std::stoull (before.at (key)),
              "invariant bucket equals independently measured warm-cache " + std::string (key));
    expect (std::stoull (successful.at ("verify_invariant_bdd_operations")) > 0 &&
            std::stoull (successful.at ("verify_traversal_bdd_operations")) > 0,
            "both winning phases do BDD work");

    Limits limits;
    limits.verifier_queries.max_steps = 0;
    const auto failed = verify_winning_certificate (store, k.alphabet, 1, result, limits, semantics);
    expect (not failed.value && failed.unknown == Unknown::resource_limit &&
            observed.at ("verify_traversal_queries") == "1" &&
            observed.at ("verify_invariant_queries") == "0", "early failure charges traversal only");
    check_verifier_split (observed);

    limits = {};
    limits.verifier_queries.aborted = [] (void* data) { return *static_cast<bool*> (data); };
    limits.verifier_queries.abort_data = &traversal_finished;
    abort_invariant = true;
    const auto aborted = verify_winning_certificate (store, k.alphabet, 1, result, limits, semantics);
    abort_invariant = traversal_finished = false;
    expect (not aborted.value && aborted.unknown == Unknown::aborted &&
            observed.at ("verify_invariant_queries") == "1" &&
            observed.at ("verify_invariant_steps") == "0" &&
            observed.at ("verify_invariant_bdd_operations") == "0",
            "failure entering the invariant charges its query without repeating traversal work");
    for (const auto* key : {"queries", "steps", "bdd_operations"}) {
      const auto field = std::string ("verify_traversal_") + key;
      expect (observed.at (field) == successful.at (field), "invariant failure preserves traversal totals");
    }
    check_verifier_split (observed);
  }

  Kernel loss {2};
  loss.graph->new_edge (0, 1, bddtrue);
  loss.graph->new_edge (1, 1, bddtrue, {0});
  RowStore losing_store {loss.view (), {}, store.report};
  losing_store.enumerate_and_freeze ();
  const auto result = Search {losing_store, loss.alphabet, 1}.solve ();
  expect (result.status == forward_result_status::lose_k, "proof-loop fixture loses");
  check_solve_counters (result, observed);
  const auto successful = observed;
  auto corrupt = result;
  // An orphan proof must still be checked, even though it cannot be in the
  // initial proof's transitive cone. Fail on its missing input after all the
  // original Bad obligations have completed.
  auto orphan = corrupt.proofs.at (*corrupt.initial_proof);
  orphan.record.id = corrupt.proofs.size ();
  orphan.input.reset ();
  corrupt.proofs.push_back (std::move (orphan));
  const auto failed = verify_losing_proof (losing_store, loss.alphabet, 1, corrupt);
  expect (not failed.value && failed.unknown == Unknown::invalid_query &&
          std::stoull (observed.at ("verify_proof_bad_queries")) ==
              std::stoull (successful.at ("verify_proof_bad_queries")) + 1,
          "losing replay still checks an orphan after every original proof");
  check_verifier_split (observed);
}

void certificate_shape_metrics () {
  Kernel k {1};
  Fields observed;
  RowStore store {k.view (), {}, Reporter {[&] (const auto& key, const auto& value) {
    observed[key] = value;
  }}};
  const auto scan = SearchTestAccess::scan_buckets (store, k.alphabet);
  expect (scan.subsumption_nodes_checked == 6 && scan.scan_tombstones == 2 &&
          scan.scan_prefilter_rejects == 2 && scan.scan_exact_compares == 2 &&
          scan.subsumption_nodes_invalidated == 1,
          "scan classifies tombstones, both prefilters, equal-mass rejection and merge-join success");
  expect (observed.at ("scan_tombstones") == "2" && observed.at ("scan_prefilter_rejects") == "2" &&
          observed.at ("scan_exact_compares") == "2", "scan reporting survives moving the result");
  for (bool has_root : {false, true}) {
    SolveResult published;
    {
      Search search {store, k.alphabet, 1};
      auto& result = SearchTestAccess::result (search);
      // Diamond sharing proof 0, plus orphan proof 1. Closure counts shared
      // dependencies once, while list lengths count every recorded edge.
      const std::vector<std::vector<std::size_t>> deps {{}, {}, {0}, {0}, {2, 3}};
      for (std::size_t id = 0; id < deps.size (); ++id)
        result.proofs.push_back ({{id, losing_reason::env_subsumed, 0, 0, deps[id]},
                                 Rank {{{0, 0}}, 1}, {}, {}});
      if (has_root) result.initial_proof = 4;
      search.publish_counters ();
      published = std::move (result);
    }
    expect (published.proofs_total == 5 && published.proofs_in_initial_cone == (has_root ? 4 : 0) &&
            published.dependency_list_len_sum == 4 && published.dependency_list_len_max == 2,
            "diamond closure excludes orphans, deduplicates shared dependencies and handles no root");
    expect (observed.at ("proofs_total") == "5" &&
            observed.at ("proofs_in_initial_cone") == (has_root ? "4" : "0") &&
            observed.at ("dependency_list_len_sum") == "4" &&
            observed.at ("dependency_list_len_max") == "2", "shape reporting survives moving the result");
  }
}

void incremental_bad_unions () {
  Kernel k {3};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  k.graph->new_edge (0, 1, u);
  k.graph->new_edge (0, 2, !u);
  k.graph->new_edge (1, 1, c, {0});
  k.graph->new_edge (1, 2, !c);
  k.graph->new_edge (2, 2, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  const std::vector<Rank> sources {Rank {{{0, 0}}, 2}, Rank {{{0, 1}}, 2},
                                  Rank {{{1, 1}}, 2}, Rank {{{1, 0}, {2, 0}}, 2},
                                  Rank {{}, 2}};
  size_t removals = 0;
  for (unsigned seed = 0; seed < 8; ++seed) {
    Search s {store, k.alphabet, 2, {}, default_choice_semantics,
              LosingInputSearch::off, LeanVerifier::off, IncrementalBad::on};
    for (const auto& source : sources)
      SearchTestAccess::check_bad (s, source);
    // Inject loss events to test the set algebra independently of whether
    // these ranks are losing in this game. Dominated entries are kept only in
    // the journal; duplicate and already-subsumed events must not be appended.
    std::vector<Rank> targets {Rank {{{1, 1}}, 2}, Rank {{{2, 1}}, 2},
                              Rank {{{1, 0}}, 2}, Rank {{{2, 0}}, 2}};
    if (seed != 0) {
      std::mt19937 random {seed};
      std::shuffle (targets.begin (), targets.end (), random);
    }
    targets.push_back (targets.front ());
    targets.push_back (Rank {{{1, 1}, {2, 1}}, 2});
    targets.push_back (Rank {{}, 2}); // removes every remaining generator
    for (const auto& target : targets) {
      SearchTestAccess::insert_loss (s, target);
      for (const auto& source : sources)
        SearchTestAccess::check_bad (s, source);
    }
    removals += SearchTestAccess::losing (s).removals;
    s.publish_counters ();
    expect (SearchTestAccess::result (s).incremental_bad_journal_replay ==
                sources.size () * SearchTestAccess::journal (s).size (),
            "each source replays every inserted generator exactly once across removals");
    expect (SearchTestAccess::journal (s).size () > SearchTestAccess::losing (s).size (),
            "removed generators survive in the journal");
  }
  expect (removals > 8, "small histories exercise antichain removals");

  // A source can miss several insertions; both overloads then use the same
  // completed prefix, while the output projection is always recomputed.
  Search s {store, k.alphabet, 2, {}, default_choice_semantics,
            LosingInputSearch::off, LeanVerifier::off, IncrementalBad::on};
  SearchTestAccess::check_bad (s, sources[0]);
  SearchTestAccess::insert_loss (s, Rank {{{1, 1}}, 2});
  SearchTestAccess::insert_loss (s, Rank {{{1, 0}}, 2});
  SearchTestAccess::check_bad (s, sources[0]);
  auto& oracle = SearchTestAccess::oracle (s);
  const auto before = oracle.work_metrics ();
  SearchTestAccess::search_bad (s, sources[0]);
  expect (oracle.work_metrics ().bdd_operations == before.bdd_operations,
          "unchanged cached Bad performs no BDD operations");
  for (auto missing : {bdd (bddtrue), u, !u}) {
    const auto expected = detail::take (oracle.bad (
        sources[0], SearchTestAccess::losing (s).ranks (), 0, missing));
    expect (detail::take (SearchTestAccess::search_bad (s, sources[0], missing)) == expected,
            "losing-input query recomputes forall and intersects the current missing region");
  }
  // Ordinary Bad is never served from the journal, even on the same Oracle
  // with a warm cache and the same nominal epoch.
  expect (detail::take (oracle.bad (sources[0], {}, 0)) == bddfalse &&
          detail::take (SearchTestAccess::search_bad (s, sources[0])) == u,
          "explicit dependency subsets remain independent of the search journal");
}

void incremental_bad_budget () {
  Kernel k {2};
  const bdd u = k.ap ("u", true);
  k.graph->new_edge (0, 1, u);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  const Rank source {{{0, 0}}, 1}, target {{{1, 0}}, 1};
  for (bool warm : {false, true}) {
    Search s {store, k.alphabet, 1, {}, default_choice_semantics,
              LosingInputSearch::off, LeanVerifier::off, IncrementalBad::on};
    size_t budget = 0;
    if (warm) {
      SearchTestAccess::check_bad (s, source);
      s.publish_counters ();
      budget = SearchTestAccess::result (s).incremental_bad_cache_bytes;
    }
    SearchTestAccess::bad_budget (s, budget);
    SearchTestAccess::insert_loss (s, target);
    for (unsigned i = 0; i < 2; ++i)
      SearchTestAccess::check_bad (s, source);
    s.publish_counters ();
    const auto& result = SearchTestAccess::result (s);
    expect (result.incremental_bad_full_rebuilds == 2 &&
            result.incremental_bad_cache_bytes == 0 && SearchTestAccess::bad_cache (s).empty (),
            "admission and growth overflow both release the cache and use full rebuilds");
    expect (result.incremental_bad_journal_replay == (warm ? 1 : 0),
            "budget exhaustion does not repeatedly replay a journal that cannot fit");
  }
}

void incremental_bad_failure () {
  Kernel k {3};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  k.graph->new_edge (0, 1, u | c);
  k.graph->new_edge (0, 2, !u);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  const Rank source {{{0, 0}}, 2};
  size_t checkpoints = 0;
  // First count all checkpoints of a successful warm-cache extension. Then
  // fail at each one, including during replay, forall and the final checkpoint.
  for (size_t fail_at = 0; fail_at <= checkpoints; ++fail_at) {
    Search s {store, k.alphabet, 2, {}, default_choice_semantics,
              LosingInputSearch::off, LeanVerifier::off, IncrementalBad::on};
    SearchTestAccess::insert_loss (s, Rank {{{1, 1}}, 2});
    SearchTestAccess::check_bad (s, source);
    SearchTestAccess::insert_loss (s, Rank {{{1, 0}}, 2});
    SearchTestAccess::insert_loss (s, Rank {{{2, 0}}, 2});
    struct Budget { size_t calls = 0, fail_at; } budget {0, fail_at};
    letters::QueryLimits limits;
    limits.aborted = [] (void* data) {
      auto& budget = *static_cast<Budget*> (data);
      if (++budget.calls == budget.fail_at)
        throw std::bad_alloc ();
      return false;
    };
    limits.abort_data = &budget;
    auto& oracle = SearchTestAccess::oracle (s);
    oracle.set_limits (limits);
    const auto query = SearchTestAccess::search_bad (s, source, bddtrue);
    oracle.set_limits ({});
    if (fail_at == 0) {
      expect (query.value.has_value (), "reference cache extension completes");
      checkpoints = budget.calls;
      continue;
    }
    expect (not query.value && query.unknown == Unknown::resource_limit &&
            SearchTestAccess::bad_cache (s).empty () && SearchTestAccess::prepared_empty (s),
            "every failed query checkpoint clears Prepared and incremental Bad together");
    expect (SearchTestAccess::journal (s).size () == 3,
            "failed queries preserve the complete losing insertion journal");
    s.publish_counters ();
    const auto before = SearchTestAccess::result (s);
    expect (before.incremental_bad_cache_bytes == 0, "failure resets retained cache byte accounting");
    SearchTestAccess::check_bad (s, source);
    s.publish_counters ();
    const auto& after = SearchTestAccess::result (s);
    expect (after.incremental_bad_cache_hits == before.incremental_bad_cache_hits &&
            after.incremental_bad_journal_replay == before.incremental_bad_journal_replay + 3,
            "retry replays the entire journal without reusing an interrupted cache");
    // An unrelated failed query has the same invalidation contract.
    limits = {};
    limits.max_steps = 0;
    oracle.set_limits (limits);
    expect (not oracle.eq (source, source).value && SearchTestAccess::bad_cache (s).empty () &&
            SearchTestAccess::prepared_empty (s), "failed non-Bad query also clears both caches");
  }
  expect (checkpoints > 10, "failure sweep reaches replay and output quantification checkpoints");
}

void incremental_bad_publication_failure () {
  Kernel k {3};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  k.graph->new_edge (0, 1, u | c);
  k.graph->new_edge (0, 2, !u);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  const Rank source {{{0, 0}}, 2};
  Search s {store, k.alphabet, 2, {}, default_choice_semantics,
            LosingInputSearch::off, LeanVerifier::off, IncrementalBad::on};
  SearchTestAccess::insert_loss (s, Rank {{{1, 1}}, 2});
  SearchTestAccess::check_bad (s, source);
  const auto before_index = SearchTestAccess::bad_cache (s).at (source).journal_index;
  expect (before_index == 1, "publication fixture starts with a completed nonempty prefix");
  SearchTestAccess::insert_loss (s, Rank {{{1, 0}}, 2});
  SearchTestAccess::insert_loss (s, Rank {{{2, 0}}, 2});
  const auto journal_size = SearchTestAccess::journal (s).size ();
  expect (journal_size == before_index + 2, "publication fixture has two pending journal entries");

  struct Failure {
      Search& search;
      const Rank& source;
      size_t fail_after_replay;
      std::optional<size_t> published_index;
  } failure {s, source, SearchTestAccess::bad_journal_replay (s) + 2, {}};
  letters::QueryLimits limits;
  limits.aborted = [] (void* data) {
    auto& failure = *static_cast<Failure*> (data);
    if (SearchTestAccess::bad_journal_replay (failure.search) == failure.fail_after_replay) {
      // The first new OR has completed; the second entry has just started.
      // Pins publishing journal_index in the cache before the union completes,
      // even if the loop retains its original start and failure cleanup works.
      // Observe before query cleanup erases the interrupted cache entry.
      failure.published_index = SearchTestAccess::bad_cache (failure.search)
                                    .at (failure.source).journal_index;
      throw std::bad_alloc ();
    }
    return false;
  };
  limits.abort_data = &failure;
  auto& oracle = SearchTestAccess::oracle (s);
  oracle.set_limits (limits);
  const auto query = SearchTestAccess::search_bad (s, source);
  oracle.set_limits ({});
  expect (not query.value && query.unknown == Unknown::resource_limit &&
          failure.published_index.has_value (), "failure is injected part-way through journal replay");
  expect (*failure.published_index == before_index,
          "incomplete union leaves the published journal index at the completed prefix");
  expect (SearchTestAccess::bad_cache (s).empty () && SearchTestAccess::prepared_empty (s) &&
          SearchTestAccess::journal (s).size () == journal_size,
          "failure clears both caches and preserves the journal");

  const auto before_retry = SearchTestAccess::bad_journal_replay (s);
  SearchTestAccess::check_bad (s, source);
  expect (SearchTestAccess::bad_journal_replay (s) == before_retry + journal_size,
          "retry replays the full journal, including every entry past the unpublished index");
  const auto& completed = SearchTestAccess::bad_cache (s).at (source);
  expect (completed.journal_index == journal_size && completed.predicate == bddtrue,
          "retry publishes the complete union over the current generators");
}

void incremental_bad_verifier_isolation () {
  Kernel k {3};
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (1, 2, bddtrue);
  k.graph->new_edge (2, 2, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  for (auto policy : {LeanVerifier::off, LeanVerifier::on}) {
    Search s {store, k.alphabet, 1, {}, default_choice_semantics,
              LosingInputSearch::on, policy, IncrementalBad::on};
    auto certificate = s.solve ();
    expect (certificate.status == forward_result_status::lose_k, "delayed-loss fixture verifies");
    const auto root = certificate.proofs.at (*certificate.initial_proof);
    auto& oracle = SearchTestAccess::oracle (s);
    expect (detail::take (oracle.bad (root.rank, {}, 0)) == bddfalse &&
            detail::take (SearchTestAccess::search_bad (s, root.rank)) == bddtrue,
            "root Bad needs a frontier generator beyond the empty dependency subset");
    // Move the root obligation before the frontier entry it actually needs,
    // without declaring any forward dependency. A whole-frontier cache would
    // wrongly validate it; chronological subset replay must reject it.
    certificate.proofs.insert (certificate.proofs.begin (), root);
    for (size_t id = 0; id < certificate.proofs.size (); ++id) {
      auto& record = certificate.proofs[id].record;
      record.id = id;
      if (id == 0)
        record.dependencies.clear ();
      else
        for (auto& dep : record.dependencies) ++dep;
    }
    certificate.initial_proof = 0;
    const auto rejected = verify_losing_proof (store, k.alphabet, 1, certificate, {},
                                               default_choice_semantics, policy);
    expect (not rejected.value && rejected.unknown == Unknown::invalid_query,
            "verifier rejects a proof valid only using a later frontier entry");
    certificate.proofs[0].record.dependencies = {root.record.dependencies.front () + 1};
    expect (not verify_losing_proof (store, k.alphabet, 1, certificate, {},
                                    default_choice_semantics, policy).value,
            "declaring the later dependency is also rejected by chronology");
  }
}

void incremental_coverage_events () {
  Kernel k {1};
  const bdd u = k.ap ("u", true), v = k.ap ("v", true);
  RowStore store {k.view (), {}, {}};
  Search s {store, k.alphabet, 1};
  auto& result = SearchTestAccess::result (s);
  result.nodes.resize (5, GuardedRankNode {Rank {{}, 1}});
  auto& oracle = SearchTestAccess::oracle (s);
  const auto regions = acacia::spot_lazy_game::detail::take (
      oracle.query<std::vector<bdd>> ([&] (auto& b) {
        return std::vector<bdd> {b.land (u, v), b.land (u, b.negate (v)), b.negate (u)};
      }));
  const auto first = SearchTestAccess::activate (s, 0, regions[0], 1);
  SearchTestAccess::activate (s, 0, regions[1], 1);
  SearchTestAccess::activate (s, 0, regions[2], 2);
  result.nodes[1].incoming.push_back ({0, first}); // Duplicate reference to an active choice.
  SearchTestAccess::loss (s, 1);
  SearchTestAccess::loss (s, 1); // Repeated processing of the same target loss.
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  expect (result.nodes[0].covered_inputs == regions[2] && result.reopened_sources == 1,
          "one target invalidates both choices once despite duplicate events/references");
  const auto replacement = SearchTestAccess::activate (s, 0, u, 3);
  result.nodes[1].incoming.push_back ({0, replacement}); // Active, but wrong target identity.
  SearchTestAccess::loss (s, 1);
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  expect (result.nodes[0].covered_inputs == bddtrue && result.reopened_sources == 1,
          "stale invalidations cannot subtract historical regions overlapping a replacement");

  // Two distinct pending losses affect one source. The old reconstruction
  // hid the second reopen attempt by clearing it while processing the first.
  SearchTestAccess::loss (s, 2);
  SearchTestAccess::loss (s, 3);
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  s.publish_counters ();
  expect (result.nodes[0].covered_inputs == bddfalse && result.reopened_sources == 3 &&
          result.reopen_enqueues == 1,
          "distinct losses count both invalidations but enqueue the affected source only once");
  SearchTestAccess::activate (s, 4, bddtrue, 4);
  SearchTestAccess::loss (s, 4);
  SearchTestAccess::loss (s, 4);
  SearchTestAccess::drain (s);
  SearchTestAccess::check_coverage (s);
  expect (not result.nodes[4].choices[0].active && result.nodes[4].covered_inputs == bddfalse &&
          result.reopened_sources == 3, "losing self-loop subtracts once without reopening itself");
}

void incremental_coverage_property () {
  Kernel k {1};
  for (unsigned i = 0; i < 4; ++i)
    k.ap ("u" + std::to_string (i), true);
  RowStore store {k.view (), {}, {}};
  std::size_t activations = 0, invalidations = 0, overlaps = 0;
  for (unsigned seed = 0; seed < 16; ++seed) {
    std::mt19937 random {seed};
    Search s {store, k.alphabet, 1};
    auto& result = SearchTestAccess::result (s);
    result.nodes.push_back (GuardedRankNode {Rank {{}, 1}});
    auto& oracle = SearchTestAccess::oracle (s);
    const auto cubes = acacia::spot_lazy_game::detail::take (
        oracle.query<std::vector<bdd>> ([&] (auto& b) {
          return b.call ([&] { return valuations (k.alphabet, k.alphabet.inputs); });
        }));
    for (unsigned step = 0; step < 256; ++step) {
      if (result.nodes.size () == 1 || (random () % 2 && result.nodes[0].covered_inputs != bddtrue)) {
        const auto region = acacia::spot_lazy_game::detail::take (oracle.query<bdd> ([&] (auto& b) {
          bdd candidate = bddfalse;
          for (auto cube : cubes)
            if (random () % 2)
              candidate = b.lor (candidate, cube);
          const auto missing = b.negate (result.nodes[0].covered_inputs);
          auto region = b.land (candidate, missing);
          if (region == bddfalse)
            region = missing;
          for (const auto& old : result.nodes[0].choices)
            if (not old.active && b.land (region, old.input_region) != bddfalse) {
              ++overlaps;
              break;
            }
          return region;
        }));
        // Reuse live targets as well as creating fresh ones, so one loss can
        // invalidate many disjoint regions without recycling historical choices.
        RankNodeId target = random () % result.nodes.size ();
        if (target == 0 || result.nodes[target].losing) {
          target = result.nodes.size ();
          result.nodes.push_back (GuardedRankNode {Rank {{}, 1}});
        }
        SearchTestAccess::activate (s, 0, region, target);
        ++activations;
      }
      else {
        const auto target = 1 + random () % (result.nodes.size () - 1);
        if (not result.nodes[target].incoming.empty ())
          result.nodes[target].incoming.push_back (result.nodes[target].incoming.front ());
        SearchTestAccess::loss (s, target);
        SearchTestAccess::drain (s);
        ++invalidations;
      }
      SearchTestAccess::check_coverage (s);
    }
  }
  expect (activations > 100 && invalidations > 100 && overlaps > 100,
          "random coverage histories exercise activation, loss and overlapping historical regions");
  std::cout << activations + invalidations << " randomized coverage operations checked, "
            << overlaps << " activations overlap inactive history\n";
}

void incremental_coverage_failure () {
  Kernel k {1};
  const bdd u = k.ap ("u", true);
  k.graph->new_edge (0, 0, bddtrue);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  // Fail after each BDD operation, and at the final query checkpoint. Repeat
  // on the second choice so the loss has already been partially propagated.
  for (std::size_t checkpoint : {3, 5, 6, 9, 11, 12}) {
    Search s {store, k.alphabet, 1};
    SearchTestAccess::initialize (s);
    auto& result = SearchTestAccess::result (s);
    result.nodes.resize (2, GuardedRankNode {Rank {{}, 1}});
    auto& oracle = SearchTestAccess::oracle (s);
    const auto not_u = acacia::spot_lazy_game::detail::take (
        oracle.query<bdd> ([&] (auto& b) { return b.negate (u); }));
    SearchTestAccess::activate (s, 0, u, 1);
    SearchTestAccess::activate (s, 0, not_u, 1);
    SearchTestAccess::loss (s, 1);
    struct Budget { std::size_t calls = 0, fail_at; } budget {0, checkpoint};
    letters::QueryLimits limits;
    limits.aborted = [] (void* data) {
      auto& budget = *static_cast<Budget*> (data);
      if (++budget.calls == budget.fail_at)
        throw std::bad_alloc (); // Existing checked boundary classifies resource failures.
      return false;
    };
    limits.abort_data = &budget;
    oracle.set_limits (limits);
    const auto failed = s.solve ();
    expect (budget.calls == checkpoint && failed.status == forward_result_status::resource_limit &&
            failed.failure == Unknown::resource_limit && failed.pending_loss &&
            failed.generators.empty () && store.phase == Phase::search,
            "resource failure during coverage subtraction leaves an inconclusive pending loss");
    expect (failed.nodes[0].choices[0].active == (checkpoint <= 6) &&
            failed.nodes[0].choices[1].active &&
            failed.nodes[0].covered_inputs == (checkpoint <= 6 ? bddtrue : not_u),
            "failed update publishes neither its union nor its inactive flag");
    expect (not verify_winning_certificate (store, k.alphabet, 1, failed).value &&
            not verify_losing_proof (store, k.alphabet, 1, failed).value,
            "neither certificate verifier can accept a failed coverage update");
  }
}

void check_regions (RowStore& store, const letters::WorkerAlphabet& a, const SolveResult& result) {
  std::size_t count = 0;
  expect (choice_regions (store, a, 1, result, count) == 0 && count != 0,
          "kernel choice-region arithmetic");
}

void copy_kernels () {
  for (unsigned n : {1, 2, 4}) {
    Kernel k {2};
    bdd copy = bddtrue;
    for (unsigned j = 0; j < n; ++j) {
      // Paired AP registration and valuation order: u0,c0,u1,c1,...
      const bdd u = k.ap ("u" + std::to_string (j), true);
      const bdd c = k.ap ("c" + std::to_string (j), false);
      copy &= bdd_biimp (u, c);
    }
    k.graph->new_edge (0, 0, copy);
    k.graph->new_edge (0, 1, !copy, {0});
    k.graph->new_edge (1, 1, bddtrue, {0});
    RowStore store {k.view (), {}, {}};
    store.enumerate_and_freeze ();
    const Rank safe {{{0, 0}}, 1}, unsafe {{{1, 1}}, 1};
    // Independently establish that this is the intended relation at K=1.
    for (auto letter : valuations (k.alphabet, k.alphabet.ap_vars)) {
      const auto post = rows::evaluate_sparse (store.cache, k.alphabet, safe, letter, 1);
      expect (post.value && *post.value == ((letter & copy) != bddfalse ? safe : unsafe),
              "copy kernel has exactly one safe output per input");
    }
    expect (bdd_exist (copy, k.alphabet.outputs) == bddtrue &&
            bdd_exist (bdd_forall (copy, k.alphabet.inputs), k.alphabet.outputs) == bddfalse,
            "copy kernel distinguishes forall-input exists-output from the reverse order");
    for (auto semantics : all_semantics) {
      const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
      const auto& node = result.nodes.at (result.initial);
      const std::size_t expected = semantics.output_choice == OutputChoice::constant ? 1u << n : 1;
      expect (result.status == forward_result_status::win_k && node.rank == safe &&
              node.covered_inputs == bddtrue && node.choices.size () == expected &&
              result.choices_created == expected, "copy kernel region count");
      for (const auto& choice : node.choices)
        expect (choice.active && choice.successor == result.initial, "copy kernel self-loop");
      check_regions (store, k.alphabet, result);
      if (semantics.successor_relation == SuccessorRelation::exact)
        std::cout << "copy n=" << n << ' '
                  << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
                  << ": " << node.choices.size () << " regions\n";
    }
  }
}

void missing_output_kernel () {
  Kernel k {2};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  const bdd legal = (!u) & (!c);
  k.graph->new_edge (0, 0, legal);
  k.graph->new_edge (0, 1, !legal, {0});
  k.graph->new_edge (1, 1, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  expect (bdd_exist (legal, k.alphabet.outputs) == !u &&
          bdd_exist (legal, k.alphabet.ap_vars) == bddtrue,
          "missing-output kernel distinguishes projection over outputs from all APs");
  for (auto semantics : all_semantics) {
    // Keep the sampling control independent of the build's default.
    const auto result = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::off}.solve ();
    expect (result.status == forward_result_status::lose_k, "input with no legal output loses");
    const auto& node = result.nodes.at (result.initial);
    expect (node.covered_inputs != bddtrue && node.choices.size () == 1 &&
            node.choices[0].input_region == !u, "missing legal output prevents full coverage");
    check_regions (store, k.alphabet, result);
    auto corrupt = result;
    corrupt.semantics.successor_relation = semantics.successor_relation == SuccessorRelation::exact
                                             ? SuccessorRelation::downward : SuccessorRelation::exact;
    expect (not verify_losing_proof (store, k.alphabet, 1, corrupt, {}, semantics).value,
            "losing certificate also validates the requested tag");

    const auto symbolic = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::on}.solve ();
    const auto& root = symbolic.nodes.at (symbolic.initial);
    expect (symbolic.status == forward_result_status::lose_k && root.losing,
            "symbolic missing-output search proves the initial node losing");
    expect (symbolic.expansions == 1 && symbolic.choices_created == 0 && root.choices.empty () &&
            root.covered_inputs == bddfalse,
            "symbolic missing-output search loses before creating a covering choice");
    check_losing_certificate (store, k.alphabet, 1, symbolic);
    const auto& proof = symbolic.proofs.at (*symbolic.initial_proof);
    expect (proof.record.reason == losing_reason::env_losing_input && proof.input == u &&
            proof.record.dependencies.empty (),
            "symbolic missing-output loss records the immediate losing input");
  }
}

void last_losing_input () {
  Kernel k {2};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  const bdd v = k.ap ("v", true), d = k.ap ("d", false);
  const bdd last = u & v;
  const bdd legal = !last & bdd_biimp (u, c) & bdd_biimp (v, d);
  k.graph->new_edge (0, 0, legal);
  k.graph->new_edge (0, 1, !legal, {0});
  k.graph->new_edge (1, 1, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  Reader reader {store, false, {}};
  Oracle oracle {reader, store, k.alphabet, 1};
  std::size_t input_checks = 0;
  expect (check_losing_inputs (store, k.alphabet, 1, reader.initial_rank (), {}, input_checks) == last,
          "the only losing input is the final total valuation");
  bdd remaining = bddtrue;
  const auto inputs = valuations (k.alphabet, k.alphabet.inputs);
  for (auto u : inputs) {
    const auto picked = oracle.model (remaining, Variables::inputs);
    expect (picked.value && *picked.value && **picked.value == u,
            "test enumeration agrees with deterministic model ordering");
    remaining &= !u;
  }
  expect (inputs.back () == last && remaining == bddfalse, "losing input is last in model order");
  for (auto semantics : all_semantics) {
    const auto sampled = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::off}.solve ();
    const auto symbolic = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::on}.solve ();
    expect (sampled.status == forward_result_status::lose_k && symbolic.status == sampled.status,
            "last-input kernel loses in both searches");
    expect (symbolic.expansions == 1 && symbolic.choices_created == 0 &&
            symbolic.expansions < sampled.expansions && sampled.choices_created > 0,
            "symbolic search finds the final losing input before any arbitrary selection");
    const auto& proof = symbolic.proofs.at (*symbolic.initial_proof);
    expect (proof.record.reason == losing_reason::env_losing_input && proof.input == last &&
            proof.record.dependencies.empty (), "immediate symbolic witness uses the existing rule");
    check_losing_certificate (store, k.alphabet, 1, symbolic);
    std::cout << "last losing input "
              << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
              << ": S2 off/on " << sampled.expansions << '/' << symbolic.expansions
              << " expansions, " << sampled.choices_created << '/' << symbolic.choices_created
              << " choices\n";
  }
}

void later_losing_input () {
  Kernel k {3};
  k.ap ("u", true);
  k.ap ("c", false);
  k.ap ("unused_input", true);
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (1, 2, bddtrue, {0});
  k.graph->new_edge (2, 2, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  Reader reader {store, false, {}};
  std::size_t input_checks = 0;
  expect (check_losing_inputs (store, k.alphabet, 1, reader.initial_rank (), {}, input_checks)
              == bddfalse, "delayed-loss root has no immediate losing input");
  for (auto semantics : all_semantics) {
    const auto sampled = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::off}.solve ();
    const auto symbolic = Search {store, k.alphabet, 1, {}, semantics, LosingInputSearch::on}.solve ();
    expect (symbolic.status == forward_result_status::lose_k && symbolic.status == sampled.status,
            "empty D falls back and still discovers the later loss");
    const auto& root = symbolic.nodes.at (symbolic.initial);
    expect (symbolic.choices_created == 1 && root.choices.size () == 1 &&
            root.choices[0].input_region == bddtrue && not root.choices[0].active &&
            symbolic.reopened_sources > 0, "fallback creates a choice that is invalidated and reopened");
    expect (symbolic.expansions == sampled.expansions &&
            symbolic.choices_created == sampled.choices_created &&
            symbolic.reopened_sources == sampled.reopened_sources,
            "fallback retains the delayed-loss search behavior");
    const auto& proof = symbolic.proofs.at (*symbolic.initial_proof);
    expect (not proof.record.dependencies.empty (), "later root loss needs an earlier proof");
    std::vector<Rank> earlier;
    for (auto dep : proof.record.dependencies)
      earlier.push_back (symbolic.proofs.at (dep).rank);
    expect (check_losing_inputs (store, k.alphabet, 1, root.rank, earlier, input_checks) == bddtrue,
            "known losing successors make every root input losing");
    check_losing_certificate (store, k.alphabet, 1, symbolic);
    // H=true omits both inputs, but the witness must supply both of them.
    expect (proof.input == valuations (k.alphabet, k.alphabet.inputs).front (),
            "symbolic proof fills even inputs absent from H");
    auto corrupt = symbolic;
    corrupt.proofs.at (*corrupt.initial_proof).input = bddtrue;
    const auto rejected = verify_losing_proof (store, k.alphabet, 1, corrupt, {}, semantics);
    expect (not rejected.value && rejected.unknown == Unknown::invalid_query,
            "verifier rejects storing a symbolic region instead of a total input cube");
  }
}

void losing_input_query_failure () {
  // Enough row work that the Bad query budget exceeds all expansion setup
  // queries. Its projection is false: a failed projection must not return it.
  Kernel k {8};
  k.ap ("u", true);
  k.ap ("c", false);
  for (unsigned q = 1; q < 8; ++q) {
    k.graph->new_edge (0, q, bddtrue);
    k.graph->new_edge (q, q, bddtrue);
  }
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  Reader reader {store, false, {}};
  const Rank rank = reader.initial_rank ();
  std::size_t checkpoints = 0;
  letters::QueryLimits counting;
  counting.aborted = [] (void* data) { ++*static_cast<std::size_t*> (data); return false; };
  counting.abort_data = &checkpoints;
  Oracle measure {reader, store, k.alphabet, 1};
  measure.set_limits (counting);
  expect (measure.query<bool> ([] (auto&) { return true; }).value.has_value (),
          "validate alphabet before measuring Bad");
  checkpoints = 0;
  expect (measure.bad (rank, {}, 0).value.has_value (), "measure the complete Bad query");
  const auto bad_steps = checkpoints;
  auto operations = [] (Oracle& oracle) {
    std::size_t result = 0;
    oracle.report (Reporter {[&] (const auto& key, const auto& value) {
      if (key == "bdd_operations") result = std::stoull (value);
    }}, "");
    return result;
  };
  for (bool limited : {false, true}) {
    Oracle oracle {reader, store, k.alphabet, 1};
    expect (oracle.query<bool> ([] (auto&) { return true; }).value.has_value (),
            "validate fresh alphabet before the budgeted query");
    const auto before = operations (oracle);
    letters::QueryLimits limits;
    if (limited) limits.max_steps = bad_steps;
    oracle.set_limits (limits);
    const auto result = oracle.bad (rank, {}, 0, bddtrue);
    if (limited) {
      expect (not result.value && result.unknown == Unknown::resource_limit,
              "budget failure during forall is UNKNOWN, not a present false projection");
      // The old query's final step pays for entry to forall; its post-operation
      // checkpoint exhausts this budget, before the land forming D can run.
      expect (operations (oracle) == operations (measure) + 1,
              "budget fails after the new forall operation and before conjunction");
      oracle.set_limits ({});
      const auto recovered = oracle.bad (rank, {}, 0, bddtrue);
      expect (recovered.value && recovered.value->second == bddfalse,
              "a fresh query after quantification failure can return a present false");
    }
    else {
      expect (result.value && result.value->second == bddfalse && result.unknown == Unknown::none,
              "unsatisfiable D is a present result");
      expect (operations (oracle) > before, "projection uses the checked BDD boundary");
    }
  }
  for (auto semantics : all_semantics) {
    Limits limits;
    limits.queries.max_steps = bad_steps;
    const auto failed = Search {store, k.alphabet, 1, limits, semantics, LosingInputSearch::on}.solve ();
    // Search retains its existing resource-limit subtype of an inconclusive
    // result; neither WIN_K nor LOSE_K is published.
    expect (failed.status == forward_result_status::resource_limit &&
            failed.failure == Unknown::resource_limit && failed.expansions == 1 &&
            failed.nodes.at (failed.initial).active_rows_complete &&
            failed.choices_created == 0 && failed.proofs.empty (),
            "search quantification budget exhaustion remains inconclusive");
  }
}

void corrupt_certificates () {
  // The two safe targets have disjoint projections. The safe ranks still form
  // an inductive invariant after changing a region/edge, so the final invariant
  // cannot hide an unsound per-choice verifier.
  Kernel k {4};
  const bdd u = k.ap ("u", true), c = k.ap ("c", false);
  k.graph->new_edge (0, 1, (!u) & (!c));
  k.graph->new_edge (0, 2, u & c);
  k.graph->new_edge (0, 3, !bdd_biimp (u, c), {0});
  k.graph->new_edge (1, 1, bddtrue);
  k.graph->new_edge (2, 2, bddtrue);
  k.graph->new_edge (3, 3, bddtrue, {0});
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  for (auto semantics : all_semantics) {
    const auto good = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    expect (good.status == forward_result_status::win_k &&
            good.nodes.at (good.initial).choices.size () == 2, "split-target fixture wins");
    expect (verify_winning_certificate (store, k.alphabet, 1, good, {}, semantics).value.has_value (),
            "uncorrupted split-target certificate replays");
    check_regions (store, k.alphabet, good);
    auto reject = [&] (const std::string& name, auto mutate) {
      auto bad = good;
      mutate (bad, bad.nodes.at (bad.initial));
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, bad, {}, semantics);
      expect (not checked.value && checked.unknown == Unknown::invalid_query, name);
    };
    reject ("region beyond freshly rebuilt projection rejected", [] (auto&, auto& node) {
      node.choices[0].input_region = bddtrue;
    });
    reject ("changed safe target rejected", [] (auto&, auto& node) {
      node.choices[0].successor = node.choices[1].successor;
    });
    reject ("removed edge rejected", [] (auto&, auto& node) { node.choices.pop_back (); });
    reject ("covered_inputs must equal the reconstructed union", [] (auto&, auto& node) {
      node.covered_inputs = bddfalse;
    });
    reject ("omitted active row rejected", [] (auto& bad, auto& node) {
      bad.nodes[node.choices[0].successor].active_rows_complete = false;
    });
    reject ("forged successor-relation tag rejected", [&] (auto& bad, auto&) {
      bad.semantics.successor_relation = semantics.successor_relation == SuccessorRelation::exact
                                            ? SuccessorRelation::downward : SuccessorRelation::exact;
    });
    reject ("forged output-choice tag rejected", [&] (auto& bad, auto&) {
      bad.semantics.output_choice = semantics.output_choice == OutputChoice::constant
                                       ? OutputChoice::existential : OutputChoice::constant;
    });
    if (semantics.output_choice == OutputChoice::existential) {
      reject ("existential choice must not contain an invalid constant cube", [] (auto&, auto& node) {
        node.choices[0].constant_output = bddtrue; // Real outputs exist: this is not total.
      });
      reject ("existential choice must not contain even a total constant cube", [&] (auto&, auto& node) {
        node.choices[0].constant_output = !c;
      });
    }
    else {
      reject ("constant choice must contain a cube", [] (auto&, auto& node) {
        node.choices[0].constant_output.reset ();
      });
      reject ("constant choice must contain a total cube", [] (auto&, auto& node) {
        node.choices[0].constant_output = bddtrue;
      });
      reject ("constant choice must contain a satisfying cube", [&] (auto&, auto& node) {
        node.choices[0].constant_output = c;
      });
    }
    for (auto invalid : {ChoiceSemantics {static_cast<SuccessorRelation> (99), semantics.output_choice},
                         ChoiceSemantics {semantics.successor_relation, static_cast<OutputChoice> (99)}}) {
      auto bad = good;
      bad.semantics = invalid;
      // Matching malformed tags are still invalid, even when supplied by the caller.
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, bad, {}, invalid);
      expect (not checked.value && checked.unknown == Unknown::invalid_query,
              "malformed semantics rejected even when matching the request");
      const auto searched = Search {store, k.alphabet, 1, {}, invalid}.solve ();
      expect (searched.status == forward_result_status::unknown &&
              searched.failure == Unknown::invalid_query, "invalid search option is inconclusive");
    }
  }
}

void successor_relations () {
  Kernel k {3};
  const bdd u = k.ap ("u", true);
  k.ap ("c", false);
  k.graph->new_edge (0, 1, bddtrue);
  k.graph->new_edge (0, 2, u);
  k.graph->new_edge (1, 1, bddtrue);
  k.graph->new_edge (2, 2, bddtrue);
  RowStore store {k.view (), {}, {}};
  store.enumerate_and_freeze ();
  for (auto semantics : all_semantics) {
    auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
    expect (result.status == forward_result_status::win_k, "successor-relation fixture wins");
    auto& node = result.nodes.at (result.initial);
    expect (node.choices.size () == 2, "successor-relation fixture has two sampled targets");
    auto upper = node.choices.back ();
    expect (result.nodes[upper.successor].rank == Rank {{{1, 0}, {2, 0}}, 1},
            "upper target has both coordinates");
    upper.input_region = bddtrue;
    node.choices = {upper};
    const auto checked = verify_winning_certificate (store, k.alphabet, 1, result, {}, semantics);
    const bool downward = semantics.successor_relation == SuccessorRelation::downward;
    expect (checked.value.has_value () == downward, "verifier respects independent successor relation");
    std::size_t count = 0;
    expect (choice_regions (store, k.alphabet, 1, result, count) == (downward ? 0 : 7),
            "arithmetic distinguishes equality from strict downward reachability");
  }
}

void empty_alphabets_and_failures () {
  for (unsigned mask : {0, 1, 2, 3}) {
    Kernel k {2};
    if (mask & 1)
      k.ap ("u", true);
    const bdd legal = mask & 2 ? k.ap ("c", false) : bdd (bddtrue);
    k.graph->new_edge (0, 0, legal);
    k.graph->new_edge (0, 1, !legal, {0});
    k.graph->new_edge (1, 1, bddtrue, {0});
    RowStore store {k.view (), {}, {}};
    store.enumerate_and_freeze ();
    for (auto semantics : all_semantics) {
      const auto result = Search {store, k.alphabet, 1, {}, semantics}.solve ();
      expect (result.status == forward_result_status::win_k && result.choices_created == 1,
              "empty input/output alphabets retain their single empty valuation");
      check_regions (store, k.alphabet, result);
      Limits limits;
      limits.verifier_queries.max_steps = 0;
      const auto checked = verify_winning_certificate (store, k.alphabet, 1, result, limits, semantics);
      expect (not checked.value && checked.unknown == Unknown::resource_limit,
              "verifier budget failure has no Boolean result");
      const auto failed = Search {store, k.alphabet, 1, limits, semantics}.solve ();
      expect (failed.status == forward_result_status::unknown && failed.failure == Unknown::resource_limit,
              "search whose verification runs out of budget stays UNKNOWN");
      limits = {};
      limits.queries.aborted = [] (void*) { return true; };
      const auto aborted = Search {store, k.alphabet, 1, limits, semantics}.solve ();
      expect (aborted.status == forward_result_status::unknown && aborted.failure == Unknown::aborted,
              "aborted search query stays UNKNOWN");
    }
    Reader reader {store, false, {}};
    Oracle oracle {reader, store, k.alphabet, 1};
    const auto present_false = oracle.query<bdd> ([&] (auto& b) {
      return b.exists (bddfalse, b.vars (Variables::outputs));
    });
    expect (present_false.value && *present_false.value == bddfalse &&
            present_false.unknown == Unknown::none, "false projection is a present result");
    bool abort_projection = false;
    letters::QueryLimits limits;
    limits.aborted = [] (void* flag) { return *static_cast<bool*> (flag); };
    limits.abort_data = &abort_projection;
    oracle.set_limits (limits);
    const auto absent = oracle.query<bdd> ([&] (auto& b) {
      abort_projection = true; // Fail inside the projection, after alphabet validation.
      return b.exists (bddfalse, b.vars (Variables::outputs));
    });
    expect (not absent.value && absent.unknown == Unknown::aborted,
            "failed existential projection is absent, distinguishable from present false");
    oracle.set_limits ({});
    const auto recovered = oracle.query<bdd> ([&] (auto& b) {
      return b.exists (bddfalse, b.vars (Variables::outputs));
    });
    expect (recovered.value && *recovered.value == bddfalse, "fresh query after failure can return false");
  }
}

int main () {
  const int report_fd = open ("/dev/null", O_WRONLY);
  const auto report = pipe_reporter (report_fd);
  try {
    for (auto semantics : all_semantics) {
      std::cout << (semantics.successor_relation == SuccessorRelation::exact ? "exact" : "downward")
                << '+' << (semantics.output_choice == OutputChoice::constant ? "constant" : "existential")
                << (semantics == default_choice_semantics ? " (default)" : "") << '\n';
      if (const int code = differential (report, semantics))
        return code;
    }
    copy_kernels ();
    lean_verifier_dependencies ();
    lean_verifier_winning_cache ();
    lean_verifier_retained_thresholds ();
    verifier_phase_metrics ();
    certificate_shape_metrics ();
    incremental_coverage_events ();
    incremental_bad_unions ();
    incremental_bad_budget ();
    incremental_bad_failure ();
    incremental_bad_publication_failure ();
    incremental_bad_verifier_isolation ();
    incremental_coverage_property ();
    incremental_coverage_failure ();
    missing_output_kernel ();
    last_losing_input ();
    later_losing_input ();
    losing_input_query_failure ();
    corrupt_certificates ();
    successor_relations ();
    empty_alphabets_and_failures ();
  } catch (const std::exception& e) {
    std::cerr << e.what () << '\n';
    return 17;
  }
  std::cout << "S2 last-input, fallback, proof chronology and quantification-budget checks passed\n"
               "quantifier-order, certificate corruption, empty-alphabet and query-failure checks passed\n";
  close (report_fd);
}
